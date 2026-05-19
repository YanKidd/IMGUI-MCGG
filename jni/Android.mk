LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)
LOCAL_MODULE := dobby
LOCAL_SRC_FILES := DOBBY/lib/$(TARGET_ARCH_ABI)/libdobby.a
include $(PREBUILT_STATIC_LIBRARY)

include $(CLEAR_VARS)

LOCAL_MODULE := main

LOCAL_CFLAGS := \
    -O3 \
    -w \
    -Wno-error \
    -DNDEBUG \
    -g0 \
    -fvisibility=hidden \
    -fvisibility-inlines-hidden \
    -ffunction-sections \
    -fdata-sections \
    -fomit-frame-pointer \

LOCAL_CPPFLAGS := \
    $(LOCAL_CFLAGS) \
    -std=c++20 \
    -fno-rtti \
    -fno-exceptions \

LOCAL_LDFLAGS := \
    -Wl,--gc-sections \
    -Wl,--strip-all \
    -Wl,--exclude-libs,ALL \
    -Wl,-z,noexecstack \
    -Wl,-z,relro \
    -Wl,-z,now \

LOCAL_LDLIBS := \
    -llog \
    -landroid \
    -lEGL \
    -lGLESv3 \

LOCAL_C_INCLUDES := \
    $(LOCAL_PATH)/XDL/include \
    $(LOCAL_PATH)/DOBBY/include \
    $(LOCAL_PATH)/IMGUI/include \
    $(LOCAL_PATH)/IMGUI/include/backends \
    $(LOCAL_PATH)/IMGUI/include/fonts \
    $(LOCAL_PATH)/UNITY/include \

LOCAL_SRC_FILES := \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/XDL/src/*.c)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/XDL/src/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/IMGUI/src/*.c)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/IMGUI/src/*.cpp)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/IMGUI/src/backends/*.c)) \
    $(patsubst $(LOCAL_PATH)/%,%,$(wildcard $(LOCAL_PATH)/IMGUI/src/backends/*.cpp)) \
    Main.cpp \

LOCAL_STATIC_LIBRARIES := \
    dobby \

include $(BUILD_SHARED_LIBRARY)
