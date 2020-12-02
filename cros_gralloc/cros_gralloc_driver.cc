/*
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "cros_gralloc_driver.h"

#include <cstdlib>
#include <fcntl.h>
#include <sys/mman.h>
#include <syscall.h>
#include <xf86drm.h>

#include "../drv_priv.h"
#include "../helpers.h"
#include "../util.h"

// Constants taken from pipe_loader_drm.c in Mesa

#define DRM_NUM_NODES 63

// DRM Render nodes start at 128
#define DRM_RENDER_NODE_START 128

// DRM Card nodes start at 0
#define DRM_CARD_NODE_START 0

int memfd_create_wrapper(const char *name, unsigned int flags)
{
	int fd;

#if defined(HAVE_MEMFD_CREATE)
	fd = memfd_create(name, flags);
#elif defined(__NR_memfd_create)
	fd = syscall(__NR_memfd_create, name, flags);
#else
	drv_log("Failed to create memfd '%s': memfd_create not available.", name);
	return -1;
#endif

	if (fd == -1)
		drv_log("Failed to create memfd '%s': %s.\n", name, strerror(errno));

	return fd;
}

cros_gralloc_driver::cros_gralloc_driver() : drv_(nullptr)
{
}

cros_gralloc_driver::~cros_gralloc_driver()
{
	buffers_.clear();
	handles_.clear();

	if (drv_) {
		int fd = drv_get_fd(drv_);
		drv_destroy(drv_);
		drv_ = nullptr;
		close(fd);
	}
}

static struct driver *init_try_node(int idx, char const *str)
{
	int fd;
	char *node;
	struct driver *drv;

	if (asprintf(&node, str, DRM_DIR_NAME, idx) < 0)
		return NULL;

	fd = open(node, O_RDWR, 0);
	free(node);

	if (fd < 0)
		return NULL;

	drv = drv_create(fd);
	if (!drv)
		close(fd);

	return drv;
}

int cros_gralloc_driver::get_fd() const {
	return drv_get_fd(drv_);
}

int32_t cros_gralloc_driver::init()
{
	/*
	 * Create a driver from render nodes first, then try card
	 * nodes.
	 *
	 * TODO(gsingh): Enable render nodes on udl/evdi.
	 */

	char const *render_nodes_fmt = "%s/renderD%d";
	char const *card_nodes_fmt = "%s/card%d";
	uint32_t num_nodes = DRM_NUM_NODES;
	uint32_t min_render_node = DRM_RENDER_NODE_START;
	uint32_t max_render_node = (min_render_node + num_nodes);
	uint32_t min_card_node = DRM_CARD_NODE_START;
	uint32_t max_card_node = (min_card_node + num_nodes);

	// Try render nodes...
	for (uint32_t i = min_render_node; i < max_render_node; i++) {
		drv_ = init_try_node(i, render_nodes_fmt);
		if (drv_)
			return 0;
	}

	// Try card nodes... for vkms mostly.
	for (uint32_t i = min_card_node; i < max_card_node; i++) {
		drv_ = init_try_node(i, card_nodes_fmt);
		if (drv_)
			return 0;
	}

	return -ENODEV;
}

int cros_gralloc_driver::init_master()
{
	int fd = open(DRM_DIR_NAME "/card0", O_RDWR, 0);
	if (fd >= 0) {
		drv_ = drv_create(fd);
		if (drv_)
			return 0;
	}

	return -ENODEV;
}

bool cros_gralloc_driver::is_supported(const struct cros_gralloc_buffer_descriptor *descriptor)
{
	struct combination *combo;
	uint32_t resolved_format;
	resolved_format = drv_resolve_format(drv_, descriptor->drm_format, descriptor->use_flags);
	combo = drv_get_combination(drv_, resolved_format, descriptor->use_flags);
	return (combo != nullptr);
}

