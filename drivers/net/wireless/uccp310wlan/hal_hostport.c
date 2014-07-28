/*HEADER**********************************************************************
******************************************************************************
***
*** Copyright (c) 2011, 2012, Imagination Technologies Ltd.
***
*** This program is free software; you can redistribute it and/or
*** modify it under the terms of the GNU General Public License
*** as published by the Free Software Foundation; either version 2
*** of the License, or (at your option) any later version.
***
*** This program is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*** GNU General Public License for more details.
***
*** You should have received a copy of the GNU General Public License
*** along with this program; if not, write to the Free Software
*** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
*** USA.
***
*** File Name  : hal_hostport.c
***
*** File Description:
*** This file contains the source functions of HAL IF for hostport+shared
*** memmory based communications
******************************************************************************
*END**************************************************************************/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/unaligned/access_ok.h>
#include <linux/netdevice.h>
#include <asm/core_reg.h>

#include "hal.h"
#include "hal_hostport.h"

#define COMMAND_START_MAGIC 0xDEAD

static struct hal_priv  *hpriv;
static const char *hal_name = "UCCP310_WIFI_HAL";

static unsigned long shm_offset = HAL_SHARED_MEM_OFFSET;
module_param(shm_offset, ulong, S_IRUSR|S_IWUSR);

#ifdef CONFIG_TIMING_DEBUG
unsigned long irq_timestamp[20];
unsigned int irq_ts_index;
spinlock_t  timing_lock;
#endif

int reset_hal_params(void)
{
	hpriv->cmd_cnt = COMMAND_START_MAGIC;
	hpriv->event_cnt = 0;
	return 0;
}

static int hal_ready(void)
{
	unsigned int value = 0;
	/* Check the ACK register bit */
	value =  readl((void __iomem *)(UCCP_CORE_HOST_TO_MTX_CMD_ADDR));
	if (value & BIT(UCCP_CORE_HOST_INT_SHIFT))
		return 0;
	else
		return 1;
}

static void tx_tasklet_fn(unsigned long data)
{
	struct hal_priv  *priv = (struct hal_priv *)data;
	struct sk_buff  *skb;
	unsigned int  value = 0;
	unsigned long start_addr;
	unsigned long timeout, timestamp;
	while ((skb = skb_dequeue(&priv->txq))) {
#ifdef CONFIG_HAL_DEBUG
		printk(KERN_DEBUG "%s: xmit dump\n", hal_name);
		print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, 1);
#endif

		timestamp = __core_reg_get(TXTIMER);
		timeout = timestamp + 100;
		while (!hal_ready()) {
			timestamp = __core_reg_get(TXTIMER);
			if (((long)timeout - (long)timestamp) < 0) {
				printk(KERN_DEBUG "%s: Interface not ready for 100 us,	dropping command. ID = %d\n", hal_name, skb->data[0]);
				dev_kfree_skb_any(skb);
				skb = NULL;
				break;
			}
		}
		if (!skb)
			continue;
		/* write the command buffer in bulk RAM */
		start_addr = readl((void __iomem *)HAL_BULK_RAM_CMD_START);
#ifdef CONFIG_HAL_DEBUG
		printk(KERN_DEBUG "%s: command address = 0x%08x\n", hal_name, (unsigned int)start_addr);
#endif
		start_addr -= HAL_MTX_BULK_RAM;
		start_addr += ((priv->bulk_ram_mem_addr)-(priv->shm_offset));

		if ((start_addr < priv->bulk_ram_mem_addr) || (start_addr > (priv->bulk_ram_mem_addr + HAL_WLAN_BULK_RAM_LEN))) {
			printk(KERN_DEBUG "%s: Invalid command address 0x%08x	in bulkram, dropping command.\n", hal_name, (unsigned int)start_addr);
			dev_kfree_skb_any(skb);
			skb = NULL;
			continue;
		}

		memcpy((unsigned char *)start_addr, skb->data, skb->len);
		value = (unsigned int) (priv->cmd_cnt);
		value |= 0x7fff0000;
		writel(value, (void __iomem *)(UCCP_CORE_HOST_TO_MTX_CMD_ADDR));
		priv->cmd_cnt++;
		dev_kfree_skb_any(skb);
	}
}

static void rx_tasklet_fn(unsigned long data)
{
	struct hal_priv *priv = (struct hal_priv *)data;
	struct sk_buff  *skb;
	unsigned char *buf;
	unsigned long temp;
	while ((skb = skb_dequeue(&priv->rxq))) {
		temp = *((unsigned long *)(skb->cb));
		buf = skb_put(skb, *(unsigned long *)(skb->cb + 4));
		memcpy(buf, (unsigned char *)temp, skb->len);
		/* Mark the buffer free */
		temp = *((unsigned long *)(skb->cb + 8));
#ifdef CONFIG_HAL_DEBUG
		printk(KERN_DEBUG "%s: Freeing event buffer at 0x%08x\n", hal_name, (unsigned int)temp);
#endif
		*((unsigned long *)temp) = 0;
#ifdef CONFIG_HAL_DEBUG
		printk(KERN_DEBUG "%s: recv dump\n", hal_name);
		print_hex_dump(KERN_DEBUG, " ", DUMP_PREFIX_NONE, 16, 1, skb->data, skb->len, 1);
#endif
		priv->rcv_handler(skb, LMAC_MOD_ID);
	}
}

