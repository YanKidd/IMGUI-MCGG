/**
 * ============================================================================
 * IMGUI-MCGG — Dear ImGui overlay template for Unity Android games.
 *
 * Hooks eglSwapBuffers via Dobby + Input.GetTouch via IL2CPP method lookup,
 * then renders a Dear ImGui overlay with a login dialog and sidebar-tab
 * scaffold. All cheat/feature code stripped — pure scaffolding.
 *
 * Drop-in template for building custom Magic Chess: Go Go mods. Replace
 * the empty tab bodies + fill in the login key validation with your own
 * backend.
 * ============================================================================
 */

#include <jni.h>
#include <dlfcn.h>
#include <android/log.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include <pthread.h>
#include <unistd.h>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "xdl.h"
#include "dobby.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

#include "Il2CppWrapper.hpp"
#include "MonoStructures.hpp"

// ── Logging ──────────────────────────────────────────────────────────────
#define IS_DEBUG 1

#if IS_DEBUG
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "IMGUI-MCGG", __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, "IMGUI-MCGG", __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "IMGUI-MCGG", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#define LOGW(...) ((void)0)
#define LOGE(...) ((void)0)
#endif

// ── Target process guard ─────────────────────────────────────────────────
static bool IsTargetProcess() {
  FILE *fp = fopen("/proc/self/cmdline", "rb");
  if (!fp) return false;
  char buf[256] = {0};
  size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
  fclose(fp);
  if (n == 0) return false;
  // Magic Chess: Go Go child process suffix.
  return strstr(buf, ":UnityKillsMe") != nullptr;
}

// ── ImGui state ──────────────────────────────────────────────────────────
static std::atomic<bool> g_imguiInitialized{false};
static std::atomic<int> g_glWidth{0};
static std::atomic<int> g_glHeight{0};

// Login state — placeholder. Plug in your own key validation backend.
enum class KeyStatus { Idle, Checking, Valid, Invalid };
static std::atomic<KeyStatus> g_keyStatus{KeyStatus::Idle};
static std::mutex g_keyMtx;
static std::string g_keyMessage;
static char g_keyInput[64] = {0};

// Sidebar tab state.
static int g_currentTab = 0;
static const char *const kTabNames[] = {"Info", "Room", "Config"};
static constexpr int kTabCount = sizeof(kTabNames) / sizeof(kTabNames[0]);

