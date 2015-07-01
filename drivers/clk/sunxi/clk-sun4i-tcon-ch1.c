/*
 * Copyright 2015 Maxime Ripard
 *
 * Maxime Ripard <maxime.ripard@free-electrons.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

#define SUN4I_TCON_CH1_SCLK_NAME_LEN	32

#define SUN4I_A10_TCON_CH1_SCLK1_PARENTS	2
#define SUN4I_A10_TCON_CH1_SCLK2_PARENTS	4

#define SUN4I_A10_TCON_CH1_SCLK2_GATE_BIT	31
#define SUN4I_A10_TCON_CH1_SCLK2_MUX_MASK	3
#define SUN4I_A10_TCON_CH1_SCLK2_MUX_SHIFT	24
#define SUN4I_A10_TCON_CH1_SCLK2_DIV_WIDTH	4
#define SUN4I_A10_TCON_CH1_SCLK2_DIV_SHIFT	0

#define SUN4I_A10_TCON_CH1_SCLK1_GATE_BIT	15
#define SUN4I_A10_TCON_CH1_SCLK1_MUX_MASK	1
#define SUN4I_A10_TCON_CH1_SCLK1_MUX_SHIFT	11

static DEFINE_SPINLOCK(sun4i_a10_tcon_ch1_lock);

static void __init sun4i_a10_tcon_ch1_setup(struct device_node *node)
{
	const char *sclk1_parents[SUN4I_A10_TCON_CH1_SCLK1_PARENTS];
	const char *sclk2_parents[SUN4I_A10_TCON_CH1_SCLK2_PARENTS];
	const char *sclk1_name = node->name;
	char sclk2_name[SUN4I_TCON_CH1_SCLK_NAME_LEN];
	char sclk2d2_name[SUN4I_TCON_CH1_SCLK_NAME_LEN];
	struct clk_gate *sclk1_gate, *sclk2_gate;
	struct clk_mux *sclk1_mux, *sclk2_mux;
	struct clk *sclk1, *sclk2, *sclk2d2;
	struct clk_divider *sclk2_div;
	void __iomem *reg;
	int i;

	of_property_read_string(node, "clock-output-names",
				&sclk1_name);

	snprintf(sclk2_name, SUN4I_TCON_CH1_SCLK_NAME_LEN,
		 "%s2", sclk1_name);

	snprintf(sclk2d2_name, SUN4I_TCON_CH1_SCLK_NAME_LEN,
		 "%s2d2", sclk1_name);

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n", sclk2_name);
		return;
	}

	for (i = 0; i < SUN4I_A10_TCON_CH1_SCLK2_PARENTS; i++)
		sclk2_parents[i] = of_clk_get_parent_name(node, i);

	sclk2_mux = kzalloc(sizeof(*sclk2_mux), GFP_KERNEL);
	if (!sclk2_mux)
		return;

	sclk2_mux->reg = reg;
	sclk2_mux->shift = SUN4I_A10_TCON_CH1_SCLK2_MUX_SHIFT;
	sclk2_mux->mask = SUN4I_A10_TCON_CH1_SCLK2_MUX_MASK;
	sclk2_mux->lock = &sun4i_a10_tcon_ch1_lock;

	sclk2_gate = kzalloc(sizeof(*sclk2_gate), GFP_KERNEL);
	if (!sclk2_gate)
		goto free_sclk2_mux;

	sclk2_gate->reg = reg;
	sclk2_gate->bit_idx = SUN4I_A10_TCON_CH1_SCLK2_GATE_BIT;
	sclk2_gate->lock = &sun4i_a10_tcon_ch1_lock;

	sclk2_div = kzalloc(sizeof(*sclk2_div), GFP_KERNEL);
	if (!sclk2_div)
		goto free_sclk2_gate;

	sclk2_div->reg = reg;
	sclk2_div->shift = SUN4I_A10_TCON_CH1_SCLK2_DIV_SHIFT;
	sclk2_div->width = SUN4I_A10_TCON_CH1_SCLK2_DIV_WIDTH;
	sclk2_div->lock = &sun4i_a10_tcon_ch1_lock;

	sclk2 = clk_register_composite(NULL, sclk2_name, sclk2_parents,
				       SUN4I_A10_TCON_CH1_SCLK2_PARENTS,
				       &sclk2_mux->hw, &clk_mux_ops,
				       &sclk2_div->hw, &clk_divider_ops,
				       &sclk2_gate->hw, &clk_gate_ops,
				       0);
	if (IS_ERR(sclk2)) {
		pr_err("%s: Couldn't register the clock\n", sclk2_name);
		goto free_sclk2_div;
	}

	sclk2d2 = clk_register_fixed_factor(NULL, sclk2d2_name, sclk2_name, 0,
					    1, 2);
	if (IS_ERR(sclk2d2)) {
		pr_err("%s: Couldn't register the clock\n", sclk2d2_name);
		goto free_sclk2;
	}

	sclk1_parents[0] = sclk2_name;
	sclk1_parents[1] = sclk2d2_name;

	sclk1_mux = kzalloc(sizeof(*sclk1_mux), GFP_KERNEL);
	if (!sclk1_mux)
		goto free_sclk2d2;

	sclk1_mux->reg = reg;
	sclk1_mux->shift = SUN4I_A10_TCON_CH1_SCLK1_MUX_SHIFT;
	sclk1_mux->mask = SUN4I_A10_TCON_CH1_SCLK1_MUX_MASK;
	sclk1_mux->lock = &sun4i_a10_tcon_ch1_lock;

	sclk1_gate = kzalloc(sizeof(*sclk1_gate), GFP_KERNEL);
	if (!sclk1_gate)
		goto free_sclk1_mux;

	sclk1_gate->reg = reg;
	sclk1_gate->bit_idx = SUN4I_A10_TCON_CH1_SCLK1_GATE_BIT;
	sclk1_gate->lock = &sun4i_a10_tcon_ch1_lock;

	sclk1 = clk_register_composite(NULL, sclk1_name, sclk1_parents,
				       SUN4I_A10_TCON_CH1_SCLK1_PARENTS,
				       &sclk1_mux->hw, &clk_mux_ops,
				       NULL, NULL,
				       &sclk1_gate->hw, &clk_gate_ops,
				       0);
	if (IS_ERR(sclk1)) {
		pr_err("%s: Couldn't register the clock\n", sclk1_name);
		goto free_sclk1_gate;
	}

	of_clk_add_provider(node, of_clk_src_simple_get, sclk1);

	return;

free_sclk1_gate:
	kfree(sclk1_gate);
free_sclk1_mux:
	kfree(sclk1_mux);
free_sclk2d2:
	clk_unregister(sclk2d2);
free_sclk2:
	clk_unregister(sclk2);
free_sclk2_div:
	kfree(sclk2_div);
free_sclk2_gate:
	kfree(sclk2_gate);
free_sclk2_mux:
	kfree(sclk2_mux);
}

CLK_OF_DECLARE(sun4i_a10_tcon_ch1, "allwinner,sun4i-a10-tcon-ch1-clk",
	       sun4i_a10_tcon_ch1_setup);
