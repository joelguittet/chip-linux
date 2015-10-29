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

#include <linux/clk.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_panel.h>

#include "sun4i_drv.h"
#include "sun4i_tcon.h"

struct sun4i_rgb {
	struct drm_connector	connector;
	struct drm_encoder	encoder;
	struct drm_panel	*panel;

	struct sun4i_drv	*drv;

	bool			enabled;
};

static inline struct sun4i_rgb *
drm_connector_to_sun4i_rgb(struct drm_connector *connector)
{
	return container_of(connector, struct sun4i_rgb,
			    connector);
}

static inline struct sun4i_rgb *
drm_encoder_to_sun4i_rgb(struct drm_encoder *encoder)
{
	return container_of(encoder, struct sun4i_rgb,
			    encoder);
}

static int sun4i_rgb_get_modes(struct drm_connector *connector)
{
	struct sun4i_rgb *rgb =
		drm_connector_to_sun4i_rgb(connector);

	return rgb->panel->funcs->get_modes(rgb->panel);
}

static int sun4i_rgb_mode_valid(struct drm_connector *connector,
				struct drm_display_mode *mode)
{
	DRM_DEBUG_DRIVER("Validating modes...\n");

	if ((mode->hsync_start < 1) || (mode->hsync_start > 0x3ff) ||
	    (mode->htotal < 1) || (mode->htotal > 0xfff))
		return MODE_H_ILLEGAL;

	DRM_DEBUG_DRIVER("Horizontal parameters OK\n");

	if ((mode->vsync_start < 1) || (mode->vsync_start > 0x3ff) ||
	    (mode->vtotal < 1) || (mode->vtotal > 0xfff))
		return MODE_V_ILLEGAL;

	DRM_DEBUG_DRIVER("Vertical parameters OK\n");

	return MODE_OK;
}

static struct drm_encoder *
sun4i_rgb_best_encoder(struct drm_connector *connector)
{
	struct sun4i_rgb *rgb =
		drm_connector_to_sun4i_rgb(connector);

	return &rgb->encoder;
}

static struct drm_connector_helper_funcs sun4i_rgb_con_helper_funcs = {
	.get_modes	= sun4i_rgb_get_modes,
	.mode_valid	= sun4i_rgb_mode_valid,
	.best_encoder	= sun4i_rgb_best_encoder,
};

static enum drm_connector_status
sun4i_rgb_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static void
sun4i_rgb_connector_destroy(struct drm_connector *connector)
{
	struct sun4i_rgb *rgb = drm_connector_to_sun4i_rgb(connector);

	drm_panel_detach(rgb->panel);
	drm_connector_cleanup(connector);
}

static struct drm_connector_funcs sun4i_rgb_con_funcs = {
	.dpms			= drm_atomic_helper_connector_dpms,
	.detect			= sun4i_rgb_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= sun4i_rgb_connector_destroy,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static void sun4i_rgb_encoder_enable(struct drm_encoder *encoder)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	if (rgb->enabled)
		return;

	DRM_DEBUG_DRIVER("Enabling RGB output\n");

	drm_panel_enable(rgb->panel);
	sun4i_tcon_enable_channel(tcon, 0);

	rgb->enabled = true;
}

static void sun4i_rgb_encoder_disable(struct drm_encoder *encoder)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	if (!rgb->enabled)
		return;

	DRM_DEBUG_DRIVER("Disabling RGB output\n");

	sun4i_tcon_disable_channel(tcon, 0);
	drm_panel_disable(rgb->panel);

	rgb->enabled = false;
}

static bool sun4i_rgb_encoder_mode_fixup(struct drm_encoder *encoder,
					 const struct drm_display_mode *mode,
					 struct drm_display_mode *adjusted)
{
	return true;
}

static void sun4i_rgb_encoder_mode_set(struct drm_encoder *encoder,
				       struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	struct sun4i_rgb *rgb = drm_encoder_to_sun4i_rgb(encoder);
	struct sun4i_drv *drv = rgb->drv;
	struct sun4i_tcon *tcon = drv->tcon;

	sun4i_tcon0_mode_set(tcon, mode);

	clk_set_rate(tcon->dclk, mode->crtc_clock * 1000);
}

static struct drm_encoder_helper_funcs sun4i_rgb_enc_helper_funcs = {
	.mode_fixup	= sun4i_rgb_encoder_mode_fixup,
	.mode_set	= sun4i_rgb_encoder_mode_set,
	.disable	= sun4i_rgb_encoder_disable,
	.enable		= sun4i_rgb_encoder_enable,
};

static void sun4i_rgb_enc_destroy(struct drm_encoder *encoder)
{
	drm_encoder_cleanup(encoder);
}

static struct drm_encoder_funcs sun4i_rgb_enc_funcs = {
	.destroy	= sun4i_rgb_enc_destroy,
};

int sun4i_rgb_init(struct drm_device *drm)
{
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_rgb *rgb;
	struct device_node *np;
	int ret;

	rgb = devm_kzalloc(drm->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;
	rgb->drv = drv;

	np = of_parse_phandle(drm->dev->of_node, "allwinner,panel", 0);
	if (!np) {
		dev_err(drm->dev, "Couldn't find our panel DT node\n");
		return -ENODEV;
	}

	rgb->panel = of_drm_find_panel(np);
	if (!rgb->panel) {
		dev_err(drm->dev, "Couldn't find our panel\n");
		return -EPROBE_DEFER;
	}

	drm_encoder_helper_add(&rgb->encoder,
			       &sun4i_rgb_enc_helper_funcs);
	ret = drm_encoder_init(drm,
			       &rgb->encoder,
			       &sun4i_rgb_enc_funcs,
			       DRM_MODE_ENCODER_NONE);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialise the rgb encoder\n");
		goto err_out;
	}

	/* The RGB encoder can only work with the TCON channel 0 */
	rgb->encoder.possible_crtcs = BIT(0);

	drm_connector_helper_add(&rgb->connector,
				 &sun4i_rgb_con_helper_funcs);
	ret = drm_connector_init(drm, &rgb->connector,
				 &sun4i_rgb_con_funcs,
				 DRM_MODE_CONNECTOR_Unknown);
	if (ret) {
		dev_err(drm->dev, "Couldn't initialise the rgb connector\n");
		goto err_cleanup_connector;
	}

	drm_mode_connector_attach_encoder(&rgb->connector, &rgb->encoder);

	drm_panel_attach(rgb->panel, &rgb->connector);

	return 0;

err_cleanup_connector:
	drm_encoder_cleanup(&rgb->encoder);
err_out:
	return ret;
}