// ── Hook originals ───────────────────────────────────────────────────────
namespace Originals {
EGLBoolean (*EglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
Il2CppObject *(*SystemData_LogicGetRoomData)() = nullptr;
Il2CppString *(*RoomData_get_strName)(Il2CppObject *self) = nullptr;
} // namespace Originals

// ── IL2CPP method resolver ───────────────────────────────────────────────
namespace Il2CppApi {
Il2CppDomain *(*il2cpp_domain_get)() = nullptr;
Il2CppAssembly **(*il2cpp_domain_get_assemblies)(const Il2CppDomain *domain,
                                                 size_t *size) = nullptr;
const Il2CppImage *(*il2cpp_assembly_get_image)(
    const Il2CppAssembly *assembly) = nullptr;
Il2CppClass **(*il2cpp_image_get_classes)(const Il2CppImage *image,
                                          size_t *size) = nullptr;
Il2CppClass *(*il2cpp_class_from_name)(const Il2CppImage *image,
                                       const char *namespaze,
                                       const char *name) = nullptr;
const char *(*il2cpp_class_get_name)(Il2CppClass *klass) = nullptr;
Il2CppClass *(*il2cpp_class_get_parent)(Il2CppClass *klass) = nullptr;
Il2CppClass *(*il2cpp_class_get_nested_types)(Il2CppClass *klass,
                                              void **iter) = nullptr;
const MethodInfo *(*il2cpp_class_get_method_from_name)(Il2CppClass *klass,
                                                       const char *name,
                                                       int argsCount) = nullptr;
bool (*il2cpp_class_is_subclass_of)(Il2CppClass *klass, Il2CppClass *klassc,
                                    bool checkInterfaces) = nullptr;
Il2CppThread *(*il2cpp_thread_attach)(Il2CppDomain *domain) = nullptr;
} // namespace Il2CppApi

static std::atomic<bool> g_il2cppReady{false};

#define RESOLVE_IL2CPP_API(handle, name)                                      \
  do {                                                                        \
    void *sym = xdl_sym((handle), #name, nullptr);                            \
    if (sym != nullptr) {                                                     \
      Il2CppApi::name = reinterpret_cast<decltype(Il2CppApi::name)>(sym);     \
    } else {                                                                  \
      LOGW("[IL2CPP] missing API %s", #name);                                 \
    }                                                                         \
  } while (0)

static bool InitIl2CppApi(void *handle) {
  if (handle == nullptr)
    return false;

  RESOLVE_IL2CPP_API(handle, il2cpp_domain_get);
  RESOLVE_IL2CPP_API(handle, il2cpp_domain_get_assemblies);
  RESOLVE_IL2CPP_API(handle, il2cpp_assembly_get_image);
  RESOLVE_IL2CPP_API(handle, il2cpp_image_get_classes);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_from_name);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_get_name);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_get_parent);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_get_nested_types);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_get_method_from_name);
  RESOLVE_IL2CPP_API(handle, il2cpp_class_is_subclass_of);
  RESOLVE_IL2CPP_API(handle, il2cpp_thread_attach);

  bool ok = Il2CppApi::il2cpp_domain_get != nullptr &&
            Il2CppApi::il2cpp_domain_get_assemblies != nullptr &&
            Il2CppApi::il2cpp_assembly_get_image != nullptr &&
            Il2CppApi::il2cpp_class_from_name != nullptr &&
            Il2CppApi::il2cpp_class_get_method_from_name != nullptr;
  if (!ok)
    return false;

  if (Il2CppApi::il2cpp_thread_attach != nullptr) {
    Il2CppDomain *domain = Il2CppApi::il2cpp_domain_get();
    if (domain != nullptr)
      Il2CppApi::il2cpp_thread_attach(domain);
  }

  g_il2cppReady.store(true, std::memory_order_release);
  return true;
}

static Il2CppClass *FindNestedInHierarchy(Il2CppClass *klass,
                                          const char *name) {
  if (klass == nullptr || name == nullptr ||
      Il2CppApi::il2cpp_class_get_nested_types == nullptr ||
      Il2CppApi::il2cpp_class_get_name == nullptr)
    return nullptr;

  Il2CppClass *current = klass;
  while (current != nullptr) {
    void *iter = nullptr;
    Il2CppClass *nested = nullptr;
    while ((nested =
                Il2CppApi::il2cpp_class_get_nested_types(current, &iter)) !=
           nullptr) {
      const char *nestedName = Il2CppApi::il2cpp_class_get_name(nested);
      if (nestedName != nullptr && strcmp(nestedName, name) == 0)
        return nested;
    }
    if (Il2CppApi::il2cpp_class_get_parent == nullptr)
      break;
    current = Il2CppApi::il2cpp_class_get_parent(current);
  }
  return nullptr;
}

static Il2CppClass *FindNestedInSubclasses(const Il2CppImage *image,
                                           Il2CppClass *klass,
                                           const char *name) {
  Il2CppClass *found = FindNestedInHierarchy(klass, name);
  if (found != nullptr)
    return found;
  if (image == nullptr || Il2CppApi::il2cpp_image_get_classes == nullptr ||
      Il2CppApi::il2cpp_class_is_subclass_of == nullptr ||
      Il2CppApi::il2cpp_class_get_nested_types == nullptr ||
      Il2CppApi::il2cpp_class_get_name == nullptr)
    return nullptr;

  size_t classCount = 0;
  Il2CppClass **classes = Il2CppApi::il2cpp_image_get_classes(image,
                                                              &classCount);
  if (classes == nullptr || classCount == 0 || classCount > 1000000)
    return nullptr;

  for (size_t i = 0; i < classCount; ++i) {
    Il2CppClass *candidate = classes[i];
    if (candidate == nullptr || candidate == klass)
      continue;
    if (!Il2CppApi::il2cpp_class_is_subclass_of(candidate, klass, false))
      continue;

    void *iter = nullptr;
    Il2CppClass *nested = nullptr;
    while ((nested =
                Il2CppApi::il2cpp_class_get_nested_types(candidate, &iter)) !=
           nullptr) {
      const char *nestedName = Il2CppApi::il2cpp_class_get_name(nested);
      if (nestedName != nullptr && strcmp(nestedName, name) == 0)
        return nested;
    }
  }
  return nullptr;
}

