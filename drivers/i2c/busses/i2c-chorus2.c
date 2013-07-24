/*
 *  I2C adapter for the Chorus2 Serial Control Bus.
 *
 *  Copyright (C) 2006-2008,2009 Imagination Technologies Ltd.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <asm/soc-chorus2/gpio.h>

/* TODO
 * - actually use the interrupt and sleep where appropriate
 */

#define SCB_REG0                0x0000
#define SCB_REG1                0x0001
#define SCB_REG2                0x0002
#define SCB_REG3                0x0003
#define SCB_REG4                0x0004
#define SCB_REG5                0x0005

#define SCB_REG1_SDM            0x0001
#define SCB_REG1_SCM            0x0002
#define SCB_REG1_BEI            0x0004
#define SCB_REG1_SC             0x0008
#define SCB_REG1_LOOPBACK       0x0010
#define SCB_REG1_BCR_MASK       0x0FE0
#define SCB_REG1_BCR_SHIFT           5
#define SCB_REG1_MODE           0x8000

#define SCB_REG2_DATA           0x00FF
#define SCB_REG2_DATA_SHIFT          0
#define SCB_REG2_BUS_STATUS     0x0030
#define SCB_REG2_BUS_STATUS_SHIFT    8
#define SCB_REG2_ACK            0x8000

#define SCB_REG3_CIE            0x0400
#define SCB_REG3_EIE            0x0800

#define REG0_CMD_SHIFT               8
#define REG0_CMD_TX                0x0
#define REG0_CMD_RESERVED          0x1
#define REG0_CMD_RX_ACK            0x2
#define REG0_CMD_RX_NACK           0x3
#define REG0_CMD_START             0x4
#define REG0_CMD_STOP              0x5
#define REG0_CMD_SLAVE_ADDR        0x6
#define REG0_CMD_EXIT_SLAVE        0x7
#define REG0_UPGRADE            0x1000

#define REG2_STATUS_IGNORE        0x00
#define REG2_STATUS_IDLE          0x01
#define REG2_STATUS_BUSY          0x02
#define REG2_STATUS_STUCK         0x03
#define REG2_ACK                0x8000
#define REG2_STATE_MASK         0x7000
#define REG2_STATE_SHIFT            12

#define REG2_STATE_IDLE            0x0
#define REG2_STATE_START           0x1
#define REG2_STATE_TX              0x2
#define REG2_STATE_RX              0x3
#define REG2_STATE_STOP            0x4
#define REG2_STATE_RSTART          0x5
#define REG2_STATE_RADDR           0x6
#define REG2_STATE_RSTOP           0x7

#define REG4_CMD_COMPLETE         0x01
#define REG4_ERROR                0x02

#define REG5_CMD_COMPLETE         0x01
#define REG5_ERROR                0x02
#define REG5_SOFT_RESET           0x04

#define TX_DATA_READ              0x01

#define SCB_WRAPPER_READY	        0x80000000
#define SCB_WRAPPER_READ_READY	        0x40000000
#define SCB_WRAPPER_IRQ		        0x20000000
#define SCB_WRAPPER_CMD_VALID	        0x10000000
#define SCB_WRAPPER_READ_NOT_WRITE	0x01000000
#define SCB_WRAPPER_ADDR_MASK	        0x00070000
#define SCB_WRAPPER_ADDR_SHIFT	                16
#define SCB_WRAPPER_DATA_MASK	        0x0000FFFF

#define MAX_REGNO                       5  /* the SCB only has 5 regs! */

#define WRAPPER_TIMEOUT                10  /* Specified as number of loops */
#define INTERRUPT_TIMEOUT	(HZ / 20)  /* Specified in jiffies */

#define SCB_BCR_50KHZ		0x8
#define SCB_BCR_100KHZ		0x11
#define SCB_BCR_200KHZ		0x22
#define SCB_BCR_400KHZ		0x44

#define SCB_PIN_CTRL_REG	0x02024004

