# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

BOARD_USES_MINIGBM := true
ifeq ($(strip $(BOARD_USES_MINIGBM)), true)

MINIGBM_GRALLOC_MK := $(call my-dir)/Android.gralloc.mk
LOCAL_PATH := $(call my-dir)
intel_drivers := i915 i965

MINIGBM_SRC := \
	amdgpu.c \
	dri.c \
	drv.c \
	dumb_driver.c \
	exynos.c \
	helpers_array.c \
	helpers.c \
	i915.c \
	mediatek.c \
	meson.c \
	msm.c \
	radeon.c \
	rockchip.c \
	vc4.c \
	virtio_gpu.c

MINIGBM_CPPFLAGS := -std=c++14
MINIGBM_CFLAGS := \
	-D_GNU_SOURCE=1 -D_FILE_OFFSET_BITS=64 \
	-Wall -Wsign-compare -Wpointer-arith \
	-Wcast-align \
	-Wno-unused-parameter -Wno-typedef-redefinition

ifneq ($(filter $(intel_drivers), $(BOARD_GPU_DRIVERS)),)
MINIGBM_CPPFLAGS += -DDRV_I915
MINIGBM_CFLAGS += -DDRV_I915
LOCAL_SHARED_LIBRARIES += libdrm_intel
endif

ifneq ($(filter meson, $(BOARD_GPU_DRIVERS)),)
MINIGBM_CPPFLAGS += -DDRV_MESON
MINIGBM_CFLAGS += -DDRV_MESON
endif

ifneq ($(filter radeonsi, $(BOARD_GPU_DRIVERS)),)
MINIGBM_CPPFLAGS += -DDRV_AMDGPU
MINIGBM_CFLAGS += -DDRV_AMDGPU
MINIGBM_INCLUDES += external/libdrm/amdgpu \
		    external/mesa/include
LOCAL_SHARED_LIBRARIES += libdrm_amdgpu
endif

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
