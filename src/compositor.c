#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>

#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <gbm.h>
#include <EGL/egl.h>
#define  EGL_EGLEXT_PROTOTYPES
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#define  GL_GLEXT_PROTOTYPES
#include <GLES2/gl2ext.h>
#include <flutter_embedder.h>

#include <flutter-pi.h>
#include <collection.h>
#include <compositor.h>
#include <cursor.h>

struct view_cb_data {
	int64_t view_id;
	platform_view_mount_cb mount;
	platform_view_unmount_cb unmount;
	platform_view_update_view_cb update_view;
	platform_view_present_cb present;
	void *userdata;

	bool was_present_last_frame;
	int last_zpos;
	FlutterSize last_size;
	FlutterPoint last_offset;
	int last_num_mutations;
	FlutterPlatformViewMutation last_mutations[16];
};

/*
struct plane_data {
	int type;
	const struct drm_plane *plane;
	bool is_reserved;
	int zpos;
};
*/

struct compositor compositor = {
	.drmdev = NULL,
	.cbs = CPSET_INITIALIZER(CPSET_DEFAULT_MAX_SIZE),
	.should_create_window_surface_backing_store = true,
	.has_applied_modeset = false,
	.stale_rendertargets = CPSET_INITIALIZER(CPSET_DEFAULT_MAX_SIZE),
	.do_blocking_atomic_commits = false
};

static struct view_cb_data *get_cbs_for_view_id_locked(int64_t view_id) {
	struct view_cb_data *data;
	
	for_each_pointer_in_cpset(&compositor.cbs, data) {
		if (data->view_id == view_id) {
			return data;
		}
	}

	return NULL;
}

static struct view_cb_data *get_cbs_for_view_id(int64_t view_id) {
	struct view_cb_data *data;
	
	cpset_lock(&compositor.cbs);
	data = get_cbs_for_view_id_locked(view_id);
	cpset_unlock(&compositor.cbs);
	
	return data;
}

/**
 * @brief Destroy all the rendertargets in the stale rendertarget cache.
 */
static int destroy_stale_rendertargets(void) {
	struct rendertarget *target;

	cpset_lock(&compositor.stale_rendertargets);

	for_each_pointer_in_cpset(&compositor.stale_rendertargets, target) {
		target->destroy(target);
		target = NULL;
	}

	cpset_unlock(&compositor.stale_rendertargets);
}

static void destroy_gbm_bo(
	struct gbm_bo *bo,
	void *userdata
) {
	struct drm_fb *fb = userdata;

	if (fb && fb->fb_id)
		drmModeRmFB(flutterpi.drm.drmdev->fd, fb->fb_id);
	
	free(fb);
}

/**
 * @brief Get a DRM FB id for this GBM BO, so we can display it.
 */
static uint32_t gbm_bo_get_drm_fb_id(struct gbm_bo *bo) {
	uint32_t width, height, format, strides[4] = {0}, handles[4] = {0}, offsets[4] = {0}, flags = 0;
	int ok = -1;

	// if the buffer object already has some userdata associated with it,
	//   it's the framebuffer we allocated.
	struct drm_fb *fb = gbm_bo_get_user_data(bo);
	if (fb) return fb->fb_id;

	// if there's no framebuffer for the bo, we need to create one.
	fb = calloc(1, sizeof(struct drm_fb));
	fb->bo = bo;

	width = gbm_bo_get_width(bo);
	height = gbm_bo_get_height(bo);
	format = gbm_bo_get_format(bo);

	uint64_t modifiers[4] = {0};
	modifiers[0] = gbm_bo_get_modifier(bo);
	const int num_planes = gbm_bo_get_plane_count(bo);

	for (int i = 0; i < num_planes; i++) {
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		handles[i] = gbm_bo_get_handle(bo).u32;
		offsets[i] = gbm_bo_get_offset(bo, i);
		modifiers[i] = modifiers[0];
	}

	if (modifiers[0]) {
		flags = DRM_MODE_FB_MODIFIERS;
	}

	ok = drmModeAddFB2WithModifiers(flutterpi.drm.drmdev->fd, width, height, format, handles, strides, offsets, modifiers, &fb->fb_id, flags);

	if (ok) {
		if (flags)
			fprintf(stderr, "drm_fb_get_from_bo: modifiers failed!\n");
		
		uint32_t handles_src[4] = {gbm_bo_get_handle(bo).u32,0,0,0};
		memcpy(handles, handles_src, 16);
		uint32_t strides_src[4] = {gbm_bo_get_stride(bo),0,0,0};
		memcpy(strides, strides_src, 16);
		memset(offsets, 0, 16);

		ok = drmModeAddFB2(flutterpi.drm.drmdev->fd, width, height, format, handles, strides, offsets, &fb->fb_id, 0);
	}

	if (ok) {
		fprintf(stderr, "drm_fb_get_from_bo: failed to create fb: %s\n", strerror(errno));
		free(fb);
		return 0;
	}

	gbm_bo_set_user_data(bo, fb, destroy_gbm_bo);

	return fb->fb_id;
}


/**
 * @brief Create a GL renderbuffer that is backed by a DRM buffer-object and registered as a DRM framebuffer
 */
static int create_drm_rbo(
	size_t width,
	size_t height,
	struct drm_rbo *out
) {
	struct drm_rbo fbo;
	EGLint egl_error;
	GLenum gl_error;
	int ok;

	eglGetError();
	glGetError();

  const EGLint lint[] = {
		EGL_WIDTH, width,
		EGL_HEIGHT, height,
		EGL_DRM_BUFFER_FORMAT_MESA, EGL_DRM_BUFFER_FORMAT_ARGB32_MESA,
		EGL_DRM_BUFFER_USE_MESA, EGL_DRM_BUFFER_USE_SCANOUT_MESA,
		EGL_NONE
	};
	fbo.egl_image = flutterpi.egl.createDRMImageMESA(flutterpi.egl.display, lint);
	if ((egl_error = eglGetError()) != EGL_SUCCESS) {
		fprintf(stderr, "[compositor] error creating DRM EGL Image for flutter backing store, eglCreateDRMImageMESA: %ld\n", egl_error);
		return EINVAL;
	}

	flutterpi.egl.exportDRMImageMESA(flutterpi.egl.display, fbo.egl_image, NULL, &fbo.gem_handle, &fbo.gem_stride);
	if ((egl_error = eglGetError()) != EGL_SUCCESS) {
		fprintf(stderr, "[compositor] error getting handle & stride for DRM EGL Image, eglExportDRMImageMESA: %d\n", egl_error);
		return EINVAL;
	}

	glGenRenderbuffers(1, &fbo.gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error generating renderbuffers for flutter backing store, glGenRenderbuffers: %ld\n", gl_error);
		return EINVAL;
	}

	glBindRenderbuffer(GL_RENDERBUFFER, fbo.gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error binding renderbuffer, glBindRenderbuffer: %d\n", gl_error);
		return EINVAL;
	}

	flutterpi.gl.EGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, fbo.egl_image);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error binding DRM EGL Image to renderbuffer, glEGLImageTargetRenderbufferStorageOES: %ld\n", gl_error);
		return EINVAL;
	}

	/*
	glGenFramebuffers(1, &fbo.gl_fbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error generating FBOs for flutter backing store, glGenFramebuffers: %d\n", gl_error);
		return EINVAL;
	}

	glBindFramebuffer(GL_FRAMEBUFFER, fbo.gl_fbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error binding FBO for attaching the renderbuffer, glBindFramebuffer: %d\n", gl_error);
		return EINVAL;
	}

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, fbo.gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error attaching renderbuffer to FBO, glFramebufferRenderbuffer: %d\n", gl_error);
		return EINVAL;
	}

	GLenum fb_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);

	*/

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	
	// glBindFramebuffer(GL_FRAMEBUFFER, 0);

	ok = drmModeAddFB2(
		flutterpi.drm.drmdev->fd,
		width,
		height,
		DRM_FORMAT_ARGB8888,
		(const uint32_t*) &(uint32_t[4]) {
			fbo.gem_handle,
			0,
			0,
			0
		},
		(const uint32_t*) &(uint32_t[4]) {
			fbo.gem_stride, 0, 0, 0
		},
		(const uint32_t*) &(uint32_t[4]) {
			0, 0, 0, 0
		},
		&fbo.drm_fb_id,
		0
	);
	if (ok == -1) {
		perror("[compositor] Could not make DRM fb from EGL Image, drmModeAddFB2");
		return errno;
	}

	*out = fbo;

	return 0;
}