static void hostport_send(struct hal_priv  *priv,
			struct sk_buff   *skb)
{
	skb_queue_tail(&priv->txq, skb);
	tasklet_schedule(&priv->tx_tasklet);
}

static void  hal_send(void      *nwb,
			unsigned char  rcv_mod_id,
			unsigned char  send_mod_id)
{
	hostport_send(hpriv, nwb);
}

static void hal_register_callback(msg_handler        handler,
				unsigned char      mod_id)
{
	hpriv->rcv_handler = handler;
}


static irqreturn_t hal_irq_handler(int    irq, void  *p)
{

	unsigned int         value;
	unsigned long        event_addr, event_status_addr, event_len;
	unsigned char        spurious;
	struct sk_buff       *skb;
	struct hal_priv      *priv = (struct hal_priv *)p;

	spurious = 0;

	value = readl((void __iomem *)(UCCP_CORE_MTX_TO_HOST_CMD_ADDR)) & 0x7fffffff;
	if (value == (0x7fff0000 | priv->event_cnt)) {
#ifdef CONFIG_TIMING_DEBUG
		spin_lock(&timing_lock);
		irq_timestamp[irq_ts_index] = __core_reg_get(TXTIMER);
		irq_ts_index = (irq_ts_index + 1)%20;
		spin_unlock(&timing_lock);
#endif
		event_addr = readl((void __iomem *)HAL_BULK_RAM_EVENT_START);
		event_status_addr = readl((void __iomem *)(HAL_BULK_RAM_EVENT_START + 4));
		event_len = readl((void __iomem *)(HAL_BULK_RAM_EVENT_START + 8));

#ifdef CONFIG_HAL_DEBUG
		printk(KERN_DEBUG "%s: event address = 0x%08x\n", hal_name, (unsigned int)event_addr);
		printk(KERN_DEBUG "%s: event status address = 0x%08x\n", hal_name, (unsigned int)event_status_addr);
		printk(KERN_DEBUG "%s: event len = %d\n", hal_name, (int)event_len);
#endif
		event_addr -= HAL_MTX_BULK_RAM;
		event_status_addr -= HAL_MTX_BULK_RAM;
		event_addr += ((priv->bulk_ram_mem_addr) - (priv->shm_offset));
		event_status_addr += ((priv->bulk_ram_mem_addr) - (priv->shm_offset));
		skb = dev_alloc_skb(event_len);
		if (!skb) {
			printk(KERN_ERR "%s: out of memory\n", hal_name);
			*((unsigned long *)event_status_addr) = 0;
		} else {
			*(unsigned long *)(skb->cb) = event_addr;		/* address of event payload */
			*(unsigned long *)(skb->cb + 4) = event_len;		/* length of event payload */
			*(unsigned long *)(skb->cb + 8) = event_status_addr;	/* address to mark free */
			skb_queue_tail(&priv->rxq, skb);
			tasklet_schedule(&priv->rx_tasklet);
		}
		priv->event_cnt++;
	} else {
			spurious = 1;
	}

	if (!spurious) {
		/* Clear the mtx interrupt */
		value = 0;
		value |= BIT(UCCP_CORE_MTX_INT_CLR_SHIFT);
		writel(*((unsigned long   *)&(value)), (void __iomem *)(UCCP_CORE_HOST_TO_MTX_ACK_ADDR));
	}

	return IRQ_HANDLED;
}

static void hal_enable_int(void  *p)
{
	unsigned int   value = 0;

	/* Set external pin irq enable for host_irq and mtx_irq */
	value = readl((void __iomem *)UCCP_CORE_INT_ENAB_ADDR);
	value |= BIT(UCCP_CORE_MTX_INT_IRQ_ENAB_SHIFT);
	writel(*((unsigned long   *)&(value)), (void __iomem *)(UCCP_CORE_INT_ENAB_ADDR));

	/* Enable raising mtx_int when MTX_INT = 1 */
	value = 0;
	value |= BIT(UCCP_CORE_MTX_INT_EN_SHIFT);
	writel(*((unsigned long *)&(value)), (void __iomem *)(UCCP_CORE_MTX_INT_ENABLE_ADDR));

	return;
}


static void hal_disable_int(void  *p)
{
	unsigned int   value = 0;

	/* Reset external pin irq enable for host_irq and mtx_irq */
	value = readl((void __iomem *)UCCP_CORE_INT_ENAB_ADDR);
	value &= ~(BIT(UCCP_CORE_MTX_INT_IRQ_ENAB_SHIFT));
	writel(*((unsigned long   *)&(value)), (void __iomem *)(UCCP_CORE_INT_ENAB_ADDR));

	/* Disable raising mtx_int when MTX_INT = 1 */
	value = 0;
	value &= ~(BIT(UCCP_CORE_MTX_INT_EN_SHIFT));
	writel(*((unsigned long *)&(value)), (void __iomem *)(UCCP_CORE_MTX_INT_ENABLE_ADDR));

	return;
}

