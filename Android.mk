LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE	:= libwrap
LOCAL_SRC_FILES	:= wrap/wrap-util.c wrap/wrap-syscall.c
LOCAL_C_INCLUDES := \
	$(LOCAL_PATH)/includes \
	$(LOCAL_PATH)/util
#LOCAL_SHARED_LIBRARIES := liblog
LOCAL_LDLIBS := -llog -lc -ldl
include $(BUILD_SHARED_LIBRARY)