/**
 * @brief Set the color attachment of a GL FBO to this DRM RBO.
 */
static int attach_drm_rbo_to_fbo(
	GLuint fbo_id,
	struct drm_rbo *rbo
) {
	EGLint egl_error;
	GLenum gl_error;

	eglGetError();
	glGetError();

	glBindFramebuffer(GL_FRAMEBUFFER, fbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error binding FBO for attaching the renderbuffer, glBindFramebuffer: %d\n", gl_error);
		return EINVAL;
	}

	glBindRenderbuffer(GL_RENDERBUFFER, rbo->gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error binding renderbuffer, glBindRenderbuffer: %d\n", gl_error);
		return EINVAL;
	}

	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo->gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error attaching renderbuffer to FBO, glFramebufferRenderbuffer: %d\n", gl_error);
		return EINVAL;
	}

	return 0;
}

/**
 * @brief Destroy this GL renderbuffer, and the associated DRM buffer-object and DRM framebuffer
 */
static void destroy_drm_rbo(
	struct drm_rbo *rbo
) {
	EGLint egl_error;
	GLenum gl_error;
	int ok;

	eglGetError();
	glGetError();

	glDeleteRenderbuffers(1, &rbo->gl_rbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error destroying OpenGL RBO, glDeleteRenderbuffers: 0x%08X\n", gl_error);
	}

	ok = drmModeRmFB(flutterpi.drm.drmdev->fd, rbo->drm_fb_id);
	if (ok < 0) {
		fprintf(stderr, "[compositor] error removing DRM FB, drmModeRmFB: %s\n", strerror(errno));
	}

	eglDestroyImage(flutterpi.egl.display, rbo->egl_image);
	if (egl_error = eglGetError(), egl_error != EGL_SUCCESS) {
		fprintf(stderr, "[compositor] error destroying EGL image, eglDestroyImage: 0x%08X\n", egl_error);
	}
}

static void rendertarget_gbm_destroy(struct rendertarget *target) {
	free(target);
}

static int rendertarget_gbm_present(
	struct rendertarget *target,
	struct drmdev_atomic_req *atomic_req,
	uint32_t drm_plane_id,
	int offset_x,
	int offset_y,
	int width,
	int height,
	int zpos
) {
	struct rendertarget_gbm *gbm_target;
	struct gbm_bo *next_front_bo;
	uint32_t next_front_fb_id;
	bool supported;
	int ok;

	gbm_target = &target->gbm;

	next_front_bo = gbm_surface_lock_front_buffer(gbm_target->gbm_surface);
	next_front_fb_id = gbm_bo_get_drm_fb_id(next_front_bo);

	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "FB_ID", next_front_fb_id);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "CRTC_ID", target->compositor->drmdev->selected_crtc->crtc->crtc_id);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "SRC_X", 0);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "SRC_Y", 0);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "SRC_W", ((uint16_t) flutterpi.display.width) << 16);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "SRC_H", ((uint16_t) flutterpi.display.height) << 16);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "CRTC_X", 0);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "CRTC_Y", 0);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "CRTC_W", flutterpi.display.width);
	drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "CRTC_H", flutterpi.display.height);

	ok = drmdev_plane_supports_setting_rotation_value(atomic_req->drmdev, drm_plane_id, DRM_MODE_ROTATE_0, &supported);
	if (ok != 0) return ok;

	if (supported) {
		drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "rotation", DRM_MODE_ROTATE_0);
	} else {
		static bool printed = false;

		if (!printed) {
			fprintf(stderr,
					"[compositor] GPU does not support reflecting the screen in Y-direction.\n"
					"             This is required for rendering into hardware overlay planes though.\n"
					"             Any UI that is drawn in overlay planes will look upside down.\n"
			);
			printed = true;
		}
	}
	
	ok = drmdev_plane_supports_setting_zpos_value(atomic_req->drmdev, drm_plane_id, zpos, &supported);
	if (ok != 0) return ok;

	if (supported) {
		drmdev_atomic_req_put_plane_property(atomic_req, drm_plane_id, "zpos", zpos);
	} else {
		static bool printed = false;

		if (!printed) { 
			fprintf(stderr,
					"[compositor] GPU does not supported the desired HW plane order.\n"
					"             Some UI layers may be invisible.\n"
			);
			printed = true;
		}
	}

	// TODO: move this to the page flip handler.
	// We can only be sure the buffer can be released when the buffer swap
	// ocurred.
	if (gbm_target->current_front_bo != NULL) {
		gbm_surface_release_buffer(gbm_target->gbm_surface, gbm_target->current_front_bo);
	}
	gbm_target->current_front_bo = (struct gbm_bo *) next_front_bo;

	return 0;
}

