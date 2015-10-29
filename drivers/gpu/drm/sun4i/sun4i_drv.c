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

#include <drm/drmP.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>

#include "sun4i_backend.h"
#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_framebuffer.h"
#include "sun4i_layer.h"
#include "sun4i_rgb.h"
#include "sun4i_tcon.h"
#include "sun4i_tv.h"

static void sun4i_drv_preclose(struct drm_device *drm,
			       struct drm_file *file_priv)
{
}

static void sun4i_drv_lastclose(struct drm_device *drm)
{
}

static int sun4i_drv_connector_plug_all(struct drm_device *drm)
{
	struct drm_connector *connector, *failed;
	int ret;

	mutex_lock(&drm->mode_config.mutex);
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		ret = drm_connector_register(connector);
		if (ret) {
			failed = connector;
			goto err;
		}
	}
	mutex_unlock(&drm->mode_config.mutex);
	return 0;

err:
	list_for_each_entry(connector, &drm->mode_config.connector_list, head) {
		if (failed == connector)
			break;

		drm_connector_unregister(connector);
	}
	mutex_unlock(&drm->mode_config.mutex);

	return ret;
}

static int sun4i_drv_enable_vblank(struct drm_device *drm, int pipe)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Enabling VBLANK on pipe %d\n", pipe);

	sun4i_tcon_enable_vblank(tcon, true);

	return 0;
}

static void sun4i_drv_disable_vblank(struct drm_device *drm, int pipe)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;

	DRM_DEBUG_DRIVER("Disabling VBLANK on pipe %d\n", pipe);

	sun4i_tcon_enable_vblank(tcon, false);
}

static int sun4i_drv_load(struct drm_device *drm, unsigned long flags)
{
	struct sun4i_drv *drv = drm->dev_private;
	int ret;

	drv = devm_kzalloc(drm->dev, sizeof(*drv), GFP_KERNEL);
	if (!drv)
		return -ENOMEM;

	drm->dev_private = drv;

	drm_vblank_init(drm, 1);
	drm_mode_config_init(drm);

	/* Prepare the backend */
	drv->backend = sun4i_backend_init(drm);
	if (IS_ERR(drv->backend)) {
		dev_err(drm->dev, "Couldn't initialise our backend\n");
		return PTR_ERR(drv->backend);
	}

	/* Prepare the TCON */
	drv->tcon = sun4i_tcon_init(drm);
	if (!drv->tcon) {
		dev_err(drm->dev, "Couldn't initialise our TCON\n");
		ret = -EINVAL;
		goto err_free_backend;
	}

	/* Create our layers */
	drv->layers = sun4i_layers_init(drm);
	if (!drv->layers) {
		dev_err(drm->dev, "Couldn't create the planes\n");
		ret = -EINVAL;
		goto err_free_tcon;
	}

	/* Create our CRTC */
	drv->crtc = sun4i_crtc_init(drm);
	if (!drv->crtc) {
		dev_err(drm->dev, "Couldn't create the CRTC\n");
		ret = -EINVAL;
		goto err_free_layers;
	}

	/* Create our outputs */
	ret = sun4i_rgb_init(drm);
	if (ret) {
		dev_err(drm->dev, "Couldn't create our RGB output\n");
		goto err_free_crtc;
	}

	ret = sun4i_tv_init(drm);
	if (ret) {
		dev_err(drm->dev, "Couldn't create our RGB output\n");
		goto err_free_rgb;
	}

	/* Create our framebuffer */
	drv->fbdev = sun4i_framebuffer_init(drm);
	if (IS_ERR(drv->fbdev)) {
		dev_err(drm->dev, "Couldn't create our framebuffer\n");
		ret = PTR_ERR(drv->fbdev);
		goto err_free_tv;
	}

	/* Enable connectors polling */
	drm_kms_helper_poll_init(drm);

	return 0;

err_free_tv:
err_free_rgb:
err_free_crtc:
err_free_layers:
err_free_tcon:
	sun4i_tcon_free(drv->tcon);
err_free_backend:
	sun4i_backend_free(drv->backend);

	return ret;
}

