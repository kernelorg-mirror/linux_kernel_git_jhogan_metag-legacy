/*
 * Copyright (C) 2012 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Clock deleter in TZ1090
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

/**
 * struct clk_tz1090_deleter - Clock deleter
 *
 * @hw:		handle between common and hardware-specific interfaces
 * @reg:	delete register
 * @shift:	start bit of delete field
 * @width:	width of delete field
 *
 * Deleter in TZ1090.  Implements .recalc_rate, .set_rate and .round_rate
 */
struct clk_tz1090_deleter {
	struct clk_hw	hw;
	void __iomem	*reg;
	u8		shift;
	u8		width;
};

/*
 * DOC: TZ1090 adjustable deleter clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_prepare only ensures that parents are prepared
 * enable - clk_enable only ensures that parents are enabled
 * rate - rate is adjustable in hardware but set_rate unimplemented.
 *		clk->rate = (parent->rate * ((1 << width) - delete)) >> width
 * parent - fixed parent. No clk_set_parent support
 */

#define to_clk_tz1090_deleter(_hw) container_of(_hw, struct clk_tz1090_deleter, hw)

static unsigned long clk_tz1090_deleter_recalc_rate(struct clk_hw *hw,
						    unsigned long parent_rate)
{
	struct clk_tz1090_deleter *deleter = to_clk_tz1090_deleter(hw);
	u32 val;
	u32 period = (1 << deleter->width);
	u32 mask = period - 1;

	val = (readl(deleter->reg) >> deleter->shift) & mask;
	return ((u64)parent_rate * (period - val)) >> deleter->width;
}

static const struct clk_ops clk_tz1090_deleter_ops = {
	.recalc_rate = clk_tz1090_deleter_recalc_rate,
};

/**
 * clk_register_tz1090_deleter_setup - register a clock deleter clock
 * @dev:		device registering this clock
 * @name:		name of this clock
 * @parent_name:	name of clock's parent
 * @flags:		framework-specific flags
 * @reg:		register address to adjust deleter
 * @shift:		start bit of delete field
 * @width:		width of delete field
 *
 * Register a TZ1090 clock deleter with the clock framework.
 */
static struct clk *__init clk_register_tz1090_deleter(struct device *dev,
						      const char *name,
						      const char *parent_name,
						      unsigned long flags,
						      void __iomem *reg,
						      u8 shift,
						      u8 width)
{
	struct clk_tz1090_deleter *deleter;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the divider */
	deleter = kzalloc(sizeof(struct clk_tz1090_deleter), GFP_KERNEL);
	if (!deleter) {
		pr_err("%s: could not allocate deleter clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_tz1090_deleter_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name: NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_tz1090_deleter assignments */
	deleter->reg = reg;
	deleter->shift = shift;
	deleter->width = width;
	deleter->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &deleter->hw);

	if (IS_ERR(clk))
		kfree(deleter);

	return clk;
}

#ifdef CONFIG_OF
/**
 * of_tz1090_deleter_setup() - Setup function for clock deleter
 */
static void __init of_tz1090_deleter_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	void __iomem *reg;
	u32 shift, width;
	const char *parent_name;

	of_property_read_string(node, "clock-output-names", &clk_name);

	if (of_property_read_u32(node, "shift", &shift)) {
		pr_err("%s(%s): could not read shift property\n",
		       __func__, clk_name);
		return;
	}

	if (of_property_read_u32(node, "width", &width)) {
		pr_err("%s(%s): could not read width property\n",
		       __func__, clk_name);
		return;
	}

	parent_name = of_clk_get_parent_name(node, 0);
	if (!parent_name) {
		pr_err("%s(%s): could not read parent clock\n",
		       __func__, clk_name);
		return;
	}

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("%s(%s): of_iomap failed\n",
		       __func__, clk_name);
		return;
	}

	clk = clk_register_tz1090_deleter(NULL, clk_name, parent_name, 0, reg,
					  shift, width);
	if (IS_ERR(clk))
		goto err_iounmap;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

err_iounmap:
	iounmap(reg);
}
CLK_OF_DECLARE(tz1090_deleter_clk, "img,tz1090-deleter",
	       of_tz1090_deleter_setup);
#endif /* CONFIG_OF */