static int rendertarget_gbm_present_legacy(
	struct rendertarget *target,
	struct drmdev *drmdev,
	uint32_t drm_plane_id,
	int offset_x,
	int offset_y,
	int width,
	int height,
	int zpos,
	bool set_mode
) {
	struct rendertarget_gbm *gbm_target;
	struct gbm_bo *next_front_bo;
	uint32_t next_front_fb_id;
	bool supported, is_primary;
	int ok;

	gbm_target = &target->gbm;

	is_primary = drmdev_plane_get_type(drmdev, drm_plane_id) == DRM_PLANE_TYPE_PRIMARY;

	next_front_bo = gbm_surface_lock_front_buffer(gbm_target->gbm_surface);
	next_front_fb_id = gbm_bo_get_drm_fb_id(next_front_bo);

	if (is_primary) {
		if (set_mode) {
			drmdev_legacy_set_mode_and_fb(
				drmdev,
				next_front_fb_id
			);
		} else {
			drmdev_legacy_primary_plane_pageflip(
				drmdev,
				next_front_fb_id,
				NULL
			);
		}
	} else {
		drmdev_legacy_overlay_plane_pageflip(
			drmdev,
			drm_plane_id,
			next_front_fb_id,
			0,
			0,
			flutterpi.display.width,
			flutterpi.display.height,
			0,
			0,
			((uint16_t) flutterpi.display.width) << 16,
			((uint16_t) flutterpi.display.height) << 16
		);
	}
	
	// TODO: move this to the page flip handler.
	// We can only be sure the buffer can be released when the buffer swap
	// ocurred.
	if (gbm_target->current_front_bo != NULL) {
		gbm_surface_release_buffer(gbm_target->gbm_surface, gbm_target->current_front_bo);
	}
	gbm_target->current_front_bo = (struct gbm_bo *) next_front_bo;

	return 0;
}

/**
 * @brief Create a type of rendertarget that is backed by a GBM Surface, used for rendering into the DRM primary plane.
 * 
 * @param[out] out A pointer to the pointer of the created rendertarget.
 * @param[in] compositor The compositor which this rendertarget should be associated with.
 * 
 * @see rendertarget_gbm
 */
static int rendertarget_gbm_new(
	struct rendertarget **out,
	struct compositor *compositor
) {
	struct rendertarget *target;
	int ok;

	target = calloc(1, sizeof *target);
	if (target == NULL) {
		*out = NULL;
		return ENOMEM;
	}

	*target = (struct rendertarget) {
		.is_gbm = true,
		.compositor = compositor,
		.gbm = {
			.gbm_surface = flutterpi.gbm.surface,
			.current_front_bo = NULL
		},
		.gl_fbo_id = 0,
		.destroy = rendertarget_gbm_destroy,
		.present = rendertarget_gbm_present,
		.present_legacy = rendertarget_gbm_present_legacy
	};

	*out = target;

	return 0;
}

static void rendertarget_nogbm_destroy(struct rendertarget *target) {
	glDeleteFramebuffers(1, &target->nogbm.gl_fbo_id);
	destroy_drm_rbo(target->nogbm.rbos + 1);
	destroy_drm_rbo(target->nogbm.rbos + 0);
	free(target);
}

static int rendertarget_nogbm_present(
	struct rendertarget *target,
	struct drmdev_atomic_req *req,
	uint32_t drm_plane_id,
	int offset_x,
	int offset_y,
	int width,
	int height,
	int zpos
) {
	struct rendertarget_nogbm *nogbm_target;
	bool supported;
	int ok;

	nogbm_target = &target->nogbm;

	nogbm_target->current_front_rbo ^= 1;
	ok = attach_drm_rbo_to_fbo(nogbm_target->gl_fbo_id, nogbm_target->rbos + nogbm_target->current_front_rbo);
	if (ok != 0) return ok;

	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "FB_ID", nogbm_target->rbos[nogbm_target->current_front_rbo ^ 1].drm_fb_id);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "CRTC_ID", target->compositor->drmdev->selected_crtc->crtc->crtc_id);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "SRC_X", 0);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "SRC_Y", 0);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "SRC_W", ((uint16_t) flutterpi.display.width) << 16);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "SRC_H", ((uint16_t) flutterpi.display.height) << 16);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "CRTC_X", 0);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "CRTC_Y", 0);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "CRTC_W", flutterpi.display.width);
	drmdev_atomic_req_put_plane_property(req, drm_plane_id, "CRTC_H", flutterpi.display.height);
	
	ok = drmdev_plane_supports_setting_rotation_value(req->drmdev, drm_plane_id, DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y, &supported);
	if (ok != 0) return ok;
	
	if (supported) {
		drmdev_atomic_req_put_plane_property(req, drm_plane_id, "rotation", DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y);
	} else {
		static bool printed = false;

		if (!printed) {
			fprintf(stderr,
					"[compositor] GPU does not support reflecting the screen in Y-direction.\n"
					"             This is required for rendering into hardware overlay planes though.\n"
					"             Any UI that is drawn in overlay planes will look upside down.\n"
			);
			printed = true;
		}
	}

	ok = drmdev_plane_supports_setting_zpos_value(req->drmdev, drm_plane_id, zpos, &supported);
	if (ok != 0) return ok;
	
	if (supported) {
		drmdev_atomic_req_put_plane_property(req, drm_plane_id, "zpos", zpos);
	} else {
		static bool printed = false;

		if (!printed) { 
			fprintf(stderr,
					"[compositor] GPU does not supported the desired HW plane order.\n"
					"             Some UI layers may be invisible.\n"
			);
			printed = true;
		}
	}

	return 0;
}

static int rendertarget_nogbm_present_legacy(
	struct rendertarget *target,
	struct drmdev *drmdev,
	uint32_t drm_plane_id,
	int offset_x,
	int offset_y,
	int width,
	int height,
	int zpos,
	bool set_mode
) {
	struct rendertarget_nogbm *nogbm_target;
	uint32_t fb_id;
	bool supported, is_primary;
	int ok;

	nogbm_target = &target->nogbm;

	is_primary = drmdev_plane_get_type(drmdev, drm_plane_id) == DRM_PLANE_TYPE_PRIMARY;

	nogbm_target->current_front_rbo ^= 1;
	ok = attach_drm_rbo_to_fbo(nogbm_target->gl_fbo_id, nogbm_target->rbos + nogbm_target->current_front_rbo);
	if (ok != 0) return ok;

	fb_id = nogbm_target->rbos[nogbm_target->current_front_rbo ^ 1].drm_fb_id;

	if (is_primary) {
		if (set_mode) {
			drmdev_legacy_set_mode_and_fb(
				drmdev,
				fb_id
			);
		} else {
			drmdev_legacy_primary_plane_pageflip(
				drmdev,
				fb_id,
				NULL
			);
		}
	} else {
		drmdev_legacy_overlay_plane_pageflip(
			drmdev,
			drm_plane_id,
			fb_id,
			0,
			0,
			flutterpi.display.width,
			flutterpi.display.height,
			0,
			0,
			((uint16_t) flutterpi.display.width) << 16,
			((uint16_t) flutterpi.display.height) << 16
		);
	}
	
	ok = drmdev_plane_supports_setting_rotation_value(drmdev, drm_plane_id, DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y, &supported);
	if (ok != 0) return ok;
	
	if (supported) {
		drmdev_legacy_set_plane_property(drmdev, drm_plane_id, "rotation", DRM_MODE_ROTATE_0 | DRM_MODE_REFLECT_Y);
	} else {
		static bool printed = false;

		if (!printed) {
			fprintf(stderr,
					"[compositor] GPU does not support reflecting the screen in Y-direction.\n"
					"             This is required for rendering into hardware overlay planes though.\n"
					"             Any UI that is drawn in overlay planes will look upside down.\n"
			);
			printed = true;
		}
	}

	ok = drmdev_plane_supports_setting_zpos_value(drmdev, drm_plane_id, zpos, &supported);
	if (ok != 0) return ok;
	
	if (supported) {
		drmdev_legacy_set_plane_property(drmdev, drm_plane_id, "zpos", zpos);
	} else {
		static bool printed = false;

		if (!printed) { 
			fprintf(stderr,
					"[compositor] GPU does not supported the desired HW plane order.\n"
					"             Some UI layers may be invisible.\n"
			);
			printed = true;
		}
	}

	return 0;
}