static int sun4i_drv_unload(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;

	drm_kms_helper_poll_fini(drm);
	sun4i_framebuffer_free(drm);
	sun4i_tcon_free(drv->tcon);
	sun4i_backend_free(drv->backend);
	drm_vblank_cleanup(drm);

	return 0;
}

static const struct file_operations sun4i_drv_fops = {
	.owner		= THIS_MODULE,
	.open		= drm_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl	= drm_compat_ioctl,
#endif
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= no_llseek,
	.mmap		= drm_gem_cma_mmap,
};

static struct drm_driver sun4i_drv_driver = {
	.driver_features	= DRIVER_GEM | DRIVER_MODESET | DRIVER_PRIME,

	/* Generic Operations */
	.load			= sun4i_drv_load,
	.unload			= sun4i_drv_unload,
	.preclose		= sun4i_drv_preclose,
	.lastclose		= sun4i_drv_lastclose,
	.fops			= &sun4i_drv_fops,
	.name			= "sun4i-drm",
	.desc			= "Allwinner sun4i Display Engine",
	.date			= "20150629",
	.major			= 1,
	.minor			= 0,

	/* GEM Operations */
	.gem_free_object	= drm_gem_cma_free_object,
	.gem_vm_ops		= &drm_gem_cma_vm_ops,

	/* PRIME Operations */
	.prime_handle_to_fd	= drm_gem_prime_handle_to_fd,
	.prime_fd_to_handle	= drm_gem_prime_fd_to_handle,
	.gem_prime_import	= drm_gem_prime_import,
	.gem_prime_export	= drm_gem_prime_export,
	.gem_prime_get_sg_table	= drm_gem_cma_prime_get_sg_table,
	.gem_prime_import_sg_table = drm_gem_cma_prime_import_sg_table,
	.gem_prime_vmap		= drm_gem_cma_prime_vmap,
	.gem_prime_vunmap	= drm_gem_cma_prime_vunmap,
	.gem_prime_mmap		= drm_gem_cma_prime_mmap,

	/* Frame Buffer Operations */
	.dumb_create		= drm_gem_cma_dumb_create,
	.dumb_map_offset	= drm_gem_cma_dumb_map_offset,
	.dumb_destroy		= drm_gem_dumb_destroy,

	/* VBlank Operations */
	.get_vblank_counter	= drm_vblank_count,
	.enable_vblank		= sun4i_drv_enable_vblank,
	.disable_vblank		= sun4i_drv_disable_vblank,
};

static int sun4i_drv_probe(struct platform_device *pdev)
{
	struct drm_device *drm;
	int ret;

	drm = drm_dev_alloc(&sun4i_drv_driver, &pdev->dev);
	if (!drm)
		return -ENOMEM;

	ret = drm_dev_set_unique(drm, dev_name(drm->dev));
	if (ret)
		goto free_drm;

	ret = drm_dev_register(drm, 0);
	if (ret)
		goto free_drm;

	ret = sun4i_drv_connector_plug_all(drm);
	if (ret)
		goto unregister_drm;

	return 0;

unregister_drm:
	drm_dev_unregister(drm);
free_drm:
	drm_dev_unref(drm);
	return ret;
}

static int sun4i_drv_remove(struct platform_device *pdev)
{
	struct drm_device *drm = platform_get_drvdata(pdev);

	drm_dev_unregister(drm);
	drm_dev_unref(drm);

	return 0;
}

static const struct of_device_id sun4i_drv_of_table[] = {
	{ .compatible = "allwinner,sun5i-a13-display-engine" },
	{ }
};
MODULE_DEVICE_TABLE(of, sun4i_drv_of_table);

static struct platform_driver sun4i_drv_platform_driver = {
	.probe		= sun4i_drv_probe,
	.remove		= sun4i_drv_remove,
	.driver		= {
		.name		= "sun4i-drm",
		.of_match_table	= sun4i_drv_of_table,
	},
};
module_platform_driver(sun4i_drv_platform_driver);

MODULE_AUTHOR("Boris Brezillon <boris.brezillon@free-electrons.com>");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_DESCRIPTION("Allwinner A10 Display Engine DRM/KMS Driver");
MODULE_LICENSE("GPL");
