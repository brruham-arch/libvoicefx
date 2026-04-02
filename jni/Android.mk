LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := voicefx
LOCAL_SRC_FILES := voicefx.cpp
LOCAL_CPPFLAGS  := -std=c++17 -fPIC
LOCAL_CFLAGS    := -Wall
LOCAL_LDLIBS    := -lm -llog -ldl -static-libstdc++ -Wl,--export-all-symbols

include $(BUILD_SHARED_LIBRARY)
