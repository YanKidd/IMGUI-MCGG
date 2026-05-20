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
#include <cstdio>
#include <cstring>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "xdl.h"
#include "dobby.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_android.h"
#include "imgui_impl_opengl3.h"

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
static const char *const kTabNames[] = {"Brutal Ops", "Config"};
static constexpr int kTabCount = sizeof(kTabNames) / sizeof(kTabNames[0]);

// ── Hook originals ───────────────────────────────────────────────────────
namespace Originals {
EGLBoolean (*EglSwapBuffers)(EGLDisplay, EGLSurface) = nullptr;
} // namespace Originals

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
    ImGui::Text("%s", kTabNames[g_currentTab]);
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("(empty — implement %s features here)",
                        kTabNames[g_currentTab]);
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
