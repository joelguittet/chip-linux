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
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_cma_helper.h>
#include <drm/drm_gem_cma_helper.h>
#include <drm/drm_plane_helper.h>

#include "sun4i_backend.h"
#include "sun4i_drv.h"

static u32 sunxi_rgb2yuv_coef[12] = {
	0x00000107, 0x00000204, 0x00000064, 0x00000108,
	0x00003f69, 0x00003ed6, 0x000001c1, 0x00000808,
	0x000001c1, 0x00003e88, 0x00003fb8, 0x00000808
};

void sun4i_backend_apply_color_correction(struct sun4i_backend *backend)
{
	int i;

	/* Set color correction */
	regmap_write(backend->regs, SUN4I_BACKEND_OCCTL_REG,
		     SUN4I_BACKEND_OCCTL_ENABLE);

	for (i = 0; i < 12; i++)
		regmap_write(backend->regs, SUN4I_BACKEND_OCRCOEF_REG(i),
			     sunxi_rgb2yuv_coef[i]);
}

void sun4i_backend_commit(struct sun4i_backend *backend)
{
	DRM_DEBUG_DRIVER("Committing changes\n");

	regmap_write(backend->regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS |
		     SUN4I_BACKEND_REGBUFFCTL_LOADCTL);
}

void sun4i_backend_layer_enable(struct sun4i_backend *backend,
				int layer, bool enable)
{
	u32 val;

	DRM_DEBUG_DRIVER("Enabling layer %d\n", layer);

	if (enable)
		val = SUN4I_BACKEND_MODCTL_LAY_EN(layer);
	else
		val = 0;

	regmap_update_bits(backend->regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_LAY_EN(layer), val);
}