int32_t cros_gralloc_driver::allocate(const struct cros_gralloc_buffer_descriptor *descriptor,
				      buffer_handle_t *out_handle)
{
	uint32_t id;
	uint64_t mod;
	size_t num_planes;
	uint32_t resolved_format;
	uint32_t bytes_per_pixel;
	uint64_t use_flags;

	struct bo *bo;
	struct cros_gralloc_handle *hnd;

	resolved_format = drv_resolve_format(drv_, descriptor->drm_format, descriptor->use_flags);
	use_flags = descriptor->use_flags;

	/*
	 * This unmask is a backup in the case DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED is resolved
	 * to non-YUV formats.
	 */
	if (descriptor->drm_format == DRM_FORMAT_FLEX_IMPLEMENTATION_DEFINED &&
	    (resolved_format == DRM_FORMAT_XBGR8888 || resolved_format == DRM_FORMAT_ABGR8888)) {
		use_flags &= ~BO_USE_HW_VIDEO_ENCODER;
	}

	bo = drv_bo_create(drv_, descriptor->width, descriptor->height, resolved_format, use_flags);
	if (!bo) {
		drv_log("Failed to create bo.\n");
		return -ENOMEM;
	}

	/*
	 * If there is a desire for more than one kernel buffer, this can be
	 * removed once the ArcCodec and Wayland service have the ability to
	 * send more than one fd. GL/Vulkan drivers may also have to modified.
	 */
	if (drv_num_buffers_per_bo(bo) != 1) {
		drv_bo_destroy(bo);
		drv_log("Can only support one buffer per bo.\n");
		return -EINVAL;
	}

	hnd = new cros_gralloc_handle();
	num_planes = drv_bo_get_num_planes(bo);

	hnd->base.version = sizeof(hnd->base);
	hnd->base.numFds = num_planes;
	hnd->base.numInts = handle_data_size - num_planes;

	for (size_t plane = 0; plane < num_planes; plane++) {
		hnd->fds[plane] = drv_bo_get_plane_fd(bo, plane);
		hnd->strides[plane] = drv_bo_get_plane_stride(bo, plane);
		hnd->offsets[plane] = drv_bo_get_plane_offset(bo, plane);
		mod = drv_bo_get_plane_format_modifier(bo, plane);
		hnd->format_modifiers[2 * plane] = static_cast<uint32_t>(mod >> 32);
		hnd->format_modifiers[2 * plane + 1] = static_cast<uint32_t>(mod);
	}

	hnd->width = drv_bo_get_width(bo);
	hnd->height = drv_bo_get_height(bo);
	hnd->format = drv_bo_get_format(bo);
	hnd->tiling = bo->meta.tiling;
	hnd->format_modifier = drv_bo_get_plane_format_modifier(bo, 0);
	hnd->use_flags = descriptor->use_flags;
	bytes_per_pixel = drv_bytes_per_pixel_from_format(hnd->format, 0);
	hnd->pixel_stride = DIV_ROUND_UP(hnd->strides[0], bytes_per_pixel);
	hnd->magic = cros_gralloc_magic;
	hnd->droid_format = descriptor->droid_format;
	hnd->usage = descriptor->producer_usage;

	id = drv_bo_get_plane_handle(bo, 0).u32;
	auto buffer = new cros_gralloc_buffer(id, bo, hnd);

	std::lock_guard<std::mutex> lock(mutex_);
	buffers_.emplace(id, buffer);
	handles_.emplace(hnd, std::make_pair(buffer, 1));
	*out_handle = &hnd->base;
	return 0;
}

