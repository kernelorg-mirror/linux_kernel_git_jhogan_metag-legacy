/*
 * IMG Meta DMA Controller (MDC) specific DMA code.
 *
 * Copyright (C) 2009,2012,2013 Imagination Technologies Ltd.
 */

#ifndef __MDC_API_H__
#define __MDC_API_H__

struct dma_chan;
struct dma_slave_config;
struct platform_device;

enum mdc_dma_tx_flags {
	MDC_PRIORITY = (1 << 0),
	MDC_NO_CALLBACK = (1 << 1),
	MDC_ACCESS_DELAY = (1 << 2),
	MDC_NEED_THREAD = (1 << 3),
};

enum mdc_dma_thread_type {
	MDC_THREAD_FAST = (1 << 0),
	MDC_THREAD_SLOW = (1 << 1),
};

struct mdc_dma_tx_control {
	enum mdc_dma_tx_flags flags;
	unsigned int prio;
	unsigned int access_delay;
	enum mdc_dma_thread_type thread_type;
};

/*
 * MDC DMA cookie
 * @req_channel: Channel to request or -1 for the first available one.
 * On return, it contains the channel that will be allocated by the MDC
 * DMA device
 * @periph: Number of peripheral device requesting the channel.
 */
struct mdc_dma_cookie {
	int req_channel;
	unsigned int periph;
};

/* Platform data for SOC DMA callbacks */
struct img_mdc_soc_callbacks {
	int (*allocate) (int, unsigned int); /* allocate a DMA channel */
	int (*free) (int); /* free a DMA channel */
	/*
	 * SOC DMA specific callbacks for suspend_noirq and resume_noirq.
	 * Both executed in atomic context.
	 */
	void *(*suspend) (void);
	void (*resume) (void *);
};

bool mdc_dma_filter_fn(struct dma_chan *, void *);

int mdc_dma_probe(struct platform_device *pdev,
		  const struct img_mdc_soc_callbacks *c);
/* Legacy API */
#include <linux/dma-mapping.h>

#define MDC_CONTEXT_OFFSET		(0x40)

/* -------------------- Register MDC_GENERAL_CONFIG -------------------- */

#define _MDC_GENERAL_CONFIG_OFFSET	(0x000)

#define _MDC_LIST_IEN_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_LIST_IEN_SHIFT		(31)
#define _MDC_LIST_IEN_MASK		(0x80000000)
#define _MDC_LIST_IEN_LENGTH		(1)

#define _MDC_BSWAP_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_BSWAP_SHIFT		(30)
#define _MDC_BSWAP_MASK			(0x40000000)
#define _MDC_BSWAP_LENGTH		(1)

#define _MDC_IEN_OFFSET			_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_IEN_SHIFT			(29)
#define _MDC_IEN_MASK			(0x20000000)
#define _MDC_IEN_LENGTH			(1)

#define _MDC_LEVEL_INT_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_LEVEL_INT_SHIFT		(28)
#define _MDC_LEVEL_INT_MASK		(0x10000000)
#define _MDC_LEVEL_INT_LENGTH		(1)

#define _MDC_CHANNEL_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_CHANNEL_SHIFT		(20)
#define _MDC_CHANNEL_MASK		(0x03F00000)
#define _MDC_CHANNEL_LENGTH		(6)

#define _MDC_ACC_DEL_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_ACC_DEL_SHIFT		(16)
#define _MDC_ACC_DEL_MASK		(0x00070000)
#define _MDC_ACC_DEL_LENGTH		(1)

#define _MDC_WAIT_UNPACK_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_WAIT_UNPACK_SHIFT		(13)
#define _MDC_WAIT_UNPACK_MASK		(0x00002000)
#define _MDC_WAIT_UNPACK_LENGTH		(1)

#define _MDC_INC_W_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_INC_W_SHIFT		(12)
#define _MDC_INC_W_MASK			(0x00001000)
#define _MDC_INC_W_LENGTH		(1)