static Il2CppClass *ResolveClassFromName(const Il2CppImage *image,
                                         const char *namespaze,
                                         const char *className) {
  if (image == nullptr || namespaze == nullptr || className == nullptr ||
      Il2CppApi::il2cpp_class_from_name == nullptr)
    return nullptr;

  Il2CppClass *klass =
      Il2CppApi::il2cpp_class_from_name(image, namespaze, className);
  if (klass != nullptr)
    return klass;

  const char *dot = strchr(className, '.');
  if (dot == nullptr)
    return nullptr;

  std::string outer(className, static_cast<size_t>(dot - className));
  Il2CppClass *current =
      Il2CppApi::il2cpp_class_from_name(image, namespaze, outer.c_str());
  const char *part = dot + 1;

  while (current != nullptr && part != nullptr && *part != '\0') {
    const char *next = strchr(part, '.');
    std::string nested(part, next == nullptr
                                 ? strlen(part)
                                 : static_cast<size_t>(next - part));
    current = FindNestedInSubclasses(image, current, nested.c_str());
    if (next == nullptr)
      break;
    part = next + 1;
  }
  return current;
}

static void *GetMethodFromName(const char *namespaze, const char *className,
                               const char *methodName, int argCount) {
  if (!g_il2cppReady.load(std::memory_order_acquire) || namespaze == nullptr ||
      className == nullptr || methodName == nullptr)
    return nullptr;

  Il2CppDomain *domain = Il2CppApi::il2cpp_domain_get();
  if (domain == nullptr)
    return nullptr;

  size_t count = 0;
  Il2CppAssembly **assemblies =
      Il2CppApi::il2cpp_domain_get_assemblies(domain, &count);
  if (assemblies == nullptr || count == 0 || count > 10000)
    return nullptr;

  for (size_t i = 0; i < count; ++i) {
    if (assemblies[i] == nullptr)
      continue;
    const Il2CppImage *image =
        Il2CppApi::il2cpp_assembly_get_image(assemblies[i]);
    Il2CppClass *klass = ResolveClassFromName(image, namespaze, className);
    if (klass == nullptr)
      continue;

    const MethodInfo *method =
        Il2CppApi::il2cpp_class_get_method_from_name(klass, methodName,
                                                     argCount);
    if (method != nullptr && method->methodPointer != nullptr)
      return reinterpret_cast<void *>(method->methodPointer);
  }
  return nullptr;
}

static void AssignRoomInfoMethods() {
  void *roomData =
      GetMethodFromName("", "SystemData", "LogicGetRoomData", 0);
  if (roomData != nullptr) {
    Originals::SystemData_LogicGetRoomData =
        reinterpret_cast<decltype(Originals::SystemData_LogicGetRoomData)>(
            roomData);
    LOGI("[ROOM] resolved SystemData.LogicGetRoomData");
  } else {
    LOGW("[ROOM] failed to resolve SystemData.LogicGetRoomData");
  }

  void *nameGetter =
      GetMethodFromName("", "SystemData.RoomData", "get_strName", 0);
  if (nameGetter != nullptr) {
    Originals::RoomData_get_strName =
        reinterpret_cast<decltype(Originals::RoomData_get_strName)>(
            nameGetter);
    LOGI("[ROOM] resolved SystemData.RoomData.get_strName");
  } else {
    LOGW("[ROOM] failed to resolve SystemData.RoomData.get_strName");
  }
}

#undef RESOLVE_IL2CPP_API

