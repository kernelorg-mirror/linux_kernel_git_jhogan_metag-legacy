/*
 * linux/arch/metag/drivers/cyclecount.c
 *
 * Meta core cycle counter sysfs interface
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

#define IDLE_COUNTER()	(T0UCTREG0 + TXUCTREGn_STRIDE * TXIDLECYC_REGNUM)

#define CYCLE_COUNTER(thread)					\
	(T0UCTREG0 + (TnUCTRX_STRIDE * thread) +		\
	 (TXUCTREGn_STRIDE * TXTACTCYC_REGNUM))

enum thread_id {
	thread0,
	thread1,
	thread2,
	thread3,
	idle
};

static ssize_t show_cycles(enum thread_id thread, char *buf)
{
	int err = -EINVAL;
	unsigned int cycles;

	switch (thread) {
	case thread0:
	case thread1:
	case thread2:
	case thread3:
		cycles = metag_in32(CYCLE_COUNTER(thread));
		metag_out32(0, CYCLE_COUNTER(thread));
		err = sprintf(buf, "%u\n", cycles);
		break;
	case idle:
		cycles = metag_in32(IDLE_COUNTER());
		metag_out32(0, IDLE_COUNTER());
		err = sprintf(buf, "%u\n", cycles);
	}

	return err;
}

#define SYSFS_CYCLE_SETUP(NAME) \
static ssize_t show_##NAME(struct device *dev,  \
			   struct device_attribute *attr, char *buf) \
{ \
	return show_cycles(NAME, buf); \
}

SYSFS_CYCLE_SETUP(thread0);
SYSFS_CYCLE_SETUP(thread1);
SYSFS_CYCLE_SETUP(thread2);
SYSFS_CYCLE_SETUP(thread3);
SYSFS_CYCLE_SETUP(idle);

static struct device_attribute thread_attrs[] = {
	__ATTR(thread0, 0444, show_thread0, NULL),
	__ATTR(thread1, 0444, show_thread1, NULL),
	__ATTR(thread2, 0444, show_thread2, NULL),
	__ATTR(thread3, 0444, show_thread3, NULL),
};

static DEVICE_ATTR(idle, 0444, show_idle, NULL);

static struct device device_perf_cycles = {
	.bus = &performance_subsys,
	.init_name = "cycles",
};

static int __init meta_cyclecount_init(void)
{
	int i, exists, ret;

	if (!performance_subsys.name)
		return -EINVAL;

	ret = device_register(&device_perf_cycles);
	if (ret)
		return ret;


	/* We always have an idle counter */
	ret = device_create_file(&device_perf_cycles, &dev_attr_idle);
	if (ret)
		return ret;

	/* Check for up to four threads */
	for (i = 0; i < ARRAY_SIZE(thread_attrs); i++) {
		exists = core_reg_read(TXUCT_ID, TXENABLE_REGNUM, i);
		if (exists) {
			ret = device_create_file(&device_perf_cycles,
						 &thread_attrs[i]);
			if (ret)
				return ret;
		}
	}

	return 0;
}
device_initcall(meta_cyclecount_init);