#define SCB_PIN_CTRL_SCB2	0
#define SCB_PIN_CTRL_GPDAC	1
#define SCB_PIN_CTRL_SPI2	2
#define SCB_PIN_CTRL_GPIO	3

#define SCB_DAT_CTRL_SHIFT	0
#define SCB_CLK_CTRL_SHIFT	2

struct chorus2_i2c {
	struct i2c_adapter adap;

	void __iomem *reg_base;

	unsigned long iobase;
	unsigned long iosize;
};

#define GPIO_CLK_PIN	GPIO_E_PIN(14)
#define GPIO_DATA_PIN	GPIO_E_PIN(15)

static void scb_clear_error(void)
{
	int attempts;

	gpio_request(GPIO_CLK_PIN, "SCB CLK gpio");
	gpio_request(GPIO_DATA_PIN, "SCB data gpio");

	/* Make clock an output pin. */
	gpio_direction_output(GPIO_CLK_PIN, 1);

	/* Make data pin an input. */
	gpio_direction_input(GPIO_DATA_PIN);

	writel((SCB_PIN_CTRL_GPIO << SCB_DAT_CTRL_SHIFT) +
	       (SCB_PIN_CTRL_GPIO << SCB_CLK_CTRL_SHIFT),
	       SCB_PIN_CTRL_REG);

	for (attempts = 0; attempts < 100; attempts++) {
		unsigned data = gpio_get_value(GPIO_DATA_PIN) & 1;
		if (data == 1)
			break;
		else {
			udelay(5);
			gpio_set_value(GPIO_CLK_PIN, 0);
			udelay(5);
			gpio_set_value(GPIO_CLK_PIN, 1);
		}
	}

	writel((SCB_PIN_CTRL_SCB2 << SCB_DAT_CTRL_SHIFT) +
	       (SCB_PIN_CTRL_SCB2 << SCB_CLK_CTRL_SHIFT),
	       SCB_PIN_CTRL_REG);

	chorus2_gpio_disable(GPIO_CLK_PIN);
	chorus2_gpio_disable(GPIO_DATA_PIN);
}

static int scb_wait_wrapper(void __iomem *reg_base, unsigned int wait_mask)
{
	unsigned int wrapper;
	unsigned int loops;

	for (loops = 0; loops < WRAPPER_TIMEOUT; loops++) {
		wrapper = readl(reg_base);
		if (wrapper & wait_mask)
			return 0;
	}

	pr_err("SCB wrapper ready timeout: %#x (%#x)\n", wrapper, wait_mask);

	return -ETIMEDOUT;
}

/* Read an SCB register via the wrapper register */
static int scb_read_reg(void __iomem *reg_base, unsigned int regno,
			unsigned short *value)
{
	unsigned short data;

	if (regno > MAX_REGNO)
		return -EINVAL;

	/* Wait for the wrapper to be ready */
	if (scb_wait_wrapper(reg_base, SCB_WRAPPER_READY))
		return -ETIMEDOUT;

	/* Write the read command */
	writel(SCB_WRAPPER_CMD_VALID | SCB_WRAPPER_READ_NOT_WRITE |
	       ((regno << SCB_WRAPPER_ADDR_SHIFT) & SCB_WRAPPER_ADDR_MASK),
	       reg_base);

	/* Wait for the valid_data to come up */
	if (scb_wait_wrapper(reg_base, SCB_WRAPPER_READ_READY))
		return -ETIMEDOUT;

	/* And read the data out */
	data = (unsigned short) readl(reg_base) & SCB_WRAPPER_DATA_MASK;

	*value = data;

	return 0;
}

/* Write an SCB register via the wrapper register */
static int scb_write_reg(void __iomem *reg_base, unsigned int regno,
			 unsigned short value)
{
	if (regno > MAX_REGNO)
		return -EINVAL;

	/* Wait for the wrapper to be ready */
	if (scb_wait_wrapper(reg_base, SCB_WRAPPER_READY))
		return -ETIMEDOUT;

	/* Write the write command */
	writel(SCB_WRAPPER_CMD_VALID |
	       ((regno << SCB_WRAPPER_ADDR_SHIFT) & SCB_WRAPPER_ADDR_MASK) |
	       value,
	       reg_base);

	return 0;
}

