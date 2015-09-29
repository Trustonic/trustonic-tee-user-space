# =============================================================================
#
# MobiCore Android build components
#
# =============================================================================

LOCAL_PATH := $(call my-dir)

# Registry Shared Library
# =============================================================================
include $(CLEAR_VARS)

LOCAL_MODULE := libMcRegistry
LOCAL_MODULE_TAGS := eng

LOCAL_CFLAGS += -DLOG_TAG=\"McRegistry\"
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES := libMcClient
ifeq ($(APP_PROJECT_PATH),)
LOCAL_SHARED_LIBRARIES += liblog
else
# Local build
LOCAL_LDLIBS := -llog
endif

LOCAL_SRC_FILES := \
	src/Connection.cpp \
	src/Registry.cpp

LOCAL_EXPORT_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_EXPORT_C_INCLUDES)

include $(BUILD_SHARED_LIBRARY)

# Daemon Application
# =============================================================================
include $(CLEAR_VARS)

LOCAL_MODULE := mcDriverDaemon
LOCAL_MODULE_TAGS := eng
LOCAL_CFLAGS += -DLOG_TAG=\"McDaemon\"
LOCAL_CFLAGS += -DTBASE_API_LEVEL=5
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -std=c++11
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_SHARED_LIBRARIES := libMcClient
ifeq ($(APP_PROJECT_PATH),)
ifneq ($(wildcard external/stlport/libstlport.mk),)
# Up to Lollipop
LOCAL_C_INCLUDES += external/stlport/stlport
include external/stlport/libstlport.mk
LOCAL_SHARED_LIBRARIES += libstlport
endif # !M
LOCAL_SHARED_LIBRARIES += liblog
else # !NDK
# Local build
LOCAL_LDLIBS := -llog
endif # NDK

LOCAL_SRC_FILES := \
	src/Connection.cpp \
	src/CThread.cpp \
	src/MobiCoreDriverDaemon.cpp \
	src/SecureWorld.cpp \
	src/FSD2.cpp \
	src/Server.cpp \
	src/PrivateRegistry.cpp

include $(BUILD_EXECUTABLE)

# Static version of the daemon for recovery

include $(CLEAR_VARS)

LOCAL_MODULE := mcDriverDaemon_static
LOCAL_MODULE_TAGS := eng
LOCAL_CFLAGS += -DLOG_TAG=\"McDaemon\"
LOCAL_CFLAGS += -DTBASE_API_LEVEL=5
LOCAL_CFLAGS += -Wall -Wextra
LOCAL_CFLAGS += -std=c++11
LOCAL_CFLAGS += -DLOG_ANDROID
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include
LOCAL_STATIC_LIBRARIES := libMcClient_static
ifeq ($(APP_PROJECT_PATH),)
ifneq ($(wildcard external/stlport/libstlport.mk),)
# Up to Lollipop
LOCAL_C_INCLUDES += external/stlport/stlport
include external/stlport/libstlport.mk
LOCAL_SHARED_LIBRARIES += libstlport
endif # !M
LOCAL_SHARED_LIBRARIES += liblog
else # !NDK
# Local build
LOCAL_LDLIBS := -llog
endif # NDK

LOCAL_SRC_FILES := \
	src/Connection.cpp \
	src/CThread.cpp \
	src/MobiCoreDriverDaemon.cpp \
	src/SecureWorld.cpp \
	src/FSD2.cpp \
	src/Server.cpp \
	src/PrivateRegistry.cpp

include $(BUILD_EXECUTABLE)

# adding the root folder to the search path appears to make absolute paths
# work for import-module - lets see how long this works and what surprises
# future developers get from this.
$(call import-add-path,/)
$(call import-module,$(COMP_PATH_MobiCoreClientLib_module))