/**
 * @brief Create a type of rendertarget that is not backed by a GBM-Surface, used for rendering into DRM overlay planes.
 * 
 * @param[out] out A pointer to the pointer of the created rendertarget.
 * @param[in] compositor The compositor which this rendertarget should be associated with.
 * 
 * @see rendertarget_nogbm
 */
static int rendertarget_nogbm_new(
	struct rendertarget **out,
	struct compositor *compositor
) {
	struct rendertarget *target;
	EGLint egl_error;
	GLenum gl_error;
	int ok;

	target = calloc(1, sizeof *target);
	if (target == NULL) {
		return ENOMEM;
	}

	target->is_gbm = false;
	target->compositor = compositor;
	target->destroy = rendertarget_nogbm_destroy;
	target->present = rendertarget_nogbm_present;
	target->present_legacy = rendertarget_nogbm_present_legacy;

	eglGetError();
	glGetError();

	glGenFramebuffers(1, &target->nogbm.gl_fbo_id);
	if (gl_error = glGetError()) {
		fprintf(stderr, "[compositor] error generating FBOs for flutter backing store, glGenFramebuffers: %d\n", gl_error);
		ok = EINVAL;
		goto fail_free_target;
	}

	ok = create_drm_rbo(
		flutterpi.display.width,
		flutterpi.display.height,
		target->nogbm.rbos + 0
	);
	if (ok != 0) {
		goto fail_delete_fb;
	}

	ok = create_drm_rbo(
		flutterpi.display.width,
		flutterpi.display.height,
		target->nogbm.rbos + 1
	);
	if (ok != 0) {
		goto fail_destroy_drm_rbo_0;
	}

	ok = attach_drm_rbo_to_fbo(target->nogbm.gl_fbo_id, target->nogbm.rbos + target->nogbm.current_front_rbo);
	if (ok != 0) {
		goto fail_destroy_drm_rbo_1;
	}

	target->gl_fbo_id = target->nogbm.gl_fbo_id;

	*out = target;
	return 0;


	fail_destroy_drm_rbo_1:
	destroy_drm_rbo(target->nogbm.rbos + 1);

	fail_destroy_drm_rbo_0:
	destroy_drm_rbo(target->nogbm.rbos + 0);

	fail_delete_fb:
	glDeleteFramebuffers(1, &target->nogbm.gl_fbo_id);

	fail_free_target:
	free(target);
	*out = NULL;
	return ok;
}

/**
 * @brief Called by flutter when the OpenGL FBO of a backing store should be destroyed.
 * Called on an internal engine-managed thread. This is actually called after the engine
 * calls @ref on_collect_backing_store.
 * 
 * @param[in] userdata The pointer to the struct flutterpi_backing_store that should be destroyed.
 */
static void on_destroy_backing_store_gl_fb(void *userdata) {
	struct flutterpi_backing_store *store;
	struct compositor *compositor;
	
	store = userdata;
	compositor = store->target->compositor;

	cpset_put_(&compositor->stale_rendertargets, store->target);

	if (store->should_free_on_next_destroy) {
		free(store);
	} else {
		store->should_free_on_next_destroy = true;
	}
}

/**
 * @brief A callback invoked by the engine to release the backing store. The embedder may
 * collect any resources associated with the backing store. Invoked on an internal
 * engine-managed thread. This is actually called before the engine calls @ref on_destroy_backing_store_gl_fb.
 * 
 * @param[in] backing_store The backing store to be collected.
 * @param[in] userdata A pointer to the flutterpi compositor.
 */
static bool on_collect_backing_store(
	const FlutterBackingStore *backing_store,
	void *userdata
) {
	struct flutterpi_backing_store *store;
	struct compositor *compositor;
	
	store = backing_store->user_data;
	compositor = store->target->compositor;

	cpset_put_(&compositor->stale_rendertargets, store->target);

	if (store->should_free_on_next_destroy) {
		free(store);
	} else {
		store->should_free_on_next_destroy = true;
	}

	return true;
}

/**
 * @brief A callback invoked by the engine to obtain a FlutterBackingStore for a specific FlutterLayer.
 * Called on an internal engine-managed thread.
 * 
 * @param[in] config The dimensions of the backing store to be created, post transform.
 * @param[out] backing_store_out The created backing store.
 * @param[in] userdata A pointer to the flutterpi compositor.
 */
static bool on_create_backing_store(
	const FlutterBackingStoreConfig *config,
	FlutterBackingStore *backing_store_out,
	void *userdata
) {
	struct flutterpi_backing_store *store;
	struct rendertarget *target;
	struct compositor *compositor;
	int ok;

	compositor = userdata;

	store = calloc(1, sizeof *store);
	if (store == NULL) {
		return false;
	}

	// first, try to find a stale GBM rendertarget.
	cpset_lock(&compositor->stale_rendertargets);
	for_each_pointer_in_cpset(&compositor->stale_rendertargets, target) break;
	if (target != NULL) {
		cpset_remove_locked(&compositor->stale_rendertargets, target);
	}
	cpset_unlock(&compositor->stale_rendertargets);

	// if we didn't find one, check if we should create one. If not,
	// try to find a stale No-GBM rendertarget. If there is none,
	// create one.
	if (target == NULL) {
		if (compositor->should_create_window_surface_backing_store) {
			// We create 1 "backing store" that is rendering to the DRM_PLANE_PRIMARY
			// plane. That backing store isn't really a backing store at all, it's
			// FBO id is 0, so it's actually rendering to the window surface.

			ok = rendertarget_gbm_new(
				&target,
				compositor
			);

			if (ok != 0) {
				free(store);
				return false;
			}

			compositor->should_create_window_surface_backing_store = false;
		} else {
			ok = rendertarget_nogbm_new(
				&target,
				compositor
			);

			if (ok != 0) {
				free(store);
				return false;
			}
		}
	}

	store->target = target;
	FlutterBackingStore backing_store = {
		.struct_size = backing_store_out->struct_size,
		.user_data = store,
		.type = kFlutterBackingStoreTypeOpenGL,
	};
	backing_store.open_gl.type = kFlutterOpenGLTargetTypeFramebuffer;
	backing_store.open_gl.framebuffer.target = GL_BGRA8_EXT;
	backing_store.open_gl.framebuffer.name = target->gl_fbo_id;
	backing_store.open_gl.framebuffer.destruction_callback = on_destroy_backing_store_gl_fb;
	backing_store.open_gl.framebuffer.user_data = store;
	store->flutter_backing_store = backing_store;

	memcpy(backing_store_out, &store->flutter_backing_store, sizeof(FlutterBackingStore));

	return true;
}