static int hal_deinit(void)
{
	struct sk_buff *skb;

	_uccp310wlan_80211if_exit();

	/* Disable host_int and mtx_irq */
	hal_disable_int(NULL);

	/* Free irq line */
	free_irq(HAL_IRQ_LINE, hpriv);

	/* Kill the HAL tasklet */
	tasklet_kill(&hpriv->tx_tasklet);
	tasklet_kill(&hpriv->rx_tasklet);

	while ((skb = skb_dequeue(&hpriv->rxq)))
		dev_kfree_skb_any(skb);
	while ((skb = skb_dequeue(&hpriv->txq)))
		dev_kfree_skb_any(skb);

	/* unmap UCCP memory */
	iounmap((void __iomem *)hpriv->uccp_mem_addr);
	release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);

	/* unmap Bulk RAM */
	iounmap((void __iomem *)hpriv->bulk_ram_mem_addr);
	release_mem_region(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN);

	kfree(hpriv);
	return 0;
}

static int hal_init(void)
{
	hpriv = kzalloc(sizeof(struct hal_priv), GFP_KERNEL);
	if (!hpriv)
		return -ENOMEM;

	hpriv->shm_offset =  shm_offset;

	if (hpriv->shm_offset != HAL_SHARED_MEM_OFFSET)
		printk(KERN_DEBUG "%s: Using shared memory offset 0x%lx\n", hal_name, hpriv->shm_offset);

	/* Map UCCP memory */
	if (!(request_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN, "uccp"))) {
		printk(KERN_ERR "%s: request_mem_region failed	for UCCP region\n", hal_name);
		kfree(hpriv);
		return -ENOMEM;
	}

	hpriv->uccp_mem_addr = (unsigned long)ioremap(HAL_META_UCC_BASE,
					HAL_META_UCC_LEN);
	if (hpriv->uccp_mem_addr == 0) {
		printk(KERN_ERR "%s: Ioremap failed for UCCP mem region.\n", hal_name);
		release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);
		kfree(hpriv);
		return -ENOMEM;
	}

	/* Map Bulk RAM */
	if (!request_mem_region(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN, "wlan_bulk_ram")) {
		printk(KERN_ERR "%s: request_mem_region failed for bulk ram.\n", hal_name);
		release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);
		kfree(hpriv);
		return -ENOMEM;
	}

	hpriv->bulk_ram_mem_addr = (unsigned long)ioremap(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN);
	if (hpriv->bulk_ram_mem_addr == 0) {
		printk(KERN_ERR "%s: Ioremap failed for bulk ram region.\n", hal_name);
		release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);
		release_mem_region(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN);
		kfree(hpriv);
		return -ENOMEM;
	}


	/* Register irq handler */
	if (request_irq(HAL_IRQ_LINE, hal_irq_handler, 0, "wlan", hpriv) != 0) {
		printk(KERN_ERR "%s: Unable to register the Interrupt handler with kernel\n", hal_name);
		release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);
		release_mem_region(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN);
		kfree(hpriv);
		return -ENOMEM;
	}

	/* Enable host_int and mtx_int */
	hal_enable_int(NULL);

	/* Intialize HAL tasklets */
	tasklet_init(&hpriv->tx_tasklet, tx_tasklet_fn, (unsigned long)hpriv);
	tasklet_init(&hpriv->rx_tasklet, rx_tasklet_fn, (unsigned long)hpriv);
	skb_queue_head_init(&hpriv->rxq);
	skb_queue_head_init(&hpriv->txq);
#ifdef CONFIG_TIMING_DEBUG
	spin_lock_init(&timing_lock);
#endif

	if (_uccp310wlan_80211if_init() < 0) {
		printk(KERN_ERR "%s: wlan_init failed\n", hal_name);
		release_mem_region(HAL_META_UCC_BASE, HAL_META_UCC_LEN);
		release_mem_region(HAL_WLAN_BULK_RAM_START, HAL_WLAN_BULK_RAM_LEN);
		kfree(hpriv);
		return -ENOMEM;
	}

	hpriv->cmd_cnt = COMMAND_START_MAGIC;
	hpriv->event_cnt = 0;
	return 0;
}

struct hal_ops_tag  hal_ops = {
	.init = hal_init,
	.deinit = hal_deinit,
	.register_callback = hal_register_callback,
	.send = hal_send,
	/* Other ops can be implement if needed. */
};

static int __init hostport_init(void)
{
	return hal_ops.init();
}

static void __exit hostport_exit(void)
{
	hal_ops.deinit();
}

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Imagination Technologies");
MODULE_DESCRIPTION("Driver for IMG UCCP310 WiFi solution");

module_init(hostport_init);
module_exit(hostport_exit);
