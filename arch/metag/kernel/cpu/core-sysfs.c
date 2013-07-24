/*
 * linux/arch/metag/drivers/core-sysfs.c
 *
 * Meta core sysfs interface, including cycle counters, perf counters and AMA
 * configuration registers.
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

#include <asm/core-sysfs.h>
#include <linux/device.h>
#include <linux/init.h>

struct bus_type performance_subsys = {
	.name = "performance",
	.dev_name = "counter",
};

struct bus_type cache_subsys = {
	.name = "cache",
	.dev_name = "cache",
};

static int __init meta_core_sysfs_init(void)
{
	int err, ret = 0;

	err = subsys_system_register(&performance_subsys, NULL);
	if (err) {
		performance_subsys.name = NULL;
		ret = err;
	}

	err = subsys_system_register(&cache_subsys, NULL);
	if (err) {
		cache_subsys.name = NULL;
		ret = err;
	}

	return ret;
}
arch_initcall(meta_core_sysfs_init);
