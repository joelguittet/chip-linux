/*
 * Copyright (C) 2015 Free Electrons
 * Copyright (C) 2015 NextThing Co
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drmP.h>

#include "sun4i_drv.h"

struct sun4i_fbdev {
	struct drm_fb_helper	fb_helper;
	struct drm_fb_cma	*fb;
};

extern const struct drm_fb_helper_funcs drm_fb_cma_helper_funcs;

struct sun4i_fbdev *sun4i_fbdev_cma_init(struct drm_device *dev,
					 unsigned int preferred_bpp,
					 unsigned int num_crtc,
					 unsigned int max_conn_count)
{
	struct sun4i_fbdev *sun4i_fbdev;
	struct drm_fb_helper *helper;
	struct drm_connector *connector;
	struct drm_connector *rgbcon = NULL, *tvcon = NULL;
	int ret;

	sun4i_fbdev = kzalloc(sizeof(*sun4i_fbdev), GFP_KERNEL);
	if (!sun4i_fbdev) {
		dev_err(dev->dev, "Failed to allocate drm fbdev.\n");
		return ERR_PTR(-ENOMEM);
	}

	helper = &sun4i_fbdev->fb_helper;

	drm_fb_helper_prepare(dev, helper, &drm_fb_cma_helper_funcs);

	ret = drm_fb_helper_init(dev, helper, num_crtc, max_conn_count);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to initialize drm fb helper.\n");
		goto err_free;
	}

	mutex_lock(&dev->mode_config.mutex);
	drm_for_each_connector(connector, dev) {
		switch (connector->connector_type) {
		case DRM_MODE_CONNECTOR_Unknown:
			rgbcon = connector;
			break;
		case DRM_MODE_CONNECTOR_Composite:
			tvcon = connector;
			break;
		default:
			break;
		}
	}

	ret = drm_fb_helper_add_one_connector(helper, rgbcon ? rgbcon : tvcon);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to add connectors.\n");
		goto err_drm_fb_helper_fini;

	}
	mutex_unlock(&dev->mode_config.mutex);

	ret = drm_fb_helper_initial_config(helper, preferred_bpp);
	if (ret < 0) {
		dev_err(dev->dev, "Failed to set initial hw configuration.\n");
		goto err_drm_fb_helper_fini;
	}

	return sun4i_fbdev;

err_drm_fb_helper_fini:
	drm_fb_helper_fini(helper);
err_free:
	kfree(sun4i_fbdev);

	return ERR_PTR(ret);
}

static void sun4i_de_output_poll_changed(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;

	if (drv->fbdev)
		drm_fbdev_cma_hotplug_event(drv->fbdev);
}

static const struct drm_mode_config_funcs sun4i_de_mode_config_funcs = {
	.output_poll_changed	= sun4i_de_output_poll_changed,
	.atomic_check		= drm_atomic_helper_check,
	.atomic_commit		= drm_atomic_helper_commit,
	.fb_create		= drm_fb_cma_create,
};

struct sun4i_fbdev *sun4i_framebuffer_init(struct drm_device *drm)
{
	drm_mode_config_reset(drm);

	drm->mode_config.max_width = 8192;
	drm->mode_config.max_height = 8192;

	drm->mode_config.funcs = &sun4i_de_mode_config_funcs;

	return sun4i_fbdev_cma_init(drm, 32,
				  drm->mode_config.num_crtc,
				  drm->mode_config.num_connector);
}

void sun4i_framebuffer_free(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;

	drm_fbdev_cma_fini(drv->fbdev);
	drm_mode_config_cleanup(drm);
}