static int chorus2_scb_ready(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short reg2;
	unsigned short reg4;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	if (scb_read_reg(algo_data->reg_base, SCB_REG2, &reg2))
		return 0;

	switch ((reg2 & SCB_REG2_BUS_STATUS) >> SCB_REG2_BUS_STATUS_SHIFT) {
	case REG2_STATUS_BUSY:
	case REG2_STATUS_STUCK:
		return 0;
	case REG2_STATUS_IDLE:
	case REG2_STATUS_IGNORE:
	default:
		break;
	}

	if (scb_read_reg(algo_data->reg_base, SCB_REG4, &reg4))
		return 0;

	if (reg4 & REG4_ERROR)
		return 0;

	return 1;
}

static unsigned char chorus2_scb_get_state(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;
	unsigned char state;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	if (scb_read_reg(algo_data->reg_base, SCB_REG2, &data))
		return 0;

	state = (data & REG2_STATE_MASK) >> REG2_STATE_SHIFT;

	return state;
}

static int chorus2_scb_got_ack(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	if (scb_read_reg(algo_data->reg_base, SCB_REG2, &data))
		return 0;

	if (data & REG2_ACK)
		return 1;

	return 0;
}

static int chorus2_scb_wait_interrupt(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;
	unsigned long start_time = jiffies;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	/* Give us a couple of ticks, or get bored */
	while (!time_after_eq(jiffies, start_time + INTERRUPT_TIMEOUT)) {
		if (scb_read_reg(algo_data->reg_base, SCB_REG4, &data))
			return -EIO;

		if (data) {
			int ret;

			if (data & REG4_CMD_COMPLETE) {
				ret = 0;
			} else if (data & REG4_ERROR) {
				dev_dbg(&i2c_adap->dev, "error interrupt!\n");

				ret = -EIO;
			} else {
				dev_dbg(&i2c_adap->dev,
					"command not complete interrupt!\n");
				ret = -EIO;
			}

			/* clear down the interrupts */
			data = REG5_ERROR | REG5_CMD_COMPLETE;
			if (scb_write_reg(algo_data->reg_base, SCB_REG5, data))
				ret = -EIO;

			return ret;
		}
	}

	dev_dbg(&i2c_adap->dev, "timed out waiting for interrupt?\n");

	return -ETIMEDOUT;
}

static int chorus2_scb_stop(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	data = (REG0_CMD_STOP << REG0_CMD_SHIFT) | REG0_UPGRADE;
	if (scb_write_reg(algo_data->reg_base, SCB_REG0, data))
		return -EIO;

	if (chorus2_scb_wait_interrupt(i2c_adap))
		return -EIO;

	return 0;
}

static int chorus2_scb_start(struct i2c_adapter *i2c_adap, unsigned char addr,
			     int reading)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;
	unsigned char state;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	state = chorus2_scb_get_state(i2c_adap);

	if (state != REG2_STATE_IDLE) {
		dev_warn(&i2c_adap->dev,
			 "scb_start: expected idle state: %hhx\n", state);
		return -EIO;
	}

	data = (REG0_CMD_START << REG0_CMD_SHIFT) | REG0_UPGRADE;
	if (scb_write_reg(algo_data->reg_base, SCB_REG0, data))
		return -EIO;

	if (chorus2_scb_wait_interrupt(i2c_adap))
		return -EIO;

	/* Manual says that status is always '0' after start or stop, so
	 * don't check it?
	 */

	/* Send the slave address */
	data = (addr << 1) | (REG0_CMD_TX << REG0_CMD_SHIFT) | REG0_UPGRADE;

	/* Set the rd/!wr bit at the bottom of the 'addr' byte */
	if (reading)
		data |= TX_DATA_READ;

	if (scb_write_reg(algo_data->reg_base, SCB_REG0, data))
		return -EIO;

	if (chorus2_scb_wait_interrupt(i2c_adap))
		return -EIO;

	/* Even if we are about to do a READ, the state machine will
	 * still report TX mode, ready for us to TX the read request!
	 */
	state = chorus2_scb_get_state(i2c_adap);

	if (state != REG2_STATE_TX)
		dev_warn(&i2c_adap->dev,
			 "scb_start: expected TX state: %hhx\n", state);

	/* And for that, we expect an ACK ! */
	if (!chorus2_scb_got_ack(i2c_adap)) {
		chorus2_scb_stop(i2c_adap);
		return -EIO;
	}

	return 0;
}

