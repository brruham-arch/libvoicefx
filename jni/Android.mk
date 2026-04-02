LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := voicefx
LOCAL_SRC_FILES := voicefx.cpp
LOCAL_CFLAGS    := -O2 -ffast-math -fPIC -Wall
LOCAL_CPPFLAGS  := -std=c++17
LOCAL_LDLIBS    := -lm -llog -ldl -static-libstdc++
LOCAL_ARM_MODE  := arm

include $(BUILD_SHARED_LIBRARY)
