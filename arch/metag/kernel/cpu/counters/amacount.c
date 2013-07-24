/*
 * linux/arch/metag/drivers/amacount.c
 *
 * Meta core Automatic MIPs Allocation (AMA) sysfs interface
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

#include <asm/core_reg.h>
#include <asm/core-sysfs.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/io.h>

/* Control unit registers for AMA */
static unsigned int cu_ama_regs[] = {
	TXAMAREG0_REGNUM,
	TXAMAREG1_REGNUM,
	TXAMAREG2_REGNUM,
	TXAMAREG3_REGNUM,
};

#define CU_AMA_REG(thread, reg)					\
	(T0UCTREG0 + (TnUCTRX_STRIDE * thread) +		\
	 (TXUCTREGn_STRIDE * cu_ama_regs[reg]))

/* Memory mapped registers for AMA */
static unsigned int mmap_ama_regs[] = {
	T0AMAREG4,
	T0AMAREG5,
	T0AMAREG6,
};

#define MMAP_AMA_REG(thread, reg)				\
	((TnAMAREGX_STRIDE * thread) + mmap_ama_regs[reg])

enum cu_reg_num {reg0, reg1, reg2, reg3};
enum mmap_reg_num {reg4, reg5, reg6};

static ssize_t show_cu_amareg(unsigned int thread,
	enum cu_reg_num reg, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(CU_AMA_REG(thread, reg));
	err = sprintf(buf, "%u\n", val);

	return err;
}

static ssize_t store_cu_amareg(unsigned int thread,
	enum mmap_reg_num reg, const char *buf, size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	metag_out32(val, CU_AMA_REG(thread, reg));

	return count;
}

#define SYSFS_CUREG_SETUP(REG) \
static ssize_t show_##REG(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
{ \
	return show_cu_amareg(dev->id, REG, buf); \
} \
static ssize_t store_##REG(struct device *dev, \
			   struct device_attribute *attr, const char *buf, \
			   size_t count) \
{ \
	return store_cu_amareg(dev->id, REG, buf, count); \
}

static ssize_t show_mmap_amareg(unsigned int thread,
	enum mmap_reg_num reg, char *buf)
{
	ssize_t err;
	u32 val;

	val = metag_in32(MMAP_AMA_REG(thread, reg));
	err = sprintf(buf, "%u\n", val);

	return err;
}

static ssize_t store_mmap_amareg(unsigned int thread,
				 enum mmap_reg_num reg, const char *buf,
				 size_t count)
{
	unsigned long val;

	if (kstrtoul(buf, 10, &val))
		return -EINVAL;

	metag_out32(val, MMAP_AMA_REG(thread, reg));

	return count;
}

#define SYSFS_MMAPREG_SETUP(REG) \
static ssize_t show_##REG(struct device *dev, \
			  struct device_attribute *attr, char *buf) \
{ \
	return show_mmap_amareg(dev->id, REG, buf); \
} \
static ssize_t store_##REG(struct device *dev, \
			   struct device_attribute *attr, \
			   const char *buf, size_t count) \
{ \
	return store_mmap_amareg(dev->id, REG, buf, count); \
}

SYSFS_CUREG_SETUP(reg0);
SYSFS_CUREG_SETUP(reg1);
SYSFS_CUREG_SETUP(reg2);
SYSFS_CUREG_SETUP(reg3);

SYSFS_MMAPREG_SETUP(reg4);
SYSFS_MMAPREG_SETUP(reg5);
SYSFS_MMAPREG_SETUP(reg6);

static struct device_attribute cu_ama_attrs[] = {
	__ATTR(amareg0, 0644, show_reg0, store_reg0),
	__ATTR(amareg1, 0644, show_reg1, store_reg1),
	__ATTR(amareg2, 0644, show_reg2, store_reg2),
	__ATTR(amareg3, 0644, show_reg3, store_reg3),
};

static struct device_attribute mmap_ama_attrs[] = {
	__ATTR(amareg4, 0644, show_reg4, store_reg4),
	__ATTR(amareg5, 0644, show_reg5, store_reg5),
	__ATTR(amareg6, 0644, show_reg6, store_reg6),
};

static struct device device_ama = {
	.bus = &performance_subsys,
	.init_name = "ama",
};

static struct device device_ama_threads[4] = {
	{
		.id = 0,
		.bus = &performance_subsys,
		.parent = &device_ama,
		.init_name = "thread0",
	},
	{
		.id = 1,
		.bus = &performance_subsys,
		.parent = &device_ama,
		.init_name = "thread1",
	},
	{
		.id = 2,
		.bus = &performance_subsys,
		.parent = &device_ama,
		.init_name = "thread2",
	},
	{
		.id = 3,
		.bus = &performance_subsys,
		.parent = &device_ama,
		.init_name = "thread3",
	},
};

static int __init meta_amacount_init(void)
{
	int i, thread, exists, ret;

	if (!performance_subsys.name)
		return -EINVAL;

	ret = device_register(&device_ama);
	if (ret)
		return ret;

	for (thread = 0; thread < 4; thread++) {
		exists = core_reg_read(TXUCT_ID, TXENABLE_REGNUM, thread);
		if (!exists)
			break;

		ret = device_register(&device_ama_threads[thread]);
		if (ret)
			return ret;

		for (i = 0; i < ARRAY_SIZE(cu_ama_attrs); i++) {
			ret = device_create_file(&device_ama_threads[thread],
						 &cu_ama_attrs[i]);
			if (ret)
				return ret;
		}
		for (i = 0; i < ARRAY_SIZE(mmap_ama_attrs); i++) {
			ret = device_create_file(&device_ama_threads[thread],
						 &mmap_ama_attrs[i]);
			if (ret)
				return ret;
		}
	}

	return 0;
}
device_initcall(meta_amacount_init);