#define _MDC_WAIT_PACK_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_WAIT_PACK_SHIFT		(9)
#define _MDC_WAIT_PACK_MASK		(0x00000200)
#define _MDC_WAIT_PACK_LENGTH		(1)

#define _MDC_INC_R_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_INC_R_SHIFT		(8)
#define _MDC_INC_R_MASK			(0x00000100)
#define _MDC_INC_R_LENGTH		(1)

#define _MDC_PHYSICAL_W_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_PHYSICAL_W_SHIFT		(7)
#define _MDC_PHYSICAL_W_MASK		(0x00000080)
#define _MDC_PHYSICAL_W_LENGTH		(1)

#define _MDC_WIDTH_W_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_WIDTH_W_SHIFT		(4)
#define _MDC_WIDTH_W_MASK		(0x00000070)
#define _MDC_WIDTH_W_LENGTH		(3)

#define _MDC_PHYSICAL_R_OFFSET		MDC_GENERAL_CONFIG_OFFSET
#define _MDC_PHYSICAL_R_SHIFT		(3)
#define _MDC_PHYSICAL_R_MASK		(0x00000008)
#define _MDC_PHYSICAL_R_LENGTH		(1)

#define _MDC_WIDTH_R_OFFSET		_MDC_GENERAL_CONFIG_OFFSET
#define _MDC_WIDTH_R_SHIFT		(0)
#define _MDC_WIDTH_R_MASK		(0x00000007)
#define _MDC_WIDTH_R_LENGTH		(3)

/* -------------------- Register MDC_READ_PORT_CONFIG -------------------- */

#define _MDC_READ_PORT_CONFIG_OFFSET	(0x004)

#define _MDC_STHREAD_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_STHREAD_SHIFT		(28)
#define _MDC_STHREAD_MASK		(0xF0000000)
#define _MDC_STHREAD_LENGTH		(4)

#define _MDC_RTHREAD_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_RTHREAD_SHIFT		(24)
#define _MDC_RTHREAD_MASK		(0x0F000000)
#define _MDC_RTHREAD_LENGTH		(4)

#define _MDC_PRIORITY_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_PRIORITY_SHIFT		(20)
#define _MDC_PRIORITY_MASK		(0x00F00000)
#define _MDC_PRIORITY_LENGTH		(4)

#define _MDC_WTHREAD_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_WTHREAD_SHIFT		(16)
#define _MDC_WTHREAD_MASK		(0x000F0000)
#define _MDC_WTHREAD_LENGTH		(4)

#define _MDC_HOLD_OFF_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_HOLD_OFF_SHIFT		(12)
#define _MDC_HOLD_OFF_MASK		(0x0000F000)
#define _MDC_HOLD_OFF_LENGTH		(4)

#define _MDC_BURST_SIZE_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_BURST_SIZE_SHIFT		(4)
#define _MDC_BURST_SIZE_MASK		(0x00000FF0)
#define _MDC_BURST_SIZE_LENGTH		(8)

#define _MDC_DREQ_ENABLE_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_DREQ_ENABLE_SHIFT		(1)
#define _MDC_DREQ_ENABLE_MASK		(0x00000002)
#define _MDC_DREQ_ENABLE_LENGTH		(1)

#define _MDC_READBACK_OFFSET		_MDC_READ_PORT_CONFIG_OFFSET
#define _MDC_READBACK_SHIFT		(0)
#define _MDC_READBACK_MASK		(0x00000001)
#define _MDC_READBACK_LENGTH		(1)

/* -------------------- Register MDC_READ_ADDRESS -------------------- */

#define _MDC_READ_ADDRESS_OFFSET	(0x008)

#define _MDC_ADDR_R_OFFSET		_MDC_READ_ADDRESS_OFFSET
#define _MDC_ADDR_R_SHIFT		(0)
#define _MDC_ADDR_MASK			(0xFFFFFFFF)
#define _MDC_ADDR_LENGTH		(32)

