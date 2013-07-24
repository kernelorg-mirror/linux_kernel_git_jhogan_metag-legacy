/*
 * linux/arch/metag/drivers/write_combiner.c
 *
 * Meta write combiner sysfs interface
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN
 * NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * Make sure we include these first with the TXUXXRX values available,
 * or else we cannot get hold of them later on after somebody else has
 * included them from the arch headers.
 */

#include <asm/core_reg.h>
#include <asm/core-sysfs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#define WRCOMBINER_REG(thread)						\
	(EXPAND_T0WRCOMBINE + (EXPAND_TnWRCOMBINE_STRIDE * thread))

enum thread_id {
	thread0,
	thread1,
	thread2,
	thread3
};

static ssize_t show_wrcombiner(enum thread_id thread, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(WRCOMBINER_REG(thread));
	err = sprintf(buf, "%u\n", val);
	return err;
}

static ssize_t store_wrcombiner(enum thread_id thread, const char *buf,
				size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	metag_out32(val, WRCOMBINER_REG(thread));

	return count;
}

#define SYSFS_WRCOMBINER_SETUP(NAME) \
static ssize_t show_##NAME##_wc(struct device *dev,  \
				struct device_attribute *attr, char *buf) \
{ \
	return show_wrcombiner(NAME, buf); \
} \
static ssize_t store_##NAME##_wc(struct device *dev, \
				 struct device_attribute *attr, \
				 const char *buf, size_t count) \
{ \
	return store_wrcombiner(NAME, buf, count); \
}

static ssize_t show_perfchan0(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(EXPAND_PERFCHAN0);
	err = sprintf(buf, "%u\n", val);
	return err;
}

static ssize_t store_perfchan0(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u32 read_val;
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	read_val = metag_in32(EXPAND_PERFCHAN0) & ~EXPPERF_CTRL_BITS;
	metag_out32(read_val | val, EXPAND_PERFCHAN0);
	return count;
}

static ssize_t show_perfchan1(struct device *sysdev,
			      struct device_attribute *attr, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(EXPAND_PERFCHAN1);
	err = sprintf(buf, "%u\n", val);
	return err;
}

static ssize_t store_perfchan1(struct device *sysdev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	u32 read_val;
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	read_val = metag_in32(EXPAND_PERFCHAN1) & ~EXPPERF_CTRL_BITS;
	metag_out32(read_val | val, EXPAND_PERFCHAN1);
	return count;
}

SYSFS_WRCOMBINER_SETUP(thread0);
SYSFS_WRCOMBINER_SETUP(thread1);
SYSFS_WRCOMBINER_SETUP(thread2);
SYSFS_WRCOMBINER_SETUP(thread3);

static DEVICE_ATTR(thread0, 0644, show_thread0_wc, store_thread0_wc);
static DEVICE_ATTR(thread1, 0644, show_thread1_wc, store_thread1_wc);
static DEVICE_ATTR(thread2, 0644, show_thread2_wc, store_thread2_wc);
static DEVICE_ATTR(thread3, 0644, show_thread3_wc, store_thread3_wc);

static struct attribute *write_combiner_root_attrs[] = {
	&dev_attr_thread0.attr,
	&dev_attr_thread1.attr,
	&dev_attr_thread2.attr,
	&dev_attr_thread3.attr,
	NULL,
};

static struct attribute_group write_combiner_root_attr_group = {
	.attrs = write_combiner_root_attrs,
};

static const struct attribute_group *write_combiner_root_attr_groups[] = {
	&write_combiner_root_attr_group,
	NULL,
};

static struct device_attribute perfchan_attrs[] = {
	__ATTR(perfchan0, 0644, show_perfchan0, store_perfchan0),
	__ATTR(perfchan1, 0644, show_perfchan1, store_perfchan1),
};

struct bus_type write_combiner_subsys = {
	.name = "write_combiner",
	.dev_name = "wc",
};

static struct device device_perf_write_combiner = {
	.bus = &performance_subsys,
	.init_name = "write_combiner",
};

static int __init meta_writecombiner_init(void)
{
	int i, exists, ret;

	/* modify number of threads displayed */
	for (i = 0; i < ARRAY_SIZE(write_combiner_root_attrs); i++) {
		exists = core_reg_read(TXUCT_ID, TXENABLE_REGNUM, i);
		if (!exists) {
			write_combiner_root_attrs[i] = NULL;
			break;
		}
	}

	ret = subsys_system_register(&write_combiner_subsys,
				     write_combiner_root_attr_groups);
	if (ret)
		return ret;

	if (!performance_subsys.name)
		return -EINVAL;

	ret = device_register(&device_perf_write_combiner);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(perfchan_attrs); i++) {
		ret = device_create_file(&device_perf_write_combiner,
					 &perfchan_attrs[i]);
		if (ret)
			return ret;
	}

	return 0;
}
device_initcall(meta_writecombiner_init);
