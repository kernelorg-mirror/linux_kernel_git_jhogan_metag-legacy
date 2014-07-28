/*
 * Copyright (C) 2010-2011 Canonical Ltd <jeremy.kerr@canonical.com>
 * Copyright (C) 2011-2012 Mike Turquette, Linaro Ltd <mturquette@linaro.org>
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Fixed rate clock implementation with rate specified in a register field.
 * Based on fixed rate clock implementation.
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

/*
 * DOC: basic specified-rate clock that cannot gate
 *
 * Traits of this clock:
 * prepare - clk_(un)prepare only ensures parents are prepared
 * enable - clk_enable only ensures parents are enabled
 * rate - rate is always a fixed value.  No clk_set_rate support
 * parent - fixed parent.  No clk_set_parent support
 */

#define to_clk_specified_rate(_hw) \
	container_of(_hw, struct clk_specified_rate, hw)

static unsigned long clk_specified_rate_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_specified_rate *specified = to_clk_specified_rate(hw);
	struct clk_specified_rate_entry *entry;
	u32 val;
	unsigned int i;

	/* read configuration field */
	val = readl(specified->reg);
	val >>= specified->shift;
	val &= (1 << specified->width) - 1;

	/* match the value in the mapping */
	for (i = 0; i < specified->num_rates; ++i) {
		entry = &specified->rates[i];
		if (val == entry->value)
			return entry->rate;
	}

	/* unknown rate! */
	return 0;
}

const struct clk_ops clk_specified_rate_ops = {
	.recalc_rate = clk_specified_rate_recalc_rate,
};
EXPORT_SYMBOL_GPL(clk_specified_rate_ops);

/**
 * clk_register_specified_rate - register specified-rate clock
 * @dev:		device that is registering this clock
 * @name:		name of this clock
 * @parent_name:	name of clock's parent
 * @flags:		framework-specific flags
 * @reg:		config register
 * @shift:		shift into config register of frequency field
 * @width:		width of frequency field in config register
 * @rates:		value->rate mapping entries
 * @num_rates:		number of rates in @rates
 */
struct clk *clk_register_specified_rate(struct device *dev, const char *name,
		const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 shift, u8 width,
		struct clk_specified_rate_entry *rates,
		unsigned long num_rates)
{
	struct clk_specified_rate *specified;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate specified-rate clock */
	specified = kzalloc(sizeof(struct clk_specified_rate), GFP_KERNEL);
	if (!specified) {
		pr_err("%s(%s): could not allocate specified clk\n",
		       __func__, name);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_specified_rate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_specified_rate assignments */
	specified->reg = reg;
	specified->shift = shift;
	specified->width = width;
	specified->rates = rates;
	specified->num_rates = num_rates;
	specified->hw.init = &init;

	/* register the clock */
	clk = clk_register(dev, &specified->hw);

	if (IS_ERR(clk))
		kfree(specified);

	return clk;
}

#ifdef CONFIG_OF
/**
 * of_specified_clk_setup() - Setup function for specified fixed rate clock
 */
void __init of_specified_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	u32 shift, width, rate;
	void __iomem *reg;
	int len, num_rates, i;
	struct property *prop;
	struct clk_specified_rate_entry *rates;
	const __be32 *p;

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

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("%s(%s): of_iomap failed\n",
		       __func__, clk_name);
		return;
	}

	/* check clock-frequency exists */
	prop = of_find_property(node, "clock-frequency", &len);
	if (!prop) {
		pr_err("%s(%s): could not find clock-frequency property\n",
		       __func__, clk_name);
		goto err_iounmap;
	}

	if (len & (sizeof(u32)*2 - 1)) {
		pr_err("%s(%s): clock-frequency has invalid size of %d bytes\n",
		       __func__, clk_name, len);
		goto err_iounmap;
	}
	num_rates = len / sizeof(*rates);

	rates = kzalloc(sizeof(*rates)*num_rates, GFP_KERNEL);
	if (!rates) {
		pr_err("%s(%s): could not allocate %d rate mapping entries\n",
		       __func__, clk_name, num_rates);
		goto err_iounmap;
	}

	/* extract rate mapping */
	for (i = 0, p = of_prop_next_u32(prop, NULL, &rates[0].value);
	     p;
	     ++i, p = of_prop_next_u32(prop, p, &rates[i].value)) {
		p = of_prop_next_u32(prop, p, &rate);
		rates[i].rate = rate;
		pr_debug("%s(%s): map %u -> %lu Hz\n",
			 __func__, clk_name, rates[i].value, rates[i].rate);
	}

	clk = clk_register_specified_rate(NULL, clk_name, NULL, CLK_IS_ROOT,
					  reg, shift, width, rates, num_rates);
	if (IS_ERR(clk))
		goto err_kfree;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

err_kfree:
	kfree(rates);
err_iounmap:
	iounmap(reg);
}
EXPORT_SYMBOL_GPL(of_specified_clk_setup);
CLK_OF_DECLARE(specified_clk, "specified-clock", of_specified_clk_setup);
#endif