static int chorus2_scb_read(struct i2c_adapter *i2c_adap, unsigned char addr,
			    char *buffer, int len)
{
	struct chorus2_i2c *algo_data;
	int remaining = len;
	char *bufp = buffer;
	unsigned short data;
	unsigned char state;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	/* Note - even though we are reading, the state machine will be in
	 * TX state, waiting for us to TX the read request to it.
	 */
	state = chorus2_scb_get_state(i2c_adap);
	if (state != REG2_STATE_TX)
		dev_warn(&i2c_adap->dev,
			 "scb_read: expected TX state: %hhx\n", state);

	while (remaining) {
		/* Send the read request - no ACK for the last read */
		if (remaining == 1)
			data = (REG0_CMD_RX_NACK << REG0_CMD_SHIFT) |
				REG0_UPGRADE;
		else
			data = (REG0_CMD_RX_ACK << REG0_CMD_SHIFT) |
				REG0_UPGRADE;

		if (scb_write_reg(algo_data->reg_base, SCB_REG0, data))
			return -ENODEV;

		if (chorus2_scb_wait_interrupt(i2c_adap)) {
			dev_dbg(&i2c_adap->dev, "no RX interrupt\n");
			return -ENODEV;
		}

		/* Should have moved to RX state now */
		state = chorus2_scb_get_state(i2c_adap);
		if (state != REG2_STATE_RX)
			dev_warn(&i2c_adap->dev,
				 "scb_read: expected RX state: %hhx\n", state);

		/* Acks on all but the last read */
		if (remaining != 1) {
			if (!chorus2_scb_got_ack(i2c_adap)) {
				dev_dbg(&i2c_adap->dev, "no ACK, %d left\n",
					remaining);
				return -EIO;
			}
		}

		if (scb_read_reg(algo_data->reg_base, SCB_REG2, &data))
			return -EIO;

		data &= SCB_REG2_DATA;

		dev_dbg(&i2c_adap->dev, "got back data [%#x]\n", data);

		*bufp++ = data;

		remaining--;
	}

	return 0;
}

static int chorus2_scb_write(struct i2c_adapter *i2c_adap, unsigned char addr,
			     const char *buffer, int len)
{
	struct chorus2_i2c *algo_data;
	int remaining = len;
	const char *bufp = buffer;
	unsigned short data;
	unsigned char state;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	while (remaining) {
		state = chorus2_scb_get_state(i2c_adap);
		if (state != REG2_STATE_TX)
			dev_warn(&i2c_adap->dev,
				 "scb_write: expected TX state (1): %hhx\n",
				 state);

		/* Send the byte */
		data = *bufp | (REG0_CMD_TX << REG0_CMD_SHIFT) | REG0_UPGRADE;
		bufp++;

		if (scb_write_reg(algo_data->reg_base, SCB_REG0, data))
			return -EIO;

		if (chorus2_scb_wait_interrupt(i2c_adap))
			return -EIO;

		state = chorus2_scb_get_state(i2c_adap);
		if (state != REG2_STATE_TX)
			dev_warn(&i2c_adap->dev,
				 "scb_write: expected TX state (2): %hhx\n",
				 state);

		/* And for that, we expect an ACK ! */
		if (!chorus2_scb_got_ack(i2c_adap))
			return -EIO;

		remaining--;
	}
	return 0;
}