struct simulated_page_flip_event_data {
	unsigned int sec;
	unsigned int usec;
};

extern void on_pageflip_event(
	int fd,
	unsigned int frame,
	unsigned int sec,
	unsigned int usec,
	void *userdata
);

static int execute_simulate_page_flip_event(void *userdata) {
	struct simulated_page_flip_event_data *data;

	data = userdata;

	on_pageflip_event(flutterpi.drm.drmdev->fd, 0, data->sec, data->usec, NULL);

	free(data);

	return 0;
}

/// PRESENT FUNCS
static bool on_present_layers(
	const FlutterLayer **layers,
	size_t layers_count,
	void *userdata
) {
	struct drmdev_atomic_req *req;
	struct view_cb_data *cb_data;
	struct pointer_set planes;
	struct compositor *compositor;
	struct drm_plane *plane;
	struct drmdev *drmdev;
	uint32_t req_flags;
	void *planes_storage[32] = {0};
	bool legacy_rendertarget_set_mode = false;
	bool schedule_fake_page_flip_event;
	bool use_atomic_modesetting;
	int ok;

	compositor = userdata;
	drmdev = compositor->drmdev;
	schedule_fake_page_flip_event = compositor->do_blocking_atomic_commits;
	use_atomic_modesetting = drmdev->supports_atomic_modesetting;

	if (use_atomic_modesetting) {
		drmdev_new_atomic_req(compositor->drmdev, &req);
	} else {
		planes = PSET_INITIALIZER_STATIC(planes_storage, 32);
		for_each_plane_in_drmdev(drmdev, plane) {
			if (plane->plane->possible_crtcs & drmdev->selected_crtc->bitmask) {
				pset_put(&planes, plane);
			}
		}
	}

	cpset_lock(&compositor->cbs);

	eglMakeCurrent(flutterpi.egl.display, flutterpi.egl.surface, flutterpi.egl.surface, flutterpi.egl.root_context);
	eglSwapBuffers(flutterpi.egl.display, flutterpi.egl.surface);

	req_flags =  0 /* DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_NONBLOCK*/;
	if (compositor->has_applied_modeset == false) {
		if (use_atomic_modesetting) {
			ok = drmdev_atomic_req_put_modeset_props(req, &req_flags);
			if (ok != 0) return false;
		} else {
			legacy_rendertarget_set_mode = true;
			schedule_fake_page_flip_event = true;
		}

		int64_t max_zpos = 0;

		if (use_atomic_modesetting) {
			for_each_unreserved_plane_in_atomic_req(req, plane) {
				if (plane->type == DRM_PLANE_TYPE_CURSOR) {
					// make sure the cursor is in front of everything
					int64_t max_zpos;
					bool supported;

					ok = drmdev_plane_get_max_zpos_value(req->drmdev, plane->plane->plane_id, &max_zpos);
					if (ok != 0) {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_get_max_zpos_value: %s\n", strerror(ok));
						continue;
					}
					
					ok = drmdev_plane_supports_setting_zpos_value(req->drmdev, plane->plane->plane_id, max_zpos, &supported);
					if (ok != 0) {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_supports_setting_zpos_value: %s\n", strerror(ok));
						continue;
					}

					if (supported) {
						drmdev_atomic_req_put_plane_property(req, plane->plane->plane_id, "zpos", max_zpos);
					} else {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_supports_setting_zpos_value: %s\n", strerror(ok));
						continue;
					}
				}
			}
		} else {
			for_each_pointer_in_pset(&planes, plane) {
				if (plane->type == DRM_PLANE_TYPE_CURSOR) {
					// make sure the cursor is in front of everything
					int64_t max_zpos;
					bool supported;

					ok = drmdev_plane_get_max_zpos_value(drmdev, plane->plane->plane_id, &max_zpos);
					if (ok != 0) {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_get_max_zpos_value: %s\n", strerror(ok));
						continue;
					}
					
					ok = drmdev_plane_supports_setting_zpos_value(drmdev, plane->plane->plane_id, max_zpos, &supported);
					if (ok != 0) {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_supports_setting_zpos_value: %s\n", strerror(ok));
						continue;
					}

					if (supported) {
						drmdev_legacy_set_plane_property(drmdev, plane->plane->plane_id, "zpos", max_zpos);
					} else {
						printf("[compositor] Could not move cursor to front. Mouse cursor may be invisible. drmdev_plane_supports_setting_zpos_value: %s\n", strerror(ok));
						continue;
					}
				}
			}
		}
		
		compositor->has_applied_modeset = true;
	}
	
	// first, the state machine phase.
	// go through the layers, update
	// all platform views accordingly.
	// unmount, update, mount. in that order
	{
		void *mounted_views_storage[layers_count];
		memset(mounted_views_storage, 0, layers_count * sizeof(void*));
		struct pointer_set mounted_views = PSET_INITIALIZER_STATIC(mounted_views_storage, layers_count);

		void *unmounted_views_storage[layers_count];
		memset(unmounted_views_storage, 0, layers_count * sizeof(void*));
		struct pointer_set unmounted_views = PSET_INITIALIZER_STATIC(unmounted_views_storage, layers_count);

		void *updated_views_storage[layers_count];
		memset(updated_views_storage, 0, layers_count * sizeof(void*));
		struct pointer_set updated_views = PSET_INITIALIZER_STATIC(updated_views_storage, layers_count);
	
		for_each_pointer_in_cpset(&compositor->cbs, cb_data) {
			const FlutterLayer *layer;
			bool is_present = false;
			int zpos;

			for (int i = 0; i < layers_count; i++) {
				if (layers[i]->type == kFlutterLayerContentTypePlatformView &&
					layers[i]->platform_view->identifier == cb_data->view_id) {
					is_present = true;
					layer = layers[i];
					zpos = i;
					break;
				}
			}

			if (!is_present && cb_data->was_present_last_frame) {
				pset_put(&unmounted_views, cb_data);
			} else if (is_present && cb_data->was_present_last_frame) {
				if (cb_data->update_view != NULL) {
					bool did_update_view = false;

					did_update_view = did_update_view || (zpos != cb_data->last_zpos);
					did_update_view = did_update_view || memcmp(&cb_data->last_size, &layer->size, sizeof(FlutterSize));
					did_update_view = did_update_view || memcmp(&cb_data->last_offset, &layer->offset, sizeof(FlutterPoint));
					did_update_view = did_update_view || (cb_data->last_num_mutations != layer->platform_view->mutations_count);
					for (int i = 0; (i < layer->platform_view->mutations_count) && !did_update_view; i++) {
						did_update_view = did_update_view || memcmp(cb_data->last_mutations + i, layer->platform_view->mutations[i], sizeof(FlutterPlatformViewMutation));
					}

					if (did_update_view) {
						pset_put(&updated_views, cb_data);
					}
				}
			} else if (is_present && !cb_data->was_present_last_frame) {
				pset_put(&mounted_views, cb_data);
			}
		}

		for_each_pointer_in_pset(&unmounted_views, cb_data) {
			if (cb_data->unmount != NULL) {
				ok = cb_data->unmount(
					cb_data->view_id,
					req,
					cb_data->userdata
				);
				if (ok != 0) {
					fprintf(stderr, "[compositor] Could not unmount platform view. unmount: %s\n", strerror(ok));
				}
			}
		}

		for_each_pointer_in_pset(&updated_views, cb_data) {
			const FlutterLayer *layer;
			int zpos;

			for (int i = 0; i < layers_count; i++) {
				if (layers[i]->type == kFlutterLayerContentTypePlatformView &&
					layers[i]->platform_view->identifier == cb_data->view_id) {
					layer = layers[i];
					zpos = i;
					break;
				}
			}

			ok = cb_data->update_view(
				cb_data->view_id,
				req,
				layer->platform_view->mutations,
				layer->platform_view->mutations_count,
				(int) round(layer->offset.x),
				(int) round(layer->offset.y),
				(int) round(layer->size.width),
				(int) round(layer->size.height),
				zpos,
				cb_data->userdata
			);
			if (ok != 0) {
				fprintf(stderr, "[compositor] Could not update platform view. update_view: %s\n", strerror(ok));
			}

			cb_data->last_zpos = zpos;
			cb_data->last_size = layer->size;
			cb_data->last_offset = layer->offset;
			cb_data->last_num_mutations = layer->platform_view->mutations_count;
			for (int i = 0; i < layer->platform_view->mutations_count; i++) {
				memcpy(cb_data->last_mutations + i, layer->platform_view->mutations[i], sizeof(FlutterPlatformViewMutation));
			}
		}

		for_each_pointer_in_pset(&mounted_views, cb_data) {
			const FlutterLayer *layer;
			int zpos;

			for (int i = 0; i < layers_count; i++) {
				if (layers[i]->type == kFlutterLayerContentTypePlatformView &&
					layers[i]->platform_view->identifier == cb_data->view_id) {
					layer = layers[i];
					zpos = i;
					break;
				}
			}

			if (cb_data->mount != NULL) {
				ok = cb_data->mount(
					layer->platform_view->identifier,
					req,
					layer->platform_view->mutations,
					layer->platform_view->mutations_count,
					(int) round(layer->offset.x),
					(int) round(layer->offset.y),
					(int) round(layer->size.width),
					(int) round(layer->size.height),
					zpos,
					cb_data->userdata
				);
				if (ok != 0) {
					fprintf(stderr, "[compositor] Could not mount platform view. %s\n", strerror(ok));
				}
			}

			cb_data->last_zpos = zpos;
			cb_data->last_size = layer->size;
			cb_data->last_offset = layer->offset;
			cb_data->last_num_mutations = layer->platform_view->mutations_count;
			for (int i = 0; i < layer->platform_view->mutations_count; i++) {
				memcpy(cb_data->last_mutations + i, layer->platform_view->mutations[i], sizeof(FlutterPlatformViewMutation));
			}
		}
	}
	
	int64_t min_zpos;
	if (use_atomic_modesetting) {
		for_each_unreserved_plane_in_atomic_req(req, plane) {
			if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
				ok = drmdev_plane_get_min_zpos_value(req->drmdev, plane->plane->plane_id, &min_zpos);
				if (ok != 0) {
					min_zpos = 0;
				}
				break;
			}
		}
	} else {
		for_each_pointer_in_pset(&planes, plane) {
			if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
				ok = drmdev_plane_get_min_zpos_value(drmdev, plane->plane->plane_id, &min_zpos);
				if (ok != 0) {
					min_zpos = 0;
				}
				break;
			}
		}
	}

	for (int i = 0; i < layers_count; i++) {
		if (layers[i]->type == kFlutterLayerContentTypeBackingStore) {
			if (use_atomic_modesetting) {
				for_each_unreserved_plane_in_atomic_req(req, plane) {
					// choose a plane which has an "intrinsic" zpos that matches
					// the zpos we want the plane to have.
					// (Since planes are buggy and we can't rely on the zpos we explicitly
					// configure the plane to have to be actually applied to the hardware.
					// In short, assigning a different value to the zpos property won't always
					// take effect.)
					if ((i == 0) && (plane->type == DRM_PLANE_TYPE_PRIMARY)) {
						ok = drmdev_atomic_req_reserve_plane(req, plane);
						break;
					} else if ((i != 0) && (plane->type == DRM_PLANE_TYPE_OVERLAY)) {
						ok = drmdev_atomic_req_reserve_plane(req, plane);
						break;
					}
				}
			} else {
				for_each_pointer_in_pset(&planes, plane) {
					if ((i == 0) && (plane->type == DRM_PLANE_TYPE_PRIMARY)) {
						break;
					} else if ((i != 0) && (plane->type == DRM_PLANE_TYPE_OVERLAY)) {
						break;
					}
				}
				if (plane != NULL) {
					pset_remove(&planes, plane);
				}
			}
			if (plane == NULL) {
				fprintf(stderr, "[compositor] Could not find a free primary/overlay DRM plane for presenting the backing store. drmdev_atomic_req_reserve_plane: %s\n", strerror(ok));
				continue;
			}

			struct flutterpi_backing_store *store = layers[i]->backing_store->user_data;
			struct rendertarget *target = store->target;

			if (use_atomic_modesetting) {
				ok = target->present(
					target,
					req,
					plane->plane->plane_id,
					0,
					0,
					compositor->drmdev->selected_mode->hdisplay,
					compositor->drmdev->selected_mode->vdisplay,
					i + min_zpos
				);
				if (ok != 0) {
					fprintf(stderr, "[compositor] Could not present backing store. rendertarget->present: %s\n", strerror(ok));
				}
			} else {
				ok = target->present_legacy(
					target,
					drmdev,
					plane->plane->plane_id,
					0,
					0,
					compositor->drmdev->selected_mode->hdisplay,
					compositor->drmdev->selected_mode->vdisplay,
					i + min_zpos,
					legacy_rendertarget_set_mode && (plane->type == DRM_PLANE_TYPE_PRIMARY)
				);
			}
		} else if (layers[i]->type == kFlutterLayerContentTypePlatformView) {
			cb_data = get_cbs_for_view_id_locked(layers[i]->platform_view->identifier);

			if ((cb_data != NULL) && (cb_data->present != NULL)) {
				ok = cb_data->present(
					cb_data->view_id,
					req,
					layers[i]->platform_view->mutations,
					layers[i]->platform_view->mutations_count,
					(int) round(layers[i]->offset.x),
					(int) round(layers[i]->offset.y),
					(int) round(layers[i]->size.width),
					(int) round(layers[i]->size.height),
					i + min_zpos,
					cb_data->userdata
				);
				if (ok != 0) {
					fprintf(stderr, "[compositor] Could not present platform view. platform_view->present: %s\n", strerror(ok));
				}
			}
		}
	}

	if (use_atomic_modesetting) {
		for_each_unreserved_plane_in_atomic_req(req, plane) {
			if ((plane->type == DRM_PLANE_TYPE_PRIMARY) || (plane->type == DRM_PLANE_TYPE_OVERLAY)) {
				drmdev_atomic_req_put_plane_property(req, plane->plane->plane_id, "FB_ID", 0);
				drmdev_atomic_req_put_plane_property(req, plane->plane->plane_id, "CRTC_ID", 0);
			}
		}
	}

	eglMakeCurrent(flutterpi.egl.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	
	if (use_atomic_modesetting) {
		do_commit:
		if (compositor->do_blocking_atomic_commits) {
			req_flags &= ~(DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT);
		} else {
			req_flags |= DRM_MODE_ATOMIC_NONBLOCK | DRM_MODE_PAGE_FLIP_EVENT;
		}
		
		ok = drmdev_atomic_req_commit(req, req_flags, NULL);
		if ((compositor->do_blocking_atomic_commits == false) && (ok < 0) && (errno == EBUSY)) {
			printf("[compositor] Non-blocking drmModeAtomicCommit failed with EBUSY.\n"
				"             Future drmModeAtomicCommits will be executed blockingly.\n"
				"             This may have have an impact on performance.\n");

			compositor->do_blocking_atomic_commits = true;
			schedule_fake_page_flip_event = true;
			goto do_commit;
		}

		drmdev_destroy_atomic_req(req);	
	}

	if (schedule_fake_page_flip_event) {
		uint64_t time = flutterpi.flutter.libflutter_engine.FlutterEngineGetCurrentTime();

		struct simulated_page_flip_event_data *data = malloc(sizeof(struct simulated_page_flip_event_data));
		if (data == NULL) {
			return false;
		}

		data->sec = time / 1000000000llu;
		data->usec = (time % 1000000000llu) / 1000;

		flutterpi_post_platform_task(execute_simulate_page_flip_event, data);
	}

	cpset_unlock(&compositor->cbs);

	return true;
}

