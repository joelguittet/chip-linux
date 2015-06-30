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

#define SUN4I_A10_PLL3_GATE_BIT	31
#define SUN4I_A10_PLL3_DIV_WIDTH	7
#define SUN4I_A10_PLL3_DIV_SHIFT	0

static DEFINE_SPINLOCK(sun4i_a10_pll3_lock);

static void __init sun4i_a10_pll3_setup(struct device_node *node)
{
	const char *clk_name = node->name, *parent;
	struct clk_factor *mult;
	struct clk_gate *gate;
	void __iomem *reg;
	struct clk *clk;

	of_property_read_string(node, "clock-output-names", &clk_name);
	parent = of_clk_get_parent_name(node, 0);

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n", clk_name);
		return;
	}

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return;

	gate->reg = reg;
	gate->bit_idx = SUN4I_A10_PLL3_GATE_BIT;
	gate->lock = &sun4i_a10_pll3_lock;

	mult = kzalloc(sizeof(*mult), GFP_KERNEL);
	if (!mult)
		goto free_gate;

	mult->reg = reg;
	mult->shift = SUN4I_A10_PLL3_DIV_SHIFT;
	mult->width = SUN4I_A10_PLL3_DIV_WIDTH;
	mult->lock = &sun4i_a10_pll3_lock;

	clk = clk_register_composite(NULL, clk_name,
				     &parent, 1,
				     NULL, NULL,
				     &mult->hw, &clk_factor_ops,
				     &gate->hw, &clk_gate_ops,
				     0);
	if (IS_ERR(clk)) {
		pr_err("%s: Couldn't register the clock\n", clk_name);
		goto free_mult;
	}

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

free_mult:
	kfree(mult);
free_gate:
	kfree(gate);
}

CLK_OF_DECLARE(sun4i_a10_pll3, "allwinner,sun4i-a10-pll3-clk",
	       sun4i_a10_pll3_setup);
