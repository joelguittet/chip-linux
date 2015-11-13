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
#include <drm/drm_modes.h>

#include <linux/clk-provider.h>
#include <linux/ioport.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/regmap.h>

#include "sun4i_crtc.h"
#include "sun4i_drv.h"
#include "sun4i_tcon.h"

void sun4i_tcon_disable(struct sun4i_tcon *tcon)
{
	if (!tcon->enabled)
		return;

	DRM_DEBUG_DRIVER("Disabling TCON\n");

	/* Disable the TCON */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE, 0);

	tcon->enabled = false;
}

void sun4i_tcon_enable(struct sun4i_tcon *tcon)
{
	if (tcon->enabled)
		return;

	DRM_DEBUG_DRIVER("Enabling TCON\n");

	/* Enable the TCON */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_TCON_ENABLE,
			   SUN4I_TCON_GCTL_TCON_ENABLE);

	tcon->enabled = true;
}

static void sun4i_tcon_finish_page_flip(struct drm_device *dev,
					struct sun4i_crtc *scrtc)
{
	unsigned long flags;

	spin_lock_irqsave(&dev->event_lock, flags);
	if (scrtc->event) {
		drm_send_vblank_event(dev, 0, scrtc->event);
		drm_vblank_put(dev, 0);
		scrtc->event = NULL;
	}
	spin_unlock_irqrestore(&dev->event_lock, flags);
}

static irqreturn_t sun4i_tcon_handler(int irq, void *private)
{
	struct drm_device *drm = private;
	struct sun4i_drv *drv = drm->dev_private;
	struct sun4i_tcon *tcon = drv->tcon;
	struct sun4i_crtc *scrtc = drv->crtc;
	unsigned int status;

	regmap_read(tcon->regs, SUN4I_TCON_GINT0_REG, &status);

	if (!(status & (SUN4I_TCON_GINT0_VBLANK_INT(0) |
			SUN4I_TCON_GINT0_VBLANK_INT(1))))
		return IRQ_NONE;

	drm_handle_vblank(scrtc->crtc.dev, 0);
	sun4i_tcon_finish_page_flip(drm, scrtc);

	/* Acknowledge the interrupt */
	regmap_write(tcon->regs, SUN4I_TCON_GINT0_REG,
		     status);

	return IRQ_HANDLED;
}

static int sun4i_tcon_create_pixel_clock(struct drm_device *drm,
					 struct sun4i_tcon *tcon,
					 struct device_node *np)
{
	const char *pixel_clk_name;
	const char *sclk_name;
	struct clk_divider *div;
	struct clk_gate *gate;

	sclk_name = __clk_get_name(tcon->sclk0);
	of_property_read_string_index(np, "clock-output-names", 0,
				      &pixel_clk_name);

	div = devm_kzalloc(drm->dev, sizeof(*div), GFP_KERNEL);
	if (!div)
		return -ENOMEM;

	div->regmap = tcon->regs;
	div->offset = SUN4I_TCON0_DCLK_REG;
	div->shift = SUN4I_TCON0_DCLK_DIV_SHIFT;
	div->width = SUN4I_TCON0_DCLK_DIV_WIDTH;
	div->flags = CLK_DIVIDER_ONE_BASED | CLK_DIVIDER_ALLOW_ZERO;

	gate = devm_kzalloc(drm->dev, sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return -ENOMEM;

	gate->regmap = tcon->regs;
	gate->offset = SUN4I_TCON0_DCLK_REG;
	gate->bit_idx = SUN4I_TCON0_DCLK_GATE_BIT;

	tcon->dclk = clk_register_composite(drm->dev, pixel_clk_name,
					    &sclk_name, 1,
					    NULL, NULL,
					    &div->hw, &clk_divider_ops,
					    &gate->hw, &clk_gate_ops,
					    CLK_USE_REGMAP);

	return 0;
}

static int sun4i_tcon_init_clocks(struct drm_device *drm,
				  struct sun4i_tcon *tcon,
				  struct device_node *np)
{
	tcon->clk = of_clk_get_by_name(np, "ahb");
	if (IS_ERR(tcon->clk)) {
		dev_err(drm->dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->clk);
	}
	clk_prepare_enable(tcon->clk);