int compositor_on_page_flip(
	uint32_t sec,
	uint32_t usec
) {
	return 0;
}

/// PLATFORM VIEW CALLBACKS
int compositor_set_view_callbacks(
	int64_t view_id,
	platform_view_mount_cb mount,
	platform_view_unmount_cb unmount,
	platform_view_update_view_cb update_view,
	platform_view_present_cb present,
	void *userdata
) {
	struct view_cb_data *entry;

	cpset_lock(&compositor.cbs);

	entry = get_cbs_for_view_id_locked(view_id);

	if (entry == NULL) {
		entry = calloc(1, sizeof(*entry));
		if (!entry) {
			cpset_unlock(&compositor.cbs);
			return ENOMEM;
		}

		cpset_put_locked(&compositor.cbs, entry);
	}

	entry->view_id = view_id;
	entry->mount = mount;
	entry->unmount = unmount;
	entry->update_view = update_view;
	entry->present = present;
	entry->userdata = userdata;

	return cpset_unlock(&compositor.cbs);
}

int compositor_remove_view_callbacks(int64_t view_id) {
	struct view_cb_data *entry;

	cpset_lock(&compositor.cbs);

	entry = get_cbs_for_view_id_locked(view_id);
	if (entry == NULL) {
		return EINVAL;
	}

	cpset_remove_locked(&compositor.cbs, entry);

	free(entry);

	cpset_unlock(&compositor.cbs);

	return 0;
}

