/*
 * linux/arch/metag/drivers/perfcount.c
 *
 * Meta core performance counter sysfs interface
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

#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <asm/core-sysfs.h>
#include <asm/metag_mem.h>

static int counter_map[] = {
	PERF_COUNT0,
	PERF_COUNT1
};

static ssize_t show_counter(struct device *dev, struct device_attribute *attr,
			    char *buf)
{
	u32 perf, val;

	val = metag_in32(counter_map[dev->id]);
	perf = val & PERF_COUNT_BITS;
	metag_out32(val & ~PERF_COUNT_BITS, counter_map[dev->id]);

	return sprintf(buf, "%u\n", perf);
}

static ssize_t show_mask(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	u32 mask;

	mask = metag_in32(counter_map[dev->id]) & PERF_THREAD_BITS;
	return sprintf(buf, "%u\n", mask);
}

static ssize_t store_mask(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned long val;
	u32 read_val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	read_val = metag_in32(counter_map[dev->id]) & ~PERF_THREAD_BITS;
	val <<= PERF_THREAD_S;
	metag_out32(read_val | val, counter_map[dev->id]);

	return count;
}

static ssize_t show_ctrl(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	u32 ctrl;

	ctrl = metag_in32(counter_map[dev->id]) & PERF_CTRL_BITS;
	return sprintf(buf, "%u\n", ctrl);
}

static ssize_t store_ctrl(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	unsigned long val;
	u32 read_val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	read_val = metag_in32(counter_map[dev->id]) & ~PERF_CTRL_BITS;
	val <<= PERF_CTRL_S;
	metag_out32(read_val | val, counter_map[dev->id]);

	return count;
}

static struct device_attribute perf_attrs[] = {
	__ATTR(counter,	0644, show_counter, NULL),
	__ATTR(mask,	0644, show_mask, store_mask),
	__ATTR(ctrl,	0644, show_ctrl, store_ctrl),
};

static struct device device_perfcount = {
	.bus = &performance_subsys,
	.init_name = "perfcount",
};

static struct device device_perf_counters[] = {
	{
		.id = 0,
		.parent = &device_perfcount,
		.bus = &performance_subsys,
	},
	{
		.id = 1,
		.parent = &device_perfcount,
		.bus = &performance_subsys,
	},
};

static int __init meta_perfcount_init(void)
{
	int i, j, ret;

	if (!performance_subsys.name)
		return -EINVAL;

	ret = device_register(&device_perfcount);
	if (ret)
		return ret;

	for (i = 0; i < ARRAY_SIZE(device_perf_counters); ++i) {
		ret = device_register(&device_perf_counters[i]);
		if (ret)
			return ret;

		for (j = 0; j < ARRAY_SIZE(perf_attrs); ++j) {
			ret = device_create_file(&device_perf_counters[i],
						 &perf_attrs[j]);
			if (ret)
				return ret;
		}
	}

	return 0;
}
device_initcall(meta_perfcount_init);
