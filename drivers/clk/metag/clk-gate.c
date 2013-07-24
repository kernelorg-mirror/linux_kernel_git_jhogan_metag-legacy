/*
 * Copyright (C) 2013 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Metag gated clock implementation
 * Based on gated clock implementation, but does appropriate locking to protect
 * registers shared between hardware threads.
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <asm/global_lock.h>

/**
 * struct clk_metag_gate - metag gating clock
 *
 * @mux:	the parent class
 * @ops:	pointer to clk_ops of parent class
 *
 * Clock which can gate its output. Extends basic mux by using a global
 * exclusive lock when read-modify-writing the mux field so that multiple
 * threads/cores can use different fields in the same register.
 */
struct clk_metag_gate {
	struct clk_gate		gate;
	const struct clk_ops	*ops;
};

static inline struct clk_metag_gate *to_clk_metag_gate(struct clk_hw *hw)
{
	struct clk_gate *gate = container_of(hw, struct clk_gate, hw);

	return container_of(gate, struct clk_metag_gate, gate);
}

/* Acquire exclusive lock since other cores may access the same register */
static int clk_metag_gate_enable(struct clk_hw *hw)
{
	struct clk_metag_gate *gate = to_clk_metag_gate(hw);
	int ret;
	unsigned long flags;

	__global_lock2(flags);
	ret = gate->ops->enable(&gate->gate.hw);
	__global_unlock2(flags);

	return ret;
}

/* Acquire exclusive lock since other cores may access the same register */
static void clk_metag_gate_disable(struct clk_hw *hw)
{
	struct clk_metag_gate *gate = to_clk_metag_gate(hw);
	unsigned long flags;

	__global_lock2(flags);
	gate->ops->disable(&gate->gate.hw);
	__global_unlock2(flags);
}

static int clk_metag_gate_is_enabled(struct clk_hw *hw)
{
	struct clk_metag_gate *gate = to_clk_metag_gate(hw);

	return gate->ops->is_enabled(&gate->gate.hw);
}

static const struct clk_ops clk_metag_gate_ops = {
	.enable = clk_metag_gate_enable,
	.disable = clk_metag_gate_disable,
	.is_enabled = clk_metag_gate_is_enabled,
};

/**
 * clk_register_metag_gate - register a Meta gate clock with the clock framework
 * @dev: device that is registering this clock
 * @name: name of this clock
 * @parent_name: name of this clock's parent
 * @flags: framework-specific flags for this clock
 * @reg: register address to control gating of this clock
 * @bit_idx: which bit in the register controls gating of this clock
 * @clk_gate_flags: gate-specific flags for this clock
 */
static struct clk *__init clk_register_metag_gate(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		void __iomem *reg, u8 bit_idx, u8 clk_gate_flags)
{
	struct clk_metag_gate *gate;
	struct clk *clk;
	struct clk_init_data init;

	/* allocate the gate */
	gate = kzalloc(sizeof(struct clk_metag_gate), GFP_KERNEL);
	if (!gate) {
		pr_err("%s: could not allocate gated clk\n", __func__);
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &clk_metag_gate_ops;
	init.flags = flags | CLK_IS_BASIC;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);

	/* struct clk_gate assignments */
	gate->gate.reg = reg;
	gate->gate.bit_idx = bit_idx;
	gate->gate.flags = clk_gate_flags;
	gate->gate.hw.init = &init;

	/* struct clk_metag_gate assignments */
	gate->ops = &clk_gate_ops;

	clk = clk_register(dev, &gate->gate.hw);

	if (IS_ERR(clk))
		kfree(gate);

	return clk;
}

#ifdef CONFIG_OF
/**
 * of_metag_gate_clk_setup() - Setup function for simple fixed rate clock
 */
static void __init of_metag_gate_clk_setup(struct device_node *node)
{
	struct clk *clk;
	const char *clk_name = node->name;
	u32 bit_idx;
	void __iomem *reg;
	const char *parent_name;
	u8 flags = 0;

	of_property_read_string(node, "clock-output-names", &clk_name);

	if (of_property_read_u32(node, "bit", &bit_idx)) {
		pr_err("%s(%s): could not read bit property\n",
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

	clk = clk_register_metag_gate(NULL, clk_name, parent_name,
				      CLK_SET_RATE_PARENT, reg, bit_idx, flags);
	if (IS_ERR(clk))
		goto err_iounmap;

	of_clk_add_provider(node, of_clk_src_simple_get, clk);
	clk_register_clkdev(clk, clk_name, NULL);

	return;

err_iounmap:
	iounmap(reg);
}
CLK_OF_DECLARE(metag_gate_clk, "img,meta-gate-clock", of_metag_gate_clk_setup);
#endif /* CONFIG_OF */