/// COMPOSITOR INITIALIZATION
int compositor_initialize(struct drmdev *drmdev) {
	compositor.drmdev = drmdev;
	return 0;
}

static void destroy_cursor_buffer(void) {
	struct drm_mode_destroy_dumb destroy_req;

	munmap(compositor.cursor.buffer, compositor.cursor.buffer_size);

	drmModeRmFB(compositor.drmdev->fd, compositor.cursor.drm_fb_id);

	memset(&destroy_req, 0, sizeof destroy_req);
	destroy_req.handle = compositor.cursor.gem_bo_handle;

	ioctl(compositor.drmdev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);

	compositor.cursor.has_buffer = false;
	compositor.cursor.buffer_depth = 0;
	compositor.cursor.gem_bo_handle = 0;
	compositor.cursor.buffer_pitch = 0;
	compositor.cursor.buffer_width = 0;
	compositor.cursor.buffer_height = 0;
	compositor.cursor.buffer_size = 0;
	compositor.cursor.drm_fb_id = 0;
	compositor.cursor.buffer = NULL;
}

static int create_cursor_buffer(int width, int height, int bpp) {
	struct drm_mode_create_dumb create_req;
	struct drm_mode_map_dumb map_req;
	uint32_t drm_fb_id;
	uint32_t *buffer;
	uint64_t cap;
	uint8_t depth;
	int ok;

	ok = drmGetCap(compositor.drmdev->fd, DRM_CAP_DUMB_BUFFER, &cap);
	if (ok < 0) {
		ok = errno;
		perror("[compositor] Could not query GPU Driver support for dumb buffers. drmGetCap");
		goto fail_return_ok;
	}

	if (cap == 0) {
		fprintf(stderr, "[compositor] Kernel / GPU Driver does not support dumb DRM buffers. Mouse cursor will not be displayed.\n");
		ok = ENOTSUP;
		goto fail_return_ok;
	}

	ok = drmGetCap(compositor.drmdev->fd, DRM_CAP_DUMB_PREFERRED_DEPTH, &cap);
	if (ok < 0) {
		ok = errno;
		perror("[compositor] Could not query dumb buffer preferred depth capability. drmGetCap");
		goto fail_return_ok;
	}

	depth = (uint8_t) cap;

	if (depth != 32) {
		fprintf(stderr, "[compositor] Preferred framebuffer depth for hardware cursor is not supported by flutter-pi.\n");
	}

	memset(&create_req, 0, sizeof create_req);
	create_req.width = width;
	create_req.height = height;
	create_req.bpp = bpp;
	create_req.flags = 0;

	ok = ioctl(compositor.drmdev->fd, DRM_IOCTL_MODE_CREATE_DUMB, &create_req);
	if (ok < 0) {
		ok = errno;
		perror("[compositor] Could not create a dumb buffer for the hardware cursor. ioctl");
		goto fail_return_ok;
	}

	ok = drmModeAddFB(compositor.drmdev->fd, create_req.width, create_req.height, 32, create_req.bpp, create_req.pitch, create_req.handle, &drm_fb_id);
	if (ok < 0) {
		ok = errno;
		perror("[compositor] Could not make a DRM FB out of the hardware cursor buffer. drmModeAddFB");
		goto fail_destroy_dumb_buffer;
	}

	memset(&map_req, 0, sizeof map_req);
	map_req.handle = create_req.handle;

	ok = ioctl(compositor.drmdev->fd, DRM_IOCTL_MODE_MAP_DUMB, &map_req);
	if (ok < 0) {
		ok = errno;
		perror("[compositor] Could not prepare dumb buffer mmap for uploading the hardware cursor icon. ioctl");
		goto fail_rm_drm_fb;
	}

	buffer = mmap(0, create_req.size, PROT_READ | PROT_WRITE, MAP_SHARED, compositor.drmdev->fd, map_req.offset);
	if (buffer == MAP_FAILED) {
		ok = errno;
		perror("[compositor] Could not mmap dumb buffer for uploading the hardware cursor icon. mmap");
		goto fail_rm_drm_fb;
	}

	compositor.cursor.has_buffer = true;
	compositor.cursor.buffer_depth = depth;
	compositor.cursor.gem_bo_handle = create_req.handle;
	compositor.cursor.buffer_pitch = create_req.pitch;
	compositor.cursor.buffer_width = width;
	compositor.cursor.buffer_height = height;
	compositor.cursor.buffer_size = create_req.size;
	compositor.cursor.drm_fb_id = drm_fb_id;
	compositor.cursor.buffer = buffer;
	
	return 0;


	fail_rm_drm_fb:
	drmModeRmFB(compositor.drmdev->fd, drm_fb_id);

	fail_destroy_dumb_buffer: ;
	struct drm_mode_destroy_dumb destroy_req;
	memset(&destroy_req, 0, sizeof destroy_req);
	destroy_req.handle = create_req.handle;
	ioctl(compositor.drmdev->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy_req);

	fail_return_ok:
	return ok;
}