// ── Theme ────────────────────────────────────────────────────────────────
static void ApplyImGuiTheme() {
  ImGuiStyle &style = ImGui::GetStyle();
  style.WindowRounding = 6.0f;
  style.FrameRounding = 4.0f;
  style.ScrollbarRounding = 8.0f;
  style.GrabRounding = 4.0f;
  style.TabRounding = 4.0f;
  style.WindowPadding = ImVec2(10, 10);
  style.FramePadding = ImVec2(8, 4);
  style.ItemSpacing = ImVec2(8, 6);
  style.ScrollbarSize = 10.0f;

  ImVec4 *c = style.Colors;
  c[ImGuiCol_WindowBg]          = ImVec4(0.075f, 0.082f, 0.103f, 0.97f);
  c[ImGuiCol_Border]            = ImVec4(0.251f, 0.263f, 0.294f, 0.50f);
  c[ImGuiCol_FrameBg]           = ImVec4(0.118f, 0.129f, 0.157f, 1.00f);
  c[ImGuiCol_FrameBgHovered]    = ImVec4(0.176f, 0.196f, 0.235f, 1.00f);
  c[ImGuiCol_FrameBgActive]     = ImVec4(0.235f, 0.255f, 0.294f, 1.00f);
  c[ImGuiCol_TitleBgActive]     = ImVec4(0.118f, 0.129f, 0.157f, 1.00f);
  c[ImGuiCol_Button]            = ImVec4(0.235f, 0.255f, 0.294f, 1.00f);
  c[ImGuiCol_ButtonHovered]     = ImVec4(0.314f, 0.333f, 0.373f, 1.00f);
  c[ImGuiCol_ButtonActive]      = ImVec4(0.392f, 0.412f, 0.451f, 1.00f);
  c[ImGuiCol_Header]            = ImVec4(0.176f, 0.196f, 0.235f, 1.00f);
  c[ImGuiCol_HeaderHovered]     = ImVec4(0.235f, 0.255f, 0.294f, 1.00f);
  c[ImGuiCol_HeaderActive]      = ImVec4(0.314f, 0.333f, 0.373f, 1.00f);
  c[ImGuiCol_Tab]               = ImVec4(0.118f, 0.129f, 0.157f, 1.00f);
  c[ImGuiCol_TabHovered]        = ImVec4(0.314f, 0.333f, 0.373f, 1.00f);
  c[ImGuiCol_TabActive]         = ImVec4(0.235f, 0.255f, 0.294f, 1.00f);
  c[ImGuiCol_ScrollbarBg]       = ImVec4(0.082f, 0.090f, 0.110f, 1.00f);
  c[ImGuiCol_ScrollbarGrab]     = ImVec4(0.251f, 0.263f, 0.294f, 1.00f);
  c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.310f, 0.322f, 0.353f, 1.00f);
  c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.933f, 0.302f, 0.302f, 1.00f);
  c[ImGuiCol_CheckMark]         = ImVec4(0.6f, 0.8f, 1.0f, 1.00f);
  c[ImGuiCol_SliderGrab]        = ImVec4(0.6f, 0.8f, 1.0f, 1.00f);
  c[ImGuiCol_SliderGrabActive]  = ImVec4(0.7f, 0.9f, 1.0f, 1.00f);
}

// ── Login dialog ─────────────────────────────────────────────────────────
static void RenderLoginDialog() {
  ImGui::SetNextWindowSize(ImVec2(420, 240), ImGuiCond_Always);
  ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                                 ImGui::GetIO().DisplaySize.y * 0.5f),
                          ImGuiCond_Always, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowBgAlpha(0.97f);
  if (ImGui::Begin("IMGUI-MCGG##LoginWin", nullptr,
                   ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoCollapse)) {
    ImGui::Spacing();
    ImGui::Text("Activation Key:");
    ImGui::TextDisabled("Format: XXX-xxxxxxxx-xxxxxxxx-xxxxxxxxxxxx");
    ImGui::SetNextItemWidth(-1);
    ImGui::InputText("##KeyInput", g_keyInput, sizeof(g_keyInput));
    ImGui::Spacing();

    KeyStatus st = g_keyStatus.load();
    bool checking = (st == KeyStatus::Checking);

    if (checking) ImGui::BeginDisabled();
    if (ImGui::Button(checking ? "Checking..." : "LOGIN", ImVec2(-1, 34))) {
      if (strlen(g_keyInput) > 0) {
        // TODO: replace with your own backend validation.
        // For template: accept any non-empty input.
        g_keyStatus.store(KeyStatus::Checking);
        std::thread([] {
          std::this_thread::sleep_for(std::chrono::seconds(1));
          g_keyStatus.store(KeyStatus::Valid);
        }).detach();
      }
    }
    if (checking) ImGui::EndDisabled();

    if (ImGui::Button("GET KEY", ImVec2(-1, 34))) {
      // TODO: open your key-issuer URL via `am start -a android.intent.action.VIEW -d <url>`
    }

    ImGui::Spacing();
    if (st == KeyStatus::Invalid) {
      std::string msg;
      {
        std::lock_guard<std::mutex> lk(g_keyMtx);
        msg = g_keyMessage;
      }
      ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "%s", msg.c_str());
    } else if (checking) {
      ImGui::TextColored(ImVec4(1, 1, 0.3f, 1), "Validating key...");
    }
  }
  ImGui::End();
}