static int chorus2_scb_xfer(struct i2c_adapter *i2c_adap, struct i2c_msg *msgs,
			    int num)
{
	int i;
	struct i2c_msg *msg;

	if (!num)
		return 0;

	for (i = 0; i < num; i++) {
		int err = 0;

		msg = &msgs[i];

		dev_dbg(&i2c_adap->dev, " %d: addr %d, %s, dlen %d\n",
			i,
			msg->addr,
			(msg->flags & I2C_M_RD) ? "rd" : "wr", msg->len);

		if (!chorus2_scb_ready(i2c_adap)) {
			dev_warn(&i2c_adap->dev, "bus not ready\n");
			return -EBUSY;
		}

		if (chorus2_scb_start(i2c_adap, msg->addr,
				      msg->flags & I2C_M_RD)) {
			/* A failed start normally means there is no
			 * device there.
			 */
			dev_dbg(&i2c_adap->dev,
				"start failed for xfer, addr %#x\n",
				msg->addr);
			scb_clear_error();
			return -ENODEV;
		}

		if (msg->flags & I2C_M_RD)
			err = chorus2_scb_read(i2c_adap, msg->addr, msg->buf,
					       msg->len);
		else
			err = chorus2_scb_write(i2c_adap, msg->addr, msg->buf,
						msg->len);

		/* Only issue a STOP at the end of the whole sequence. */

		/* NOTE NOTE NOTE
		 * I think we have a bug in our I2C/SCB controller here!
		 *
		 * In theory, I should be able to issue 'restarts' between
		 * transactions, and only issue a STOP when I've run out
		 * of things to do.
		 *
		 * In actuallity, that does not seem to work.
		 * I'd really like to move this outside of the cmd loop, or
		 * to only issue STOPs if the slave addr changes between cmds.
		 */

		chorus2_scb_stop(i2c_adap);

		if (err) {
			dev_err(&i2c_adap->dev,
				"%s %d bytes addr %#x failed error %d\n",
				msg->flags & I2C_M_RD ? "read" : "write",
				msg->len, msg->addr, err);
			return err;
		}
	}

	return i;
}