/* -------------------- Register MDC_WRITE_ADDRESS -------------------- */

#define _MDC_WRITE_ADDRESS_OFFSET	(0x00C)

#define _MDC_ADDR_W_OFFSET		_MDC_WRITE_ADDRESS_OFFSET
#define _MDC_ADDR_W_SHIFT		(0)
#define _MDC_ADDR_W_MASK		(0xFFFFFFFF)
#define _MDC_ADDR_W_LENGTH		(32)

/* -------------------- Register MDC_TRANSFER_SIZE -------------------- */

#define _MDC_TRANSFER_SIZE_OFFSET	(0x010)

#define _MDC_CNT_OFFSET			_MDC_TRANSFER_SIZE_OFFSET
#define _MDC_CNT_SHIFT			(0)
#define _MDC_CNT_MASK			(0x00FFFFFF)
#define _MDC_CNT_LENGTH			(24)

/* -------------------- Register MDC_LIST_NODE_ADDRESS -------------------- */

#define _MDC_LIST_NODE_ADDRESS_OFFSET	(0x014)

#define _MDC_ADDR_S_OFFSET		_MDC_LIST_NODE_ADDRESS_OFFSET
#define _MDC_ADDR_S_SHIFT		(0)
#define _MDC_ADDR_S_MASK		(0xFFFFFFFF)
#define _MDC_ADDR_S_LENGTH		(32)

/* -------------------- Register MDC_CMDS_PROCESSED -------------------- */

#define _MDC_CMDS_PROCESSED_OFFSET	(0x018)

#define _MDC_CMD_PROCESSED_OFFSET	_MDC_CMDS_PROCESSED_OFFSET
#define _MDC_CMD_PROCESSED_SHIFT	(16)
#define _MDC_CMD_PROCESSED_MASK		(0x003F0000)
#define _MDC_CMD_PROCESSED_LENGTH	(6)

#define _MDC_INT_ACTIVE_OFFSET		_MDC_CMDS_PROCESSED_OFFSET
#define _MDC_INT_ACTIVE_SHIFT		(8)
#define _MDC_INT_ACTIVE_MASK		(0x00000100)
#define _MDC_INT_ACTIVE_LENGTH		(1)

#define _MDC_CMDS_DONE_OFFSET		_MDC_CMDS_PROCESSED_OFFSET
#define _MDC_CMDS_DONE_SHIFT		(0)
#define _MDC_CMDS_DONE_MASK		(0x0000003F)
#define _MDC_CMDS_DONE_LENGTH		(6)

/* -------------------- Register MDC_CONTROL_AND_STATUS -------------------- */

#define _MDC_CONTROL_AND_STATUS_OFFSET	(0x01C)

#define _MDC_TAG_OFFSET			_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_TAG_SHIFT			(24)
#define _MDC_TAG_MASK			(0x0F000000)
#define _MDC_TAG_LENGTH			(4)

#define _MDC_CANCEL_OFFSET		_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_CANCEL_SHIFT		(20)
#define _MDC_CANCEL_MASK		(0x00100000)
#define _MDC_CANCEL_LENGTH		(1)

#define _MDC_DREQ_OFFSET		_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_DREQ_SHIFT			(16)
#define _MDC_DREQ_MASK			(0x00010000)
#define _MDC_DREQ_LENGTH		(1)

#define _MDC_FIFO_DEPTH_OFFSET 		_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_FIFO_DEPTH_SHIFT		(8)
#define _MDC_FIFO_DEPTH_MASK		(0x00000100)
#define _MDC_FIFO_DEPTH_LENGTH 		(1)

#define _MDC_LIST_EN_OFFSET		_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_LIST_EN_SHIFT		(4)
#define _MDC_LIST_EN_MASK		(0x00000010)
#define _MDC_LIST_EN_LENGTH		(1)

#define _MDC_EN_OFFSET			_MDC_CONTROL_AND_STATUS_OFFSET
#define _MDC_EN_SHIFT			(0)
#define _MDC_EN_MASK			(0x00000001)
#define _MDC_EN_LENGTH			(1)

