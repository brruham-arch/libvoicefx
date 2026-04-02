LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := voicefx
LOCAL_SRC_FILES := voicefx.c
LOCAL_CFLAGS    := -O2 -ffast-math -fPIC -Wall
LOCAL_LDLIBS    := -lm -llog -ldl -lstdc++
LOCAL_ARM_MODE  := arm

include $(BUILD_SHARED_LIBRARY)