	tcon->sclk0 = of_clk_get_by_name(np, "tcon-ch0");
	if (IS_ERR(tcon->sclk0)) {
		dev_err(drm->dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->sclk0);
	}

	tcon->sclk1 = of_clk_get_by_name(np, "tcon-ch1");
	if (IS_ERR(tcon->sclk1)) {
		dev_err(drm->dev, "Couldn't get the TCON bus clock\n");
		return PTR_ERR(tcon->sclk1);
	}

	return sun4i_tcon_create_pixel_clock(drm, tcon, np);
}

static void sun4i_tcon_free_clocks(struct sun4i_tcon *tcon)
{
	clk_unregister(tcon->dclk);
	clk_put(tcon->sclk1);
	clk_put(tcon->sclk0);
	clk_disable_unprepare(tcon->clk);
	clk_put(tcon->clk);
}

static int sun4i_tcon_init_irq(struct drm_device *drm,
			       struct sun4i_tcon *tcon,
			       struct device_node *np)
{
	int irq, ret;

	irq = of_irq_get(np, 0);
	if (irq < 0) {
		dev_err(drm->dev, "Couldn't retrieve the TCON interrupt\n");
		return irq;
	}

	ret = devm_request_irq(drm->dev, irq, sun4i_tcon_handler, 0,
			       dev_name(drm->dev), tcon);
	if (ret) {
		dev_err(drm->dev, "Couldn't request the IRQ\n");
		return ret;
	}

	return 0;
}

static struct regmap_config sun4i_tcon_regmap_config = {
	.reg_bits	= 32,
	.val_bits	= 32,
	.reg_stride	= 4,
	.max_register	= 0x800,
	.name		= "tcon",
};

static int sun4i_tcon_init_regmap(struct drm_device *drm,
				  struct sun4i_tcon *tcon,
				  struct device_node *np)
{
	struct resource res;
	void __iomem *regs;
	int ret;

	ret = of_address_to_resource(np, 0, &res);
	regs = devm_ioremap_resource(drm->dev, &res);
	if (IS_ERR(regs)) {
		dev_err(drm->dev, "Couldn't map the TCON registers\n");
		return PTR_ERR(regs);
	}

	tcon->regs = devm_regmap_init_mmio(drm->dev, regs,
					   &sun4i_tcon_regmap_config);
	if (IS_ERR(tcon->regs)) {
		dev_err(drm->dev, "Couldn't create the TCON regmap\n");
		return PTR_ERR(tcon->regs);
	}

	/* Make sure the TCON is disabled and all IRQs are off */
	regmap_write(tcon->regs, SUN4I_TCON_GCTL_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT0_REG, 0);
	regmap_write(tcon->regs, SUN4I_TCON_GINT1_REG, 0);

	/* Disable IO lines and set them to tristate */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, ~0);
	regmap_write(tcon->regs, SUN4I_TCON1_IO_TRI_REG, ~0);

	return 0;
}

struct sun4i_tcon *sun4i_tcon_init(struct drm_device *drm)
{
	struct sun4i_de *de = drm->dev_private;
	struct sun4i_tcon *tcon;
	struct device_node *np;
	int ret = 0;

