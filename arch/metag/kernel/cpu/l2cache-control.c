/*
 * l2cache-control.c
 *
 * Meta Level 2 cache sysfs interface
 *
 * Copyright (C) 2011-2012 Imagination Technologies Ltd.
 * Written by James Hogan <james.hogan@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <asm/core-sysfs.h>
#include <asm/l2cache.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/kernel.h>

static ssize_t show_l2c_enabled(struct device *sysdev,
				struct device_attribute *attr, char *buf)
{
	ssize_t err;
	int val;

	val = !!meta_l2c_is_enabled();
	err = sprintf(buf, "%d\n", val);
	return err;
}

static ssize_t store_l2c_enabled(struct device *sysdev,
				 struct device_attribute *attr, const char *buf,
				 size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val) {
		pr_info("L2 Cache: Enabling... ");
		if (meta_l2c_enable())
			pr_cont("already enabled\n");
		else
			pr_cont("done\n");
	} else {
		pr_info("L2 Cache: Disabling... ");
		if (meta_l2c_disable())
			pr_cont("already disabled\n");
		else
			pr_cont("done\n");
	}

	return count;
}

static ssize_t show_l2c_prefetch(struct device *sysdev,
				 struct device_attribute *attr, char *buf)
{
	ssize_t err;
	int val;

	val = !!meta_l2c_pf_is_enabled();
	err = sprintf(buf, "%d\n", val);
	return err;
}

static ssize_t store_l2c_prefetch(struct device *sysdev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val) {
		pr_info("L2 Cache: Enabling prefetch... ");
		if (meta_l2c_pf_enable(1))
			pr_cont("already enabled\n");
		else
			pr_cont("done\n");
	} else {
		pr_info("L2 Cache: Disabling prefetch... ");
		if (!meta_l2c_pf_enable(0))
			pr_cont("already disabled\n");
		else
			pr_cont("done\n");
	}

	return count;
}

static ssize_t show_l2c_writeback(struct device *sysdev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t err;

	/* when read, we return whether the L2 is a writeback cache */
	err = sprintf(buf, "%d\n", !!meta_l2c_is_writeback());
	return err;
}

static ssize_t store_l2c_writeback(struct device *sysdev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		meta_l2c_writeback();

	return count;
}

static ssize_t show_l2c_flush(struct device *sysdev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%d\n", 0);
	return err;
}

static ssize_t store_l2c_flush(struct device *sysdev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	if (val)
		meta_l2c_flush();

	return count;
}

static ssize_t type_show(struct device *sysdev, struct device_attribute *attr,
			    char *buf)
{
	ssize_t err;
	const char *type;

	if (meta_l2c_is_unified()) {
		type = "Unified";
	} else {
		/*
		 * Should be "Instruction" or "Data" really, but we're
		 * representing the L2 cache as a whole.
		 */
		type = "Separate";
	}
	err = sprintf(buf, "%s\n", type);
	return err;
}

static ssize_t level_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%d\n", 2);
	return err;
}

static ssize_t size_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%uK\n", meta_l2c_size() >> 10);
	return err;
}

static ssize_t coherency_line_size_show(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%u\n", meta_l2c_linesize());
	return err;
}

static ssize_t number_of_sets_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	ssize_t err;
	unsigned int sets;

	sets = meta_l2c_size() / (meta_l2c_ways() * meta_l2c_linesize());
	err = sprintf(buf, "%u\n", sets);
	return err;
}

static ssize_t ways_of_associativity_show(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%u\n", meta_l2c_ways());
	return err;
}

static ssize_t revision_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	ssize_t err;

	err = sprintf(buf, "%u\n", meta_l2c_revision());
	return err;
}

static ssize_t config_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	ssize_t err;

	err = sprintf(buf, "0x%08x\n", meta_l2c_config());
	return err;
}

static struct device_attribute l2c_attrs[] = {
	/*
	 * These are fairly standard attributes, used by other architectures in
	 * /sys/devices/system/cpu/cpuX/cache/indexX/ (but on Meta they're
	 * elsewhere).
	 */
	__ATTR_RO(type),
	__ATTR_RO(level),
	__ATTR_RO(size),
	__ATTR_RO(coherency_line_size),
	__ATTR_RO(number_of_sets),
	__ATTR_RO(ways_of_associativity),

	/*
	 * Other read only attributes, specific to Meta.
	 */
	__ATTR_RO(revision),
	__ATTR_RO(config),

	/*
	 * These can be used to perform operations on the cache, such as
	 * enabling the cache and prefetch, and triggering a full writeback or
	 * flush.
	 */
	__ATTR(enabled,   0644, show_l2c_enabled, store_l2c_enabled),
	__ATTR(prefetch,  0644, show_l2c_prefetch, store_l2c_prefetch),
	__ATTR(writeback, 0644, show_l2c_writeback, store_l2c_writeback),
	__ATTR(flush,     0644, show_l2c_flush, store_l2c_flush),
};

static struct device device_cache_l2 = {
	.bus = &cache_subsys,
	.init_name = "l2",
};

static int __init meta_l2c_sysfs_init(void)
{
	int i, ret;

	if (!cache_subsys.name)
		return -EINVAL;

	/* if there's no L2 cache, don't add the sysfs nodes */
	if (!meta_l2c_is_present())
		return 0;

	ret = device_register(&device_cache_l2);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(l2c_attrs); i++) {
		ret = device_create_file(&device_cache_l2,
					 &l2c_attrs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
device_initcall(meta_l2c_sysfs_init);
