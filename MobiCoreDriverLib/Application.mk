# =============================================================================
#
# Main build file defining the project modules and their global variables.
#
# =============================================================================

# Don't remove this - mandatory
APP_PROJECT_PATH := $(abspath $(call my-dir))

APP_STL := stlport_static

# Application wide Cflags
GLOBAL_INCLUDES := \
    $(COMP_PATH_MobiCoreDriverMod)/Public \
    $(COMP_PATH_TlSdk)/Public/MobiCore/inc \
    $(COMP_PATH_MobiCore)/inc \
    $(COMP_PATH_TlCm)/Public \
    $(COMP_PATH_TlCm)/Public/TlCm

# Show all warnings
APP_CFLAGS += -Wall

LOG_WRAPPER := $(COMP_PATH_Logwrapper)

APP_PLATFORM := android-9

# Position Independent Executable
APP_PIE := true