int compositor_apply_cursor_state(
	bool is_enabled,
	int rotation,
	double device_pixel_ratio
) {
	const struct cursor_icon *cursor;
	int ok;

	if (is_enabled == true) {
		// find the best fitting cursor icon.
		{
			double last_diff = INFINITY;

			cursor = NULL;
			for (int i = 0; i < n_cursors; i++) {
				double cursor_dpr = (cursors[i].width * 3 * 10.0) / (25.4 * 38);
				double cursor_screen_dpr_diff = device_pixel_ratio - cursor_dpr;
				if ((cursor_screen_dpr_diff >= 0) && (cursor_screen_dpr_diff < last_diff)) {
					cursor = cursors + i;
				}
			}
		}

		// destroy the old cursor buffer, if necessary
		if (compositor.cursor.has_buffer && (compositor.cursor.buffer_width != cursor->width)) {
			destroy_cursor_buffer();
		}

		// create a new cursor buffer, if necessary
		if (compositor.cursor.has_buffer == false) {
			create_cursor_buffer(cursor->width, cursor->width, 32);
		}

		if ((compositor.cursor.is_enabled == false) || (compositor.cursor.current_rotation != rotation) || (compositor.cursor.current_cursor != cursor)) {
			int rotated_hot_x, rotated_hot_y;
			
			if (rotation == 0) {
				memcpy(compositor.cursor.buffer, cursor->data, compositor.cursor.buffer_size);
				rotated_hot_x = cursor->hot_x;
				rotated_hot_y = cursor->hot_y;
			} else if ((rotation == 90) || (rotation == 180) || (rotation == 270)) {
				for (int y = 0; y < cursor->width; y++) {
					for (int x = 0; x < cursor->width; x++) {
						int buffer_x, buffer_y;
						if (rotation == 90) {
							buffer_x = cursor->width - y - 1;
							buffer_y = x;
						} else if (rotation == 180) {
							buffer_x = cursor->width - y - 1;
							buffer_y = cursor->width - x - 1;
						} else {
							buffer_x = y;
							buffer_y = cursor->width - x - 1;
						}
						
						int buffer_offset = compositor.cursor.buffer_pitch * buffer_y + (compositor.cursor.buffer_depth / 8) * buffer_x;
						int cursor_offset = cursor->width * y + x;
						
						compositor.cursor.buffer[buffer_offset / 4] = cursor->data[cursor_offset];
					}
				}

				if (rotation == 90) {
					rotated_hot_x = cursor->width - cursor->hot_y - 1;
					rotated_hot_y = cursor->hot_x;
				} else if (rotation == 180) {
					rotated_hot_x = cursor->width - cursor->hot_x - 1;
					rotated_hot_y = cursor->width - cursor->hot_y - 1;
				} else if (rotation == 270) {
					rotated_hot_x = cursor->hot_y;
					rotated_hot_y = cursor->width - cursor->hot_x - 1;
				}
			} else {
				return EINVAL;
			}

			compositor.cursor.current_rotation = rotation;
			compositor.cursor.current_cursor = cursor;
			compositor.cursor.cursor_size = cursor->width;
			compositor.cursor.hot_x = rotated_hot_x;
			compositor.cursor.hot_y = rotated_hot_y;
			compositor.cursor.is_enabled = true;

			ok = drmModeSetCursor2(
				compositor.drmdev->fd,
				compositor.drmdev->selected_crtc->crtc->crtc_id,
				compositor.cursor.gem_bo_handle,
				compositor.cursor.cursor_size,
				compositor.cursor.cursor_size,
				rotated_hot_x,
				rotated_hot_y
			);
			if (ok < 0) {
				perror("[compositor] Could not set the mouse cursor buffer. drmModeSetCursor");
				return errno;
			}

			ok = drmModeMoveCursor(
				compositor.drmdev->fd,
				compositor.drmdev->selected_crtc->crtc->crtc_id,
				compositor.cursor.x - compositor.cursor.hot_x,
				compositor.cursor.y - compositor.cursor.hot_y
			);
			if (ok < 0) {
				perror("[compositor] Could not move cursor. drmModeMoveCursor");
				return errno;
			}
		}
		
		return 0;
	} else if ((is_enabled == false) && (compositor.cursor.is_enabled == true)) {
		drmModeSetCursor(
			compositor.drmdev->fd,
			compositor.drmdev->selected_crtc->crtc->crtc_id,
			0, 0, 0
		);

		destroy_cursor_buffer();

		compositor.cursor.cursor_size = 0;
		compositor.cursor.current_cursor = NULL;
		compositor.cursor.current_rotation = 0;
		compositor.cursor.hot_x = 0;
		compositor.cursor.hot_y = 0;
		compositor.cursor.x = 0;
		compositor.cursor.y = 0;
		compositor.cursor.is_enabled = false;
	}
}

int compositor_set_cursor_pos(int x, int y) {
	int ok;

	if (compositor.cursor.is_enabled == false) {
		return EINVAL;
	}

	ok = drmModeMoveCursor(compositor.drmdev->fd, compositor.drmdev->selected_crtc->crtc->crtc_id, x - compositor.cursor.hot_x, y - compositor.cursor.hot_y);
	if (ok < 0) {
		perror("[compositor] Could not move cursor. drmModeMoveCursor");
		return errno;
	}

	compositor.cursor.x = x;
	compositor.cursor.y = y;    

	return 0;
}

const FlutterCompositor flutter_compositor = {
	.struct_size = sizeof(FlutterCompositor),
	.user_data = &compositor,
	.create_backing_store_callback = on_create_backing_store,
	.collect_backing_store_callback = on_collect_backing_store,
	.present_layers_callback = on_present_layers,
};