	tcon = devm_kzalloc(drm->dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return NULL;
	tcon->de = de;

	np = of_parse_phandle(drm->dev->of_node, "allwinner,tcon", 0);
	if (!np) {
		dev_err(drm->dev, "Couldn't find the tcon node\n");
		return NULL;
	}

	ret = sun4i_tcon_init_regmap(drm, tcon, np);
	if (ret) {
		dev_err(drm->dev, "Couldn't init our TCON regmap\n");
		goto err_node_put;
	}

	ret = sun4i_tcon_init_clocks(drm, tcon, np);
	if (ret) {
		dev_err(drm->dev, "Couldn't init our TCON clocks\n");
		goto err_free_regmap;
	}

	ret = sun4i_tcon_init_irq(drm, tcon, np);
	if (ret) {
		dev_err(drm->dev, "Couldn't init our TCON interrupts\n");
		goto err_free_clocks;
	}

	return tcon;

err_free_clocks:
	sun4i_tcon_free_clocks(tcon);
err_free_regmap:
err_node_put:
	of_node_put(np);
	return NULL;
}

void sun4i_tcon_free(struct sun4i_tcon *tcon)
{
	sun4i_tcon_free_clocks(tcon);
}

void sun4i_tcon_disable_channel(struct sun4i_tcon *tcon, int channel)
{
	/* Disable the TCON's channel */
	if (channel == 0) {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE, 0);
		clk_disable_unprepare(tcon->dclk);
	} else if (channel == 1) {
		regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
				   SUN4I_TCON1_CTL_TCON_ENABLE, 0);
		clk_disable_unprepare(tcon->sclk1);
	}
}

void sun4i_tcon_enable_channel(struct sun4i_tcon *tcon, int channel)
{
	/* Enable the TCON's channel */
	if (channel == 0) {
		regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
				   SUN4I_TCON0_CTL_TCON_ENABLE,
				   SUN4I_TCON0_CTL_TCON_ENABLE);
		clk_prepare_enable(tcon->dclk);
	} else if (channel == 1) {
		regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
				   SUN4I_TCON1_CTL_TCON_ENABLE,
				   SUN4I_TCON1_CTL_TCON_ENABLE);
		clk_prepare_enable(tcon->sclk1);
	}
}

void sun4i_tcon_enable_vblank(struct sun4i_tcon *tcon, bool enable)
{
	u32 mask, val = 0;

	DRM_DEBUG_DRIVER("%sabling VBLANK interrupt\n", enable ? "En" : "Dis");

	mask = SUN4I_TCON_GINT0_VBLANK_ENABLE(0) |
	       SUN4I_TCON_GINT0_VBLANK_ENABLE(1);

	if (enable)
		val = mask;

	regmap_update_bits(tcon->regs, SUN4I_TCON_GINT0_REG, mask, val);
}

static int sun4i_tcon_get_clk_delay(struct drm_display_mode *mode,
				    int channel)
{
	int delay = mode->vtotal - mode->vdisplay;

	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		delay /= 2;

	if (channel == 1)
		delay -= 2;

	delay = min(delay, 30);

	DRM_DEBUG_DRIVER("TCON %d clock delay %u\n", channel, delay);

	return delay;
}

void sun4i_tcon0_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode)
{
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val;

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 1);
	regmap_update_bits(tcon->regs, SUN4I_TCON0_CTL_REG,
			   SUN4I_TCON0_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON0_CTL_CLK_DELAY(clk_delay));

	/* Set the resolution */
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC0_REG,
		     SUN4I_TCON0_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON0_BASIC0_Y(mode->crtc_vdisplay));

	/* Set horizontal display timings */
	bp = mode->crtc_htotal - mode->crtc_hsync_end;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->crtc_htotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC1_REG,
		     SUN4I_TCON0_BASIC1_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON0_BASIC1_H_BACKPORCH(bp));

	/* Set vertical display timings */
	bp = mode->crtc_vtotal - mode->crtc_vsync_end;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->crtc_vtotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC2_REG,
		     SUN4I_TCON0_BASIC2_V_TOTAL(mode->crtc_vtotal) |
		     SUN4I_TCON0_BASIC2_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON0_BASIC3_REG,
		     SUN4I_TCON0_BASIC3_V_SYNC(vsync) |
		     SUN4I_TCON0_BASIC3_H_SYNC(hsync));

	/* TODO: Fix pixel clock phase shift */
	val = SUN4I_TCON0_IO_POL_DCLK_PHASE(1);

	/* Setup the polarity of the various signals */
	if (!(mode->flags & DRM_MODE_FLAG_PHSYNC))
		val |= SUN4I_TCON0_IO_POL_HSYNC_POSITIVE;

	if (!(mode->flags & DRM_MODE_FLAG_PVSYNC))
		val |= SUN4I_TCON0_IO_POL_VSYNC_POSITIVE;

	/* Map output pins to channel 0 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON0);

	regmap_write(tcon->regs, SUN4I_TCON0_IO_POL_REG, val);

	/* Enable the output on the pins */
	regmap_write(tcon->regs, SUN4I_TCON0_IO_TRI_REG, 0);
}

