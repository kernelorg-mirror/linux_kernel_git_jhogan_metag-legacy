/*
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Metag simple multiplexer clock implementation
 * Based on simple multiplexer clock implementation, but does appropriate
 * locking to protect registers shared between hardware threads.
 *
 */

#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <asm/global_lock.h>

/**
 * struct clk_metag_mux - metag multiplexer clock
 *
 * @mux:	the parent class
 * @ops:	pointer to clk_ops of parent class
 *
 * Clock with multiple selectable parents. Extends basic mux by using a global
 * exclusive lock when read-modify-writing the mux field so that multiple
 * threads/cores can use different fields in the same register.
 */
struct clk_metag_mux {
	struct clk_mux		mux;
	const struct clk_ops	*ops;
};

static inline struct clk_metag_mux *to_clk_metag_mux(struct clk_hw *hw)
{
	struct clk_mux *mux = container_of(hw, struct clk_mux, hw);

	return container_of(mux, struct clk_metag_mux, mux);
}

static u8 clk_metag_mux_get_parent(struct clk_hw *hw)
{
	struct clk_metag_mux *mux = to_clk_metag_mux(hw);

	return mux->ops->get_parent(&mux->mux.hw);
}

/* Acquire exclusive lock since other cores may access the same register */
static int clk_metag_mux_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_metag_mux *mux = to_clk_metag_mux(hw);
	int ret;
	unsigned long flags;

	__global_lock2(flags);
	ret = mux->ops->set_parent(&mux->mux.hw, index);
	__global_unlock2(flags);

	return ret;
}

static long clk_metag_mux_determine_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate,
				     struct clk **best_parent)
{
	struct clk_metag_mux *mux = to_clk_metag_mux(hw);

	return mux->ops->determine_rate(&mux->mux.hw, rate, prate, best_parent);
}

static const struct clk_ops clk_metag_mux_ops = {
	.get_parent = clk_metag_mux_get_parent,
	.set_parent = clk_metag_mux_set_parent,
	.determine_rate = clk_metag_mux_determine_rate,
};

static struct clk *__init clk_register_metag_mux(struct device *dev,
		const char *name, const char **parent_names, u8 num_parents,
		s32 default_parent, unsigned long flags, void __iomem *reg,
		u8 shift, u8 width, u8 clk_mux_flags)
{
	struct clk_metag_mux *mux;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the mux */
	mux = kzalloc(sizeof(struct clk_metag_mux), GFP_KERNEL);
	if (!mux) {
		pr_err("%s: could not allocate metag mux clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_metag_mux_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = parent_names;
	init.num_parents = num_parents;

	/* struct clk_mux assignments */
	mux->mux.reg = reg;
	mux->mux.shift = shift;
	mux->mux.mask = BIT(width) - 1;
	mux->mux.flags = clk_mux_flags;
	mux->mux.hw.init = &init;

	/* struct clk_metag_mux assignments */
	mux->ops = &clk_mux_ops;

	/* set default value */
	if (default_parent >= 0)
		clk_metag_mux_set_parent(&mux->mux.hw, default_parent);

	clk = clk_register(dev, &mux->mux.hw);

	if (IS_ERR(clk))
		kfree(mux);

	return clk;
}

#ifdef CONFIG_OF
/**
 * of_metag_mux_clk_setup() - Setup function for simple fixed rate clock
 */
static void __init of_metag_mux_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	u32 shift, width, default_clock;
	void __iomem *reg;
	int len, i;
	struct property *prop;
	const char **parent_names;
	unsigned int num_parents;
	u8 flags = 0;
	unsigned long clk_flags = 0;

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

	/* count maximum number of parent clocks */
	prop = of_find_property(node, "clocks", &len);
	if (!prop) {
		pr_err("%s(%s): could not find clocks property\n",
		       __func__, clk_name);
		return;
	}
	/*
	 * There cannot be more parents than entries in "clocks" property (which
	 * may include additional args too. It also needs to fit in a u8.
	 */
	num_parents = len / sizeof(u32);
	num_parents = min(num_parents, 0xffu);

	/* allocate an array of parent names */
	parent_names = kzalloc(sizeof(const char *)*num_parents, GFP_KERNEL);
	if (!parent_names) {
		pr_err("%s(%s): could not allocate %u parent names\n",
		       __func__, clk_name, num_parents);
		goto err_kfree;
	}

	/* fill in the parent names */
	for (i = 0; i < num_parents; ++i) {
		parent_names[i] = of_clk_get_parent_name(node, i);
		if (!parent_names[i]) {
			/* truncate array length if we hit the end early */
			num_parents = i;
			break;
		}
	}

	/* default parent clock (mux value) */
	if (!of_property_read_u32(node, "default-clock", &default_clock)) {
		if (default_clock >= num_parents) {
			pr_err("%s(%s): default-clock %u out of range (%u bits)\n",
			       __func__, clk_name, default_clock, width);
			goto err_kfree;
		}
	} else {
		default_clock = -1;
	}

	reg = of_iomap(node, 0);
	if (!reg) {
		pr_err("%s(%s): of_iomap failed\n",
		       __func__, clk_name);
		goto err_kfree;
	}

	if (of_find_property(node, "linux,clk-set-rate-parent", NULL))
		clk_flags |= CLK_SET_RATE_PARENT;
	if (of_find_property(node, "linux,clk-set-rate-remux", NULL))
		clk_flags |= CLK_SET_RATE_REMUX;


	clk = clk_register_metag_mux(NULL, clk_name, parent_names, num_parents,
				     default_clock, clk_flags, reg, shift,
				     width, flags);
	if (IS_ERR(clk))
		goto err_iounmap;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);

	return;

err_iounmap:
	iounmap(reg);
err_kfree:
	kfree(parent_names);
}
CLK_OF_DECLARE(metag_mux_clk, "img,meta-mux-clock", of_metag_mux_clk_setup);
#endif /* CONFIG_OF */
