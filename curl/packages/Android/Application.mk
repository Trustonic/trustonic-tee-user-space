# =============================================================================
#
# Main build file defining the project modules and their global variables.
#
# =============================================================================

# Don't remove this - mandatory
APP_PROJECT_PATH := $(call my-dir)

# Don't optimize for better debugging
APP_OPTIM := debug

# Position Independent Executable
APP_PIE := true