// ── Main mod menu ────────────────────────────────────────────────────────
static void RenderInfoTab() {
  ImGui::TextDisabled("Runtime");
  if (ImGui::BeginTable("##InfoTable", 2,
                        ImGuiTableFlags_BordersInnerV |
                            ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

    const char *labels[] = {
        "Project",
        "Target",
        "ABI",
        "Framebuffer",
        "ImGui",
        "Log tag",
    };
    char framebuffer[32];
    snprintf(framebuffer, sizeof(framebuffer), "%d x %d",
             g_glWidth.load(std::memory_order_relaxed),
             g_glHeight.load(std::memory_order_relaxed));
    const char *values[] = {
        "IMGUI-MCGG",
        "Magic Chess: Go Go",
        "arm64-v8a",
        framebuffer,
        g_imguiInitialized.load(std::memory_order_relaxed) ? "Initialized"
                                                           : "Pending",
        "IMGUI-MCGG",
    };

    for (int i = 0; i < 6; ++i) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(labels[i]);
      ImGui::TableSetColumnIndex(1);
      ImGui::TextUnformatted(values[i]);
    }
    ImGui::EndTable();
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Gunakan tab Config untuk pengaturan menu.");
}

static void AppendCodepointAsUtf8(std::string &out, uint32_t cp) {
  if (cp <= 0x7Fu) {
    out.push_back(static_cast<char>(cp));
  } else if (cp <= 0x7FFu) {
    out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else if (cp <= 0xFFFFu) {
    out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  } else {
    out.push_back(static_cast<char>(0xF0u | (cp >> 18)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
    out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
  }
}

static std::string Il2CppStringToStdString(const Il2CppString *value) {
  if (value == nullptr || value->length <= 0)
    return {};

  const auto *chars = reinterpret_cast<const uint16_t *>(value->chars);
  std::string out;
  out.reserve(static_cast<size_t>(value->length) * 3u);

  for (int32_t i = 0; i < value->length; ++i) {
    uint32_t cp = chars[i];
    if (cp >= 0xD800u && cp <= 0xDBFFu && i + 1 < value->length) {
      uint32_t low = chars[i + 1];
      if (low >= 0xDC00u && low <= 0xDFFFu) {
        cp = 0x10000u + ((cp - 0xD800u) << 10) + (low - 0xDC00u);
        ++i;
      }
    }
    if (cp >= 0xD800u && cp <= 0xDFFFu)
      cp = '?';
    AppendCodepointAsUtf8(out, cp);
  }
  return out;
}

template <typename T>
static T ReadField(const char *base, size_t offset) {
  T value{};
  memcpy(&value, base + offset, sizeof(T));
  return value;
}

struct RoomScoutEntry {
  uint64_t uid{};
  std::string name;
  uint32_t country{};
  uint32_t roleLevel{};
  uint32_t magicRank{};
  uint32_t magicCup{};
  uint32_t elo{};
  uint32_t mythPoint{};
  uint32_t battleCnt{};
  bool isRobot{};
  bool isFakeRole{};
  bool isNewPlayer{};
};

static bool ReadRoomRoster(std::vector<RoomScoutEntry> &out) {
  out.clear();
  if (Originals::SystemData_LogicGetRoomData == nullptr)
    return false;

  Il2CppObject *raw = Originals::SystemData_LogicGetRoomData();
  if (raw == nullptr)
    return false;

  const auto *roster = reinterpret_cast<Unity::List<Il2CppObject *> *>(raw);
  int count = roster->GetSize();
  if (count <= 0 || count > 64 || roster->items == nullptr)
    return false;

  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) {
    Il2CppObject *rd = roster->get_Item(i);
    if (rd == nullptr)
      continue;

    const char *base = reinterpret_cast<const char *>(rd);
    RoomScoutEntry entry{};
    entry.uid = ReadField<uint64_t>(base, 0x18);
    entry.isRobot = ReadField<bool>(base, 0x38);
    entry.country = ReadField<uint32_t>(base, 0x4C);
    entry.elo = ReadField<uint32_t>(base, 0x124);
    entry.roleLevel = ReadField<uint32_t>(base, 0x128);
    entry.isNewPlayer = ReadField<bool>(base, 0x12C);
    entry.mythPoint = ReadField<uint32_t>(base, 0x1B4);
    entry.magicCup = ReadField<uint32_t>(base, 0x240);
    entry.magicRank = ReadField<uint32_t>(base, 0x244);
    entry.battleCnt = ReadField<uint32_t>(base, 0x264);
    entry.isFakeRole = ReadField<bool>(base, 0x2F0);

    if (Originals::RoomData_get_strName != nullptr)
      entry.name = Il2CppStringToStdString(Originals::RoomData_get_strName(rd));
    if (entry.name.empty())
      entry.name = entry.isRobot ? "[BOT]" : "Unknown";

    out.push_back(std::move(entry));
  }
  return !out.empty();
}

static const char *CountryCodeToName(uint32_t code) {
  switch (code) {
    case 0:   return "--";
    case 1:   return "US";
    case 7:   return "RU";
    case 20:  return "EG";
    case 33:  return "FR";
    case 34:  return "ES";
    case 39:  return "IT";
    case 44:  return "UK";
    case 49:  return "DE";
    case 52:  return "MX";
    case 54:  return "AR";
    case 55:  return "BR";
    case 60:  return "MY";
    case 61:  return "AU";
    case 62:  return "ID";
    case 63:  return "PH";
    case 64:  return "NZ";
    case 65:  return "SG";
    case 66:  return "TH";
    case 81:  return "JP";
    case 82:  return "KR";
    case 84:  return "VN";
    case 86:  return "CN";
    case 90:  return "TR";
    case 91:  return "IN";
    case 92:  return "PK";
    case 95:  return "MM";
    case 852: return "HK";
    case 853: return "MO";
    case 855: return "KH";
    case 856: return "LA";
    case 880: return "BD";
    case 886: return "TW";
    case 966: return "SA";
    case 971: return "AE";
    default:  return nullptr;
  }
}

static const char *RankIdToName(uint32_t id) {
  switch (id) {
    case 0:  return "Unranked";
    case 1:  return "Warrior III";
    case 2:  return "Warrior II";
    case 3:  return "Warrior I";
    case 4:  return "Elite III";
    case 5:  return "Elite II";
    case 6:  return "Elite I";
    case 7:  return "Master IV";
    case 8:  return "Master III";
    case 9:  return "Master II";
    case 10: return "Master I";
    case 11: return "Grandmaster V";
    case 12: return "Grandmaster IV";
    case 13: return "Grandmaster III";
    case 14: return "Grandmaster II";
    case 15: return "Grandmaster I";
    case 16: return "Epic V";
    case 17: return "Epic IV";
    case 18: return "Epic III";
    case 19: return "Epic II";
    case 20: return "Epic I";
    case 21: return "Legend V";
    case 22: return "Legend IV";
    case 23: return "Legend III";
    case 24: return "Legend II";
    case 25: return "Legend I";
    case 26: return "Mythic";
    case 27: return "Mythical Honor";
    case 28: return "Mythical Glory";
    case 29: return "Mythical Immortal";
    default: return nullptr;
  }
}

static void RenderRoomTab() {
  static int resolveAttempts = 0;
  if (g_il2cppReady.load(std::memory_order_acquire) &&
      Originals::SystemData_LogicGetRoomData == nullptr &&
      resolveAttempts < 3) {
    ++resolveAttempts;
    AssignRoomInfoMethods();
  }

  ImGui::TextDisabled("Read-only room cache from SystemData.LogicGetRoomData.");
  ImGui::Spacing();

  if (!g_il2cppReady.load(std::memory_order_acquire)) {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                       "IL2CPP belum siap. Tunggu game selesai load.");
    return;
  }
  if (Originals::SystemData_LogicGetRoomData == nullptr) {
    ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1.0f),
                       "Method room data belum resolved.");
    return;
  }

  std::vector<RoomScoutEntry> roster;
  if (!ReadRoomRoster(roster)) {
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
                       "Belum ada room data. Tekan Find Match dulu.");
    return;
  }

  int realCount = 0;
  int botCount = 0;
  int ghostCount = 0;
  for (const RoomScoutEntry &entry : roster) {
    if (entry.isFakeRole) {
      ++ghostCount;
    } else if (entry.isRobot) {
      ++botCount;
    } else {
      ++realCount;
    }
  }

  ImGui::Text("Total: %zu slot | Real: %d | Bot: %d | Ghost: %d",
              roster.size(), realCount, botCount, ghostCount);
  ImGui::Spacing();

  if (ImGui::BeginTable("##RoomScoutTable", 7,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                            ImGuiTableFlags_SizingStretchProp |
                            ImGuiTableFlags_Resizable)) {
    ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 28.0f);
    ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Nickname", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Country", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 56.0f);
    ImGui::TableSetupColumn("Battles", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < roster.size(); ++i) {
      const RoomScoutEntry &entry = roster[i];
      ImGui::TableNextRow();

      ImGui::TableSetColumnIndex(0);
      ImGui::Text("%zu", i + 1);

      ImGui::TableSetColumnIndex(1);
      if (entry.isFakeRole) {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "GHOST");
      } else if (entry.isRobot) {
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 0.4f, 1.0f), "BOT");
      } else {
        ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "REAL");
      }

      ImGui::TableSetColumnIndex(2);
      if (entry.isNewPlayer) {
        ImGui::Text("%s [NEW]", entry.name.c_str());
      } else {
        ImGui::Text("%s", entry.name.c_str());
      }

      ImGui::TableSetColumnIndex(3);
      const char *rankName = RankIdToName(entry.magicRank);
      if (rankName != nullptr) {
        ImGui::Text("%s / %u cup", rankName, entry.magicCup);
      } else {
        ImGui::Text("Rank #%u / %u cup", entry.magicRank, entry.magicCup);
      }

      ImGui::TableSetColumnIndex(4);
      const char *country = CountryCodeToName(entry.country);
      if (country != nullptr) {
        ImGui::Text("%s", country);
      } else {
        ImGui::Text("%u", entry.country);
      }

      ImGui::TableSetColumnIndex(5);
      ImGui::Text("%u", entry.roleLevel);

      ImGui::TableSetColumnIndex(6);
      ImGui::Text("%u", entry.battleCnt);
    }
    ImGui::EndTable();
  }
}