static int sun4i_backend_drm_format_to_layer(u32 format, u32 *mode)
{
	switch (format) {
	case DRM_FORMAT_XRGB8888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_XRGB8888;
		break;

	case DRM_FORMAT_RGB888:
		*mode = SUN4I_BACKEND_LAY_FBFMT_RGB888;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

int sun4i_backend_update_layer_coord(struct sun4i_backend *backend,
				     int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;

	DRM_DEBUG_DRIVER("Updating layer %d\n", layer);

	if (plane->type == DRM_PLANE_TYPE_PRIMARY) {
		DRM_DEBUG_DRIVER("Primary layer, updating global size W: %u H: %u\n",
				 state->crtc_w, state->crtc_h);
		regmap_write(backend->regs, SUN4I_BACKEND_DISSIZE_REG,
			     SUN4I_BACKEND_DISSIZE(state->crtc_w,
						   state->crtc_h));
	}

	/* Set the line width */
	DRM_DEBUG_DRIVER("Layer line width: %d bits\n", fb->pitches[0] * 8);
	regmap_write(backend->regs, SUN4I_BACKEND_LAYLINEWIDTH_REG(layer),
		     fb->pitches[0] * 8);

	/* Set height and width */
	DRM_DEBUG_DRIVER("Layer size W: %u H: %u\n",
			 state->crtc_w, state->crtc_h);
	regmap_write(backend->regs, SUN4I_BACKEND_LAYSIZE_REG(layer),
		     SUN4I_BACKEND_LAYSIZE(state->crtc_w,
					   state->crtc_h));

	/* Set base coordinates */
	DRM_DEBUG_DRIVER("Layer coordinates X: %d Y: %d\n",
			 state->crtc_x, state->crtc_y);
	regmap_write(backend->regs, SUN4I_BACKEND_LAYCOOR_REG(layer),
		     SUN4I_BACKEND_LAYCOOR(state->crtc_x,
					   state->crtc_y));

	return 0;
}

int sun4i_backend_update_layer_formats(struct sun4i_backend *backend,
				       int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	bool interlaced = false;
	u32 val;
	int ret;

	if (plane->state->crtc)
		interlaced = plane->state->crtc->state->adjusted_mode.flags
			& DRM_MODE_FLAG_INTERLACE;

	regmap_update_bits(backend->regs, SUN4I_BACKEND_MODCTL_REG,
			   SUN4I_BACKEND_MODCTL_ITLMOD_EN,
			   interlaced ? SUN4I_BACKEND_MODCTL_ITLMOD_EN : 0);

	DRM_DEBUG_DRIVER("Switching display backend interlaced mode %s\n",
			 interlaced ? "on" : "off");

	ret = sun4i_backend_drm_format_to_layer(fb->pixel_format, &val);
	if (ret) {
		DRM_DEBUG_DRIVER("Invalid format\n");
		return val;
	}

	regmap_update_bits(backend->regs, SUN4I_BACKEND_ATTCTL_REG1(layer),
			   SUN4I_BACKEND_ATTCTL_REG1_LAY_FBFMT, val);

	return 0;
}

int sun4i_backend_update_layer_buffer(struct sun4i_backend *backend,
				      int layer, struct drm_plane *plane)
{
	struct drm_plane_state *state = plane->state;
	struct drm_framebuffer *fb = state->fb;
	struct drm_gem_cma_object *gem;
	u32 lo_paddr, hi_paddr;
	dma_addr_t paddr;
	int bpp;

	/* Get the physical address of the buffer in memory */
	gem = drm_fb_cma_get_gem_obj(fb, 0);

	DRM_DEBUG_DRIVER("Using GEM @ 0x%x\n", gem->paddr);

	/* Compute the start of the displayed memory */
	bpp = drm_format_plane_cpp(fb->pixel_format, 0);
	paddr = gem->paddr + fb->offsets[0];
	paddr += state->src_x * bpp;
	paddr += state->src_y * fb->pitches[0];

	DRM_DEBUG_DRIVER("Setting buffer address to 0x%x\n", paddr);

	/* Write the 32 lower bits of the address (in bits) */
	lo_paddr = paddr << 3;
	DRM_DEBUG_DRIVER("Setting address lower bits to 0x%x\n", lo_paddr);
	regmap_write(backend->regs, SUN4I_BACKEND_LAYFB_L32ADD_REG(layer),
		     lo_paddr);

	/* And the upper bits */
	hi_paddr = paddr >> 29;
	DRM_DEBUG_DRIVER("Setting address high bits to 0x%x\n", hi_paddr);
	regmap_update_bits(backend->regs, SUN4I_BACKEND_LAYFB_H4ADD_REG,
			   SUN4I_BACKEND_LAYFB_H4ADD_MSK(layer),
			   SUN4I_BACKEND_LAYFB_H4ADD(layer, hi_paddr));

	return 0;
}

static struct regmap_config sun4i_backend_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x5800,
	.name		= "backend",
};

struct sun4i_backend *sun4i_backend_init(struct drm_device *drm)
{
	struct sun4i_backend *backend;
	struct resource *res;
	void __iomem *regs;
	int i;

	backend = devm_kzalloc(drm->dev, sizeof(*backend), GFP_KERNEL);
	if (!backend)
		return ERR_PTR(-ENOMEM);

	res = platform_get_resource_byname(to_platform_device(drm->dev),
					   IORESOURCE_MEM, "backend0");
	regs = devm_ioremap_resource(drm->dev, res);
	if (IS_ERR(regs)) {
		dev_err(drm->dev, "Couldn't map the backend0 registers\n");
		return ERR_CAST(regs);
	}

	backend->regs = devm_regmap_init_mmio(drm->dev, regs,
					      &sun4i_backend_regmap_config);
	if (IS_ERR(backend->regs)) {
		dev_err(drm->dev, "Couldn't create the backend0 regmap\n");
		return ERR_CAST(backend->regs);
	}

	backend->bus_clk = devm_clk_get(drm->dev, "backend0-bus");
	if (IS_ERR(backend->bus_clk)) {
		dev_err(drm->dev, "Couldn't get the backend bus clock\n");
		return ERR_CAST(backend->bus_clk);
	}
	clk_prepare_enable(backend->bus_clk);

	backend->mod_clk = devm_clk_get(drm->dev, "backend0-mod");
	if (IS_ERR(backend->mod_clk)) {
		dev_err(drm->dev, "Couldn't get the backend module clock\n");
		return ERR_CAST(backend->mod_clk);
	}
	clk_prepare_enable(backend->mod_clk);

	backend->ram_clk = devm_clk_get(drm->dev, "backend0-ram");
	if (IS_ERR(backend->ram_clk)) {
		dev_err(drm->dev, "Couldn't get the backend RAM clock\n");
		return ERR_CAST(backend->ram_clk);
	}
	clk_prepare_enable(backend->ram_clk);

	/* Reset the registers */
	for (i = 0x800; i < 0x1000; i += 4)
		regmap_write(backend->regs, i, 0);

	/* Disable registers autoloading */
	regmap_write(backend->regs, SUN4I_BACKEND_REGBUFFCTL_REG,
		     SUN4I_BACKEND_REGBUFFCTL_AUTOLOAD_DIS);

	/* Enable the backend */
	regmap_write(backend->regs, SUN4I_BACKEND_MODCTL_REG,
		     SUN4I_BACKEND_MODCTL_DEBE_EN |
		     SUN4I_BACKEND_MODCTL_START_CTL);

	return backend;
}

void sun4i_backend_free(struct sun4i_backend *backend)
{
	clk_disable_unprepare(backend->ram_clk);
	clk_disable_unprepare(backend->mod_clk);
	clk_disable_unprepare(backend->bus_clk);
}