void sun4i_tcon1_mode_set(struct sun4i_tcon *tcon,
			  struct drm_display_mode *mode)
{
	unsigned int bp, hsync, vsync;
	u8 clk_delay;
	u32 val;

	/* Adjust clock delay */
	clk_delay = sun4i_tcon_get_clk_delay(mode, 1);
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_CLK_DELAY_MASK,
			   SUN4I_TCON1_CTL_CLK_DELAY(clk_delay));

	/* Set interlaced mode */
	if (mode->flags & DRM_MODE_FLAG_INTERLACE)
		val = SUN4I_TCON1_CTL_INTERLACE_ENABLE;
	else
		val = 0;
	regmap_update_bits(tcon->regs, SUN4I_TCON1_CTL_REG,
			   SUN4I_TCON1_CTL_INTERLACE_ENABLE,
			   val);

	/* Set the input resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC0_REG,
		     SUN4I_TCON1_BASIC0_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC0_Y(mode->crtc_vdisplay));

	/* Set the upscaling resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC1_REG,
		     SUN4I_TCON1_BASIC1_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC1_Y(mode->crtc_vdisplay));

	/* Set the output resolution */
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC2_REG,
		     SUN4I_TCON1_BASIC2_X(mode->crtc_hdisplay) |
		     SUN4I_TCON1_BASIC2_Y(mode->crtc_vdisplay));

	/* Set horizontal display timings */
	bp = mode->crtc_htotal - mode->crtc_hsync_end;
	DRM_DEBUG_DRIVER("Setting horizontal total %d, backporch %d\n",
			 mode->htotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC3_REG,
		     SUN4I_TCON1_BASIC3_H_TOTAL(mode->crtc_htotal) |
		     SUN4I_TCON1_BASIC3_H_BACKPORCH(bp));

	/* Set vertical display timings */
	bp = mode->crtc_vtotal - mode->crtc_vsync_end;
	DRM_DEBUG_DRIVER("Setting vertical total %d, backporch %d\n",
			 mode->vtotal, bp);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC4_REG,
		     SUN4I_TCON1_BASIC4_V_TOTAL(mode->vtotal) |
		     SUN4I_TCON1_BASIC4_V_BACKPORCH(bp));

	/* Set Hsync and Vsync length */
	hsync = mode->crtc_hsync_end - mode->crtc_hsync_start;
	vsync = mode->crtc_vsync_end - mode->crtc_vsync_start;
	DRM_DEBUG_DRIVER("Setting HSYNC %d, VSYNC %d\n", hsync, vsync);
	regmap_write(tcon->regs, SUN4I_TCON1_BASIC5_REG,
		     SUN4I_TCON1_BASIC5_V_SYNC(vsync) |
		     SUN4I_TCON1_BASIC5_H_SYNC(hsync));

	/* Map output pins to channel 1 */
	regmap_update_bits(tcon->regs, SUN4I_TCON_GCTL_REG,
			   SUN4I_TCON_GCTL_IOMAP_MASK,
			   SUN4I_TCON_GCTL_IOMAP_TCON1);
}