static void RenderModMenu() {
  ImGui::SetNextWindowSize(ImVec2(880, 540), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowBgAlpha(0.97f);
  if (ImGui::Begin("    IMGUI-MCGG    ", nullptr)) {
    // Vertical sidebar layout: left tab column + right content panel.
    const float sidebarW = 160.0f;

    ImGui::BeginChild("##Sidebar", ImVec2(sidebarW, 0), true);
    for (int i = 0; i < kTabCount; ++i) {
      bool selected = (g_currentTab == i);
      if (ImGui::Selectable(kTabNames[i], selected, 0, ImVec2(0, 32))) {
        g_currentTab = i;
      }
    }
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("##Content", ImVec2(0, 0), true);
    if (g_currentTab < 0 || g_currentTab >= kTabCount)
      g_currentTab = 0;
    ImGui::Text("%s", kTabNames[g_currentTab]);
    ImGui::Separator();
    ImGui::Spacing();
    switch (g_currentTab) {
      case 0:
        RenderInfoTab();
        break;
      case 1:
        RenderRoomTab();
        break;
      default:
        ImGui::TextDisabled("(empty - implement %s features here)",
                            kTabNames[g_currentTab]);
        break;
    }
    ImGui::EndChild();
  }
  ImGui::End();
}

// ── eglSwapBuffers detour ────────────────────────────────────────────────
namespace Hooks {
EGLBoolean EglSwapBuffers(EGLDisplay dpy, EGLSurface surface) {
  // One-time ImGui init on first frame.
  if (!g_imguiInitialized.load(std::memory_order_relaxed)) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.IniFilename = nullptr; // no persistent layout file
    ApplyImGuiTheme();
    ImGui_ImplAndroid_Init(nullptr);
    ImGui_ImplOpenGL3_Init("#version 300 es");
    g_imguiInitialized.store(true, std::memory_order_relaxed);
    LOGI("[ImGui] initialized");
  }

  // Track GL framebuffer size for menu layout.
  EGLint w = 0, h = 0;
  if (eglQuerySurface(dpy, surface, EGL_WIDTH, &w) &&
      eglQuerySurface(dpy, surface, EGL_HEIGHT, &h)) {
    g_glWidth.store(w, std::memory_order_relaxed);
    g_glHeight.store(h, std::memory_order_relaxed);
    ImGuiIO &io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);
  }

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplAndroid_NewFrame();
  ImGui::NewFrame();

  KeyStatus st = g_keyStatus.load();
  if (st != KeyStatus::Valid) {
    RenderLoginDialog();
  } else {
    RenderModMenu();
  }

  ImGui::Render();
  // Reset GL state ImGui's backend expects (Unity leaves it dirty).
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glViewport(0, 0, w, h);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                      GL_ONE_MINUS_SRC_ALPHA);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

  return Originals::EglSwapBuffers(dpy, surface);
}
} // namespace Hooks