/* -------------------- Register MDC_GLOBAL_CONFA -------------------------- */

#define _MDC_GLOBAL_CONFA_OFFSET	(0x900)

#define _MDC_THREADID_WIDTH_OFFSET	_MDC_GLOBAL_CONFA_OFFSET
#define _MDC_THREADID_WIDTH_SHIFT	(16)
#define _MDC_THREADID_WIDTH_MASK	(0x00FF0000)
#define _MDC_THREADID_WIDTH_LENGTH	(8)

#define _MDC_NUM_CONTEXTS_OFFSET	_MDC_GLOBAL_CONFA_OFFSET
#define _MDC_NUM_CONTEXTS_SHIFT		(8)
#define _MDC_NUM_CONTEXTS_MASK		(0x0000FF00)
#define _MDC_NUM_CONTEXTS_LENGTH	(8)

#define _MDC_SYS_DATA_WIDTH_OFFSET	_MDC_GLOBAL_CONFA_OFFSET
#define _MDC_SYS_DATA_WIDTH_SHIFT	(0)
#define _MDC_SYS_DATA_WIDTH_MASK	(0x000000FF)
#define _MDC_SYS_DATA_WIDTH_LENGTH	(8)


/* -------------------- Register MDC_GLOBAL_CONFG -------------------------- */

#define _MDC_GLOBAL_CONFB_OFFSET	(0x904)


/* -------------------- Register MDC_GLOBAL_STATUS -------------------------- */

#define _MDC_GLOBAL_STATUS_OFFSET	(0x908)


/*Helper Macros (dont use outside of this file */
#define _REG_ADDRESS(REG) _##REG##_OFFSET
#define _REG_MASK(REG) _##REG##_MASK
#define _REG_SHIFT(REG) _##REG##_SHIFT

#define _MDC_WRITE_REG(base, context, REG, value)	\
	iowrite32(value,(void *)((base + (context * MDC_CONTEXT_OFFSET) \
			+ _REG_ADDRESS(REG))))

#define _MDC_READ_REG(base, context, REG)	\
	ioread32(base + (void *)((context * MDC_CONTEXT_OFFSET) \
			+ _REG_ADDRESS(REG)))

#define _MDC_READ_REG_FIELD(base, ctext, REG) \
	((_MDC_READ_REG(base, ctext, REG) & _REG_MASK(REG)) >> _REG_SHIFT(REG))

#define _MDC_READ_GLOBAL_REG(base, address)\
	ioread32((void *)(base + address))

#define _MDC_READ_GLOBAL_REG_FIELD(base, REG) \
	((_MDC_READ_GLOBAL_REG(base, _REG_ADDRESS(REG)) \
			& _REG_MASK(REG)) >> _REG_SHIFT(REG))

/* Helper to be used externally */
#define MDC_SET_FIELD(data, FIELD, value) \
{\
	data &= ~_REG_MASK(FIELD);\
	data |=  value << _REG_SHIFT(FIELD);\
}




static inline void MDC_REG_RESET_CONTEXT(u32 base, int context)
{
	_MDC_WRITE_REG(base, context, MDC_CONTROL_AND_STATUS, (1 << 20));
	wmb();
	_MDC_WRITE_REG(base, context, MDC_GENERAL_CONFIG, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_READ_PORT_CONFIG, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_READ_ADDRESS, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_WRITE_ADDRESS, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_LIST_NODE_ADDRESS, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_TRANSFER_SIZE, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_CMDS_PROCESSED, 0x00000000);
	_MDC_WRITE_REG(base, context, MDC_CONTROL_AND_STATUS, 0x00000000);
	wmb();
}



static inline void MDC_RSET_GENERAL_CONFIG(u32 base, int context, u32 config)
{
	_MDC_WRITE_REG(base,  context, MDC_GENERAL_CONFIG, config);
}