static u32 chorus2_scb_func(struct i2c_adapter *i2c_adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static int __init chorus2_scb_init(struct i2c_adapter *i2c_adap)
{
	struct chorus2_i2c *algo_data;
	unsigned short data;
	unsigned short readback;

	algo_data = (struct chorus2_i2c *)i2c_adap->algo_data;

	/* First, lets try a soft reset! */
	data = REG5_SOFT_RESET;
	if (scb_write_reg(algo_data->reg_base, SCB_REG5, data)) {
		dev_dbg(&i2c_adap->dev, "failed to soft reset Chorus2 SCB\n");
		return -EIO;
	}

	/* Reg1 - Mode control register:
	 *  DAT and CLK open drain.
	 *  Bus Error Interrupts enabled
	 *  Status Codes enabled
	 *  Not in loopback
	 *  Clock rate 100kHz
	 *  Byte mode
	 */
	data = SCB_BCR_100KHZ << SCB_REG1_BCR_SHIFT;
	data |= SCB_REG1_BEI;
	data |= SCB_REG1_SC;

	if (scb_write_reg(algo_data->reg_base, SCB_REG1, data)) {
		dev_dbg(&i2c_adap->dev, "failed to write SCB reg1\n");
		return -EIO;
	}

	if (scb_read_reg(algo_data->reg_base, SCB_REG1, &readback)) {
		dev_dbg(&i2c_adap->dev, "failed to read back SCB reg1\n");
		return -EIO;
	}

	if (readback != data) {
		dev_dbg(&i2c_adap->dev, "failed to set SCB reg1\n");
		return -EIO;
	}

	/* Reg3 - timeouts, interrupt enables etc. */
	data = SCB_REG3_CIE;
	data |= SCB_REG3_EIE;

	/* FIXME - Need to define and clean up the whole timeout
	 * stuff - for instance - program the timeout register in the SCB?
	 */
	if (scb_write_reg(algo_data->reg_base, SCB_REG3, data)) {
		dev_dbg(&i2c_adap->dev, "failed to write SCB reg3\n");
		return -EIO;
	}

	if (scb_read_reg(algo_data->reg_base, SCB_REG3, &readback)) {
		dev_dbg(&i2c_adap->dev, "failed to read back SCB reg3\n");
		return -EIO;
	}

	if (readback != data) {
		dev_dbg(&i2c_adap->dev, "failed to set SCB reg1\n");
		return -EIO;
	}

	return 0;
}

static const struct i2c_algorithm i2c_chorus2_algorithm = {
	.master_xfer = chorus2_scb_xfer,
	.functionality = chorus2_scb_func,
};

static int __init i2c_chorus2_probe(struct platform_device *dev)
{
	struct chorus2_i2c *i2c;
	struct resource *res;
	struct device_node *node = dev->dev.of_node;
	int ret;
	u32 val;

	if (!node)
		return -ENOENT;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -ENODEV;

	if (!request_mem_region(res->start, resource_size(res), res->name))
		return -ENOMEM;

	i2c = kzalloc(sizeof(struct chorus2_i2c), GFP_KERNEL);
	if (!i2c) {
		ret = -ENOMEM;
		goto out_error_kmalloc;
	}

	i2c->adap.owner = THIS_MODULE;
	i2c->adap.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;
	i2c->adap.algo = &i2c_chorus2_algorithm;
	i2c->adap.retries = 5;

	sprintf(i2c->adap.name, "chorus2_i2c-i2c.%u", dev->id);

	i2c->reg_base = ioremap(res->start, resource_size(res));
	if (!i2c->reg_base) {
		ret = -EIO;
		goto out_error_ioremap;
	}

	i2c->iobase = res->start;
	i2c->iosize = resource_size(res);

	i2c->adap.algo_data = i2c;
	i2c->adap.dev.parent = &dev->dev;

	/*
	 * Get the "dev->id" from the device tree. If there
	 * is no such attribute, print an error message and
	 * free allocated resources.
	 */
	ret = of_property_read_u32(node, "id", &val);
	if (ret) {
		dev_err(&dev->dev, "could not find the id number");
		goto out_error_id;
	}

	i2c->adap.nr = val;

	ret = chorus2_scb_init(&i2c->adap);

	if (ret) {
		dev_warn(&dev->dev, "failed to reset bus\n");
		goto out_error_ioremap;
	}

	ret = i2c_add_numbered_adapter(&i2c->adap);
	if (ret < 0) {
		dev_info(&dev->dev, "failed to add bus\n");
		goto out_error_ioremap;
	}

	platform_set_drvdata(dev, i2c);

	dev_info(&dev->dev, "Chorus2 I2C adapter probed successfully\n");

	return 0;

out_error_id:
	iounmap(i2c->reg_base);
out_error_ioremap:
	kfree(i2c);
out_error_kmalloc:
	release_mem_region(res->start, resource_size(res));
	return ret;
}

static int __exit i2c_chorus2_remove(struct platform_device *dev)
{
	struct chorus2_i2c *i2c = platform_get_drvdata(dev);

	i2c_del_adapter(&i2c->adap);

	release_mem_region(i2c->iobase, i2c->iosize);
	kfree(i2c);

	return 0;
}

static const struct of_device_id i2c_img_match[] = {
	{ .compatible = "img,chorus2-i2c" },
	{}
};
MODULE_DEVICE_TABLE(of, i2c_img_match);

static struct platform_driver i2c_chorus2_driver = {
	.driver = {
		.name	= "chorus2-i2c",
		.owner	= THIS_MODULE,
		.of_match_table	= i2c_img_match,
	},
	.remove = __exit_p(i2c_chorus2_remove),
};

static int __init i2c_adap_chorus2_init(void)
{
	return platform_driver_probe(&i2c_chorus2_driver, i2c_chorus2_probe);
}
module_init(i2c_adap_chorus2_init);

static void __exit i2c_adap_chorus2_exit(void)
{
	platform_driver_unregister(&i2c_chorus2_driver);
}
module_exit(i2c_adap_chorus2_exit);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("Chorus2 SCB I2C bus");
MODULE_LICENSE("GPL");
