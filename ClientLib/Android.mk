LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE := libMcClient
LOCAL_MODULE_TAGS := eng

LOCAL_CFLAGS := -fvisibility=hidden
LOCAL_CFLAGS += -DTBASE_API_LEVEL=5
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -std=c++11
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/include/GP
ifeq ($(APP_PROJECT_PATH),)
LOCAL_SHARED_LIBRARIES += liblog
else
LOCAL_LDLIBS := -llog
endif

LOCAL_SRC_FILES := \
	src/common_client.cpp \
	src/driver_client.cpp \
	src/mc_client_api.cpp \
	src/tee_client_api.cpp

LOCAL_EXPORT_CFLAGS := -DLOG_ANDROID
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/include/GP
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_EXPORT_C_INCLUDES)

include $(BUILD_SHARED_LIBRARY)

# Static version of the client lib for recovery

include $(CLEAR_VARS)

LOCAL_MODULE := libMcClient_static
LOCAL_MODULE_TAGS := eng

LOCAL_CFLAGS := -fvisibility=hidden
LOCAL_CFLAGS += -DTBASE_API_LEVEL=5
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -std=c++11
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/include/GP
ifeq ($(APP_PROJECT_PATH),)
LOCAL_SHARED_LIBRARIES += liblog
endif

LOCAL_SRC_FILES := \
	src/common_client.cpp \
	src/driver_client.cpp \
	src/mc_client_api.cpp \
	src/tee_client_api.cpp

LOCAL_EXPORT_CFLAGS := -DLOG_ANDROID
LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/include/GP
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_EXPORT_C_INCLUDES)

include $(BUILD_STATIC_LIBRARY)