int32_t cros_gralloc_driver::retain(buffer_handle_t handle)
{
	uint32_t id;
	std::lock_guard<std::mutex> lock(mutex_);

	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (buffer) {
		handles_[hnd].second++;
		buffer->increase_refcount();
		return 0;
	}

	if (drmPrimeFDToHandle(drv_get_fd(drv_), hnd->fds[0], &id)) {
		drv_log("drmPrimeFDToHandle failed.\n");
		return -errno;
	}

	if (buffers_.count(id)) {
		buffer = buffers_[id];
		buffer->increase_refcount();
	} else {
		struct bo *bo;
		struct drv_import_fd_data data;
		data.format = hnd->format;
		data.tiling = hnd->tiling;

		data.width = hnd->width;
		data.height = hnd->height;
		data.use_flags = hnd->use_flags;

		memcpy(data.fds, hnd->fds, sizeof(data.fds));
		memcpy(data.strides, hnd->strides, sizeof(data.strides));
		memcpy(data.offsets, hnd->offsets, sizeof(data.offsets));
		for (uint32_t plane = 0; plane < DRV_MAX_PLANES; plane++) {
			data.format_modifiers[plane] =
			    static_cast<uint64_t>(hnd->format_modifiers[2 * plane]) << 32;
			data.format_modifiers[plane] |= hnd->format_modifiers[2 * plane + 1];
		}

		bo = drv_bo_import(drv_, &data);
		if (!bo)
			return -EFAULT;

		id = drv_bo_get_plane_handle(bo, 0).u32;

		buffer = new cros_gralloc_buffer(id, bo, nullptr);
		buffers_.emplace(id, buffer);
	}

	handles_.emplace(hnd, std::make_pair(buffer, 1));
	return 0;
}

int32_t cros_gralloc_driver::release(buffer_handle_t handle)
{
	std::lock_guard<std::mutex> lock(mutex_);

	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (!buffer) {
		drv_log("Invalid Reference.\n");
		return -EINVAL;
	}

	if (!--handles_[hnd].second)
		handles_.erase(hnd);

	if (buffer->decrease_refcount() == 0) {
		buffers_.erase(buffer->get_id());
		delete buffer;
	}

	return 0;
}

int32_t cros_gralloc_driver::lock(buffer_handle_t handle, int32_t acquire_fence,
				  bool close_acquire_fence, const struct rectangle *rect,
				  uint32_t map_flags, uint8_t *addr[DRV_MAX_PLANES])
{
	int32_t ret = cros_gralloc_sync_wait(acquire_fence, close_acquire_fence);
	if (ret)
		return ret;

	std::lock_guard<std::mutex> lock(mutex_);
	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (!buffer) {
		drv_log("Invalid Reference.\n");
		return -EINVAL;
	}

	return buffer->lock(rect, map_flags, addr);
}

int32_t cros_gralloc_driver::unlock(buffer_handle_t handle, int32_t *release_fence)
{
	std::lock_guard<std::mutex> lock(mutex_);

	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (!buffer) {
		drv_log("Invalid Reference.\n");
		return -EINVAL;
	}

	/*
	 * From the ANativeWindow::dequeueBuffer documentation:
	 *
	 * "A value of -1 indicates that the caller may access the buffer immediately without
	 * waiting on a fence."
	 */
	*release_fence = -1;
	return buffer->unlock();
}

int32_t cros_gralloc_driver::get_backing_store(buffer_handle_t handle, uint64_t *out_store)
{
	std::lock_guard<std::mutex> lock(mutex_);

	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (!buffer) {
		drv_log("Invalid Reference.\n");
		return -EINVAL;
	}

	*out_store = static_cast<uint64_t>(buffer->get_id());
	return 0;
}

int32_t cros_gralloc_driver::resource_info(buffer_handle_t handle, uint32_t strides[DRV_MAX_PLANES],
					   uint32_t offsets[DRV_MAX_PLANES],
					   uint64_t *format_modifier)
{
	std::lock_guard<std::mutex> lock(mutex_);

	auto hnd = cros_gralloc_convert_handle(handle);
	if (!hnd) {
		drv_log("Invalid handle.\n");
		return -EINVAL;
	}

	auto buffer = get_buffer(hnd);
	if (!buffer) {
		drv_log("Invalid Reference.\n");
		return -EINVAL;
	}

	return buffer->resource_info(strides, offsets, format_modifier);
}

cros_gralloc_buffer *cros_gralloc_driver::get_buffer(cros_gralloc_handle_t hnd)
{
	/* Assumes driver mutex is held. */
	if (handles_.count(hnd))
		return handles_[hnd].first;

	return nullptr;
}
