/*
 * linux/drivers/clk/clk-io.h
 *
 * Copyright (C) 2015 Matthias Brugger <matthias.bgg@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __LINUX_CLK_IO_H
#define __LINUX_CLK_IO_H

#include <linux/clk-provider.h>

void clk_io_writel(struct clk_hw *hw, void __iomem *reg, struct regmap *regmap,
			u32 offset, u32 val);
u32 clk_io_readl(struct clk_hw *hw, void __iomem *reg, struct regmap *regmap,
			u32 offset);
int clk_io_update_bits(struct clk_hw *hw, void __iomem *reg,
			struct regmap *regmap, u32 offset, u32 mask, u32 val);

#endif
