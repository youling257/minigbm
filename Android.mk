# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

ifeq ($(strip $(BOARD_USES_MINIGBM)), true)

MINIGBM_GRALLOC_MK := $(call my-dir)/Android.gralloc.mk
LOCAL_PATH := $(call my-dir)

MINIGBM_SRC := \
	amdgpu.c \
	dri.c \
	drv.c \
	drv_array_helpers.c \
	drv_helpers.c \
	dumb_driver.c \
	i915.c \
	mediatek.c \
	msm.c \
	rockchip.c \
	vc4.c \
	virtgpu.c \
	virtgpu_cross_domain.c \
	virtgpu_virgl.c

MINIGBM_CPPFLAGS := -std=c++14

MINIGBM_CFLAGS_32 += \
	-DDRI_DRIVER_DIR=/vendor/lib/dri

MINIGBM_CFLAGS_64 += \
	-DDRI_DRIVER_DIR=/vendor/lib64/dri

MINIGBM_CFLAGS := \
	-DDRV_AMDGPU \
	-DDRV_I915 \
	-D_GNU_SOURCE=1 -D_FILE_OFFSET_BITS=64 \
	-Wall -Wsign-compare -Wpointer-arith \
	-Wcast-qual -Wcast-align \
	-Wno-unused-parameter -Wno-typedef-redefinition \
	-Wno-missing-field-initializers \
	-Wno-invalid-offsetof

MINIGBM_INCLUDES += external/libdrm/amdgpu \
		    external/mesa/include

LOCAL_SHARED_LIBRARIES += libdrm_amdgpu libdrm_intel

include $(CLEAR_VARS)

SUBDIRS := cros_gralloc

LOCAL_SHARED_LIBRARIES := \
	libcutils \
	libdrm

LOCAL_SRC_FILES := $(MINIGBM_SRC)
LOCAL_C_INCLUDES := $(MINIGBM_INCLUDES)

include $(MINIGBM_GRALLOC_MK)

LOCAL_CFLAGS := $(MINIGBM_CFLAGS)
LOCAL_CPPFLAGS := $(MINIGBM_CPPFLAGS)
LOCAL_CFLAGS_32 := $(MINIGBM_CFLAGS_32)
LOCAL_CFLAGS_64 :=$(MINIGBM_CFLAGS_64)

LOCAL_MODULE := gralloc.minigbm
LOCAL_MODULE_TAGS := optional
# The preferred path for vendor HALs is /vendor/lib/hw
LOCAL_PROPRIETARY_MODULE := true
LOCAL_MODULE_RELATIVE_PATH := hw
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_MODULE_SUFFIX := $(TARGET_SHLIB_SUFFIX)
ifeq ($(shell test $(PLATFORM_SDK_VERSION) -ge 27; echo $$?), 0)
LOCAL_SHARED_LIBRARIES += libnativewindow
LOCAL_STATIC_LIBRARIES += libarect
LOCAL_HEADER_LIBRARIES += libnativebase_headers libsystem_headers libhardware_headers
endif
LOCAL_SHARED_LIBRARIES += libsync liblog
include $(BUILD_SHARED_LIBRARY)


include $(CLEAR_VARS)
LOCAL_SHARED_LIBRARIES := libcutils
LOCAL_STATIC_LIBRARIES := libdrm

LOCAL_SRC_FILES += $(MINIGBM_SRC) gbm.c gbm_helpers.c

LOCAL_EXPORT_C_INCLUDE_DIRS := $(LOCAL_PATH)
LOCAL_CFLAGS := $(MINIGBM_CFLAGS)
LOCAL_CPPFLAGS := $(MINIGBM_CPPFLAGS)

LOCAL_MODULE := libminigbm
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)

endif