// ── Setup ────────────────────────────────────────────────────────────────
static void SetupThread() {
  LOGI("[SETUP] booting");

  // Resolve + hook eglSwapBuffers. Retry until the GL library is loaded
  // (the constructor may run before libEGL has been mapped).
  void *swapAddr = nullptr;
  for (int attempt = 0; attempt < 30; ++attempt) {
    swapAddr = DobbySymbolResolver(nullptr, "eglSwapBuffers");
    if (swapAddr) break;
    sleep(1);
  }
  if (!swapAddr) {
    LOGE("[SETUP] eglSwapBuffers not resolved");
    return;
  }
  if (DobbyHook(swapAddr, reinterpret_cast<void *>(Hooks::EglSwapBuffers),
                reinterpret_cast<void **>(&Originals::EglSwapBuffers)) != 0) {
    LOGE("[SETUP] DobbyHook(eglSwapBuffers) failed");
    return;
  }
  LOGI("[SETUP] eglSwapBuffers hooked @ %p", swapAddr);

  void *il2cppHandle = nullptr;
  for (int attempt = 0; attempt < 30; ++attempt) {
    il2cppHandle = xdl_open("liblogic.so", XDL_DEFAULT);
    if (il2cppHandle == nullptr)
      il2cppHandle = xdl_open("libil2cpp.so", XDL_DEFAULT);
    if (il2cppHandle != nullptr)
      break;
    sleep(1);
  }
  if (il2cppHandle == nullptr) {
    LOGW("[SETUP] IL2CPP library not resolved; Room tab unavailable");
    return;
  }
  if (InitIl2CppApi(il2cppHandle)) {
    AssignRoomInfoMethods();
  } else {
    LOGW("[SETUP] IL2CPP API init failed; Room tab unavailable");
  }
}

// ── Loader entry ─────────────────────────────────────────────────────────
__attribute__((constructor)) __attribute__((visibility("default")))
static void InitLibrary() {
  if (!IsTargetProcess()) {
    LOGW("[INIT] not target process — skipping");
    return;
  }
  LOGI("[INIT] target process detected — spawning setup thread");
  std::thread(SetupThread).detach();
}

// JNI_OnLoad just forwards to the original libmain.so (this is a proxy
// library — Unity calls libmain.so and we live in its place).
extern "C" __attribute__((visibility("default")))
jint JNI_OnLoad(JavaVM *vm, void * /*reserved*/) {
  LOGI("[JNI] JNI_OnLoad invoked");
  return JNI_VERSION_1_6;
}
