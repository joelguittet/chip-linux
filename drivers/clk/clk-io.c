/*
 * Copyright (C) 2015 Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/clk-provider.h>

void clk_io_writel(struct clk_hw *hw, void __iomem *reg, struct regmap *regmap,
			u32 offset, u32 val)
{
	if (__clk_get_flags(hw->clk) & CLK_USE_REGMAP)
		regmap_write(regmap, offset, val);
	else
		clk_writel(val, reg);
}

u32 clk_io_readl(struct clk_hw *hw, void __iomem *reg, struct regmap *regmap,
			u32 offset)
{
	u32 val;

	if (__clk_get_flags(hw->clk) & CLK_USE_REGMAP)
		regmap_read(regmap, offset, &val);
	else
		val = clk_readl(reg);

	return val;
}

int clk_io_update_bits(struct clk_hw *hw, void __iomem *reg,
			struct regmap *regmap, u32 offset, u32 mask, u32 val)
{
	unsigned int tmp;

	if (__clk_get_flags(hw->clk) & CLK_USE_REGMAP)
		return regmap_update_bits(regmap, offset, mask, val);

	tmp = clk_readl(reg);
	tmp &= ~mask;
	tmp |= val;
	clk_writel(tmp, reg);

	return 0;
}
