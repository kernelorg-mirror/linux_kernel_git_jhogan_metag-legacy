/*
 * linux/arch/metag/drivers/write_combiner.c
 *
 * Meta memory arbiter sysfs interface
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
#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#define MEMARBITER_REG(thread)						\
	(EXPAND_T0ARBITER + (EXPAND_TnARBITER_STRIDE * thread))

enum thread_id {
	thread0,
	thread1,
	thread2,
	thread3
};

static ssize_t show_memarbiter(enum thread_id thread, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(MEMARBITER_REG(thread));
	err = sprintf(buf, "%u\n", val);
	return err;
}

static void store_memarbiter(enum thread_id thread, const char *buf)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return;

	metag_out32(val, MEMARBITER_REG(thread));
}

#define SYSFS_MEMARBITER_SETUP(NAME) \
static ssize_t show_##NAME##_ma(struct device *dev,  \
				struct device_attribute *attr, char *buf) \
{ \
	return show_memarbiter(NAME, buf); \
} \
static ssize_t store_##NAME##_ma(struct device *dev, \
				struct device_attribute *attr, \
				const char *buf, size_t count) \
{ \
	store_memarbiter(NAME, buf); \
	return count; \
}

SYSFS_MEMARBITER_SETUP(thread0);
SYSFS_MEMARBITER_SETUP(thread1);
SYSFS_MEMARBITER_SETUP(thread2);
SYSFS_MEMARBITER_SETUP(thread3);

static DEVICE_ATTR(thread0, 0644, show_thread0_ma, store_thread0_ma);
static DEVICE_ATTR(thread1, 0644, show_thread1_ma, store_thread1_ma);
static DEVICE_ATTR(thread2, 0644, show_thread2_ma, store_thread2_ma);
static DEVICE_ATTR(thread3, 0644, show_thread3_ma, store_thread3_ma);

static struct attribute *memory_arbiter_root_attrs[] = {
	&dev_attr_thread0.attr,
	&dev_attr_thread1.attr,
	&dev_attr_thread2.attr,
	&dev_attr_thread3.attr,
	NULL,
};

static struct attribute_group memory_arbiter_root_attr_group = {
	.attrs = memory_arbiter_root_attrs,
};

static const struct attribute_group *memory_arbiter_root_attr_groups[] = {
	&memory_arbiter_root_attr_group,
	NULL,
};

struct bus_type memory_arbiter_subsys = {
	.name = "memory_arbiter",
	.dev_name = "ma",
};

static int __init meta_memarbiter_init(void)
{
	int i, exists, ret;

	/* modify number of threads displayed */
	for (i = 0; i < ARRAY_SIZE(memory_arbiter_root_attrs); i++) {
		exists = core_reg_read(TXUCT_ID, TXENABLE_REGNUM, i);
		if (!exists) {
			memory_arbiter_root_attrs[i] = NULL;
			break;
		}
	}

	ret = subsys_system_register(&memory_arbiter_subsys,
				     memory_arbiter_root_attr_groups);
	return ret;
}
device_initcall(meta_memarbiter_init);