static inline u32 MDC_RGET_GENERAL_CONFIG(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_GENERAL_CONFIG);
}

static inline void MDC_RSET_READ_PORT_CONFIG(u32 base, int context, u32 config)
{
	_MDC_WRITE_REG(base, context, MDC_READ_PORT_CONFIG, config);
}

static inline u32 MDC_RGET_READ_PORT_CONFIG(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_READ_PORT_CONFIG);
}

static inline void MDC_RSET_READ_ADDRESS(u32 base, int context, u32 address)
{
	_MDC_WRITE_REG(base, context, MDC_READ_ADDRESS, address);
}

static inline u32 MDC_RGET_READ_ADDRESS(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_READ_ADDRESS);
}

static inline void MDC_RSET_WRITE_ADDRESS(u32 base, int context, u32 address)
{
	_MDC_WRITE_REG(base, context, MDC_WRITE_ADDRESS, address);
}

static inline u32 MDC_RGET_WRITE_ADDRESS(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_WRITE_ADDRESS);
}

static inline void MDC_RSET_TRANSFER_SIZE(u32 base, int context, u32 size)
{
	_MDC_WRITE_REG(base, context, MDC_TRANSFER_SIZE, size);
}

static inline void MDC_RSET_LIST_NODE_ADDR(u32 base, int context, u32 address)
{
	_MDC_WRITE_REG(base, context, MDC_ADDR_S, address);
}

static inline u32 MDC_RGET_LIST_NODE_ADDR(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_ADDR_S);
}


static inline u32 MDC_RGET_CMDS_PROCESSED(u32 base, int context)
{
	return _MDC_READ_REG(base, context, MDC_CMDS_PROCESSED);
}

static inline void MDC_RSET_CMDS_PROCESSED(u32 base, int context, u32 val)
{
	_MDC_WRITE_REG(base, context, MDC_CMDS_PROCESSED, val);
}

static inline void MDC_REG_ENABLE(u32 base, int context)
{
	_MDC_WRITE_REG(base, context, MDC_EN,
		_MDC_READ_REG(base, context, MDC_EN) | _MDC_EN_MASK);
}

static inline void MDC_LIST_ENABLE(u32 base, int context)
{
	_MDC_WRITE_REG(base, context, MDC_LIST_EN,
		_MDC_READ_REG(base, context, MDC_LIST_EN) | _MDC_LIST_EN_MASK);
}

static inline void MDC_CANCEL(u32 base, int context)
{
	_MDC_WRITE_REG(base, context, MDC_CANCEL, _MDC_CANCEL_MASK);
}

static inline int MDC_REG_IS_BUSY(u32 base, int context)
{
	return _MDC_READ_REG_FIELD(base, context, MDC_EN);
}

/*List Support: */

struct img_dma_mdc_list {
	volatile u32 gen_conf;
	volatile u32 readport_conf;
	volatile u32 read_addr;
	volatile u32 write_addr;
	volatile u32 xfer_size;
	volatile u32 node_addr;
	volatile u32 cmds_done;
	volatile u32 ctrl_status;
};

enum img_dma_priority {
	IMG_DMA_PRIO_BULK = 0,
	IMG_DMA_PRIO_REALTIME,
};

enum img_dma_direction {
	IMG_DMA_INVALID_DIR,
	IMG_DMA_TO_PERIPHERAL,
	IMG_DMA_FROM_PERIPHERAL,
	IMG_DMA_MEM2MEM,
};

enum img_dma_channel_state {
	IMG_DMA_CHANNEL_RESERVED,
	IMG_DMA_CHANNEL_AVAILABLE,
	IMG_DMA_CHANNEL_INUSE,
};

enum img_dma_width {
	IMG_DMA_WIDTH_8 = 0,
	IMG_DMA_WIDTH_16,
	IMG_DMA_WIDTH_32,
	IMG_DMA_WIDTH_64,
	IMG_DMA_WIDTH_128,
};

#endif /* __MDC_API_H__ */
