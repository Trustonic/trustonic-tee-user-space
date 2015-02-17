# =============================================================================
#
# Module: libCommon.a - classes shared by various modules
#
# =============================================================================

LOCAL_PATH	:= $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE	:= Common

# Add new source files here
#LOCAL_SRC_FILES +=\
#	CMutex.cpp\
#	Connection.cpp\
#    NetlinkConnection.cpp\
#	CSemaphore.cpp\
#	CThread.cpp

# Header files required by components including this module
LOCAL_EXPORT_C_INCLUDES	+= $(LOCAL_PATH)

# Import logwrapper
#include $(COMP_PATH_Logwrapper)/Android.mk

include $(BUILD_STATIC_LIBRARY)
