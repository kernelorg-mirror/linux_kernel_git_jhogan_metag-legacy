/*
 * Copyright (C) 2009,2010 Imagination Technologies Limited.
 *
 * Quantum TouchScreen Controller driver.
 */

#include <linux/types.h>
#include <linux/wait.h>
#include <linux/fs.h>
#include <linux/poll.h>
#include <linux/miscdevice.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/input/ts_qt5480.h>

#include "ts_qt5480.h"

/*
 * TODO
 *  - multi-touch support
 *  - clean up debug messages
 *  - drop Pure's interface and migrate to just /dev/input
 *  - figure out unreliability of writes
 */

#ifndef I2C_M_WR
#define I2C_M_WR	0x00
#endif

#define QT5480_NAME		"ts_qt5480"

#define QT5480_MAX_I2C_RETRIES	5

#define QT5480_SLAVE_ADDRESS	0x30

/* Polling Frequency */
#define QT5480_POLL_TIME	(HZ/25)

/* FIXME this is probably not correct */
#define MAX_PRESSURE_VALUE	0x10

/* touch-screen touch event */
struct ts_qt5480_touch {
	struct list_head link;
	unsigned char x;
	unsigned char y;
	unsigned char size;
	unsigned char area;
};

/* touch-screen gesture event */
struct ts_qt5480_gesture {
	unsigned char event;
	unsigned char dir;
	unsigned char dist;
	unsigned char x;
	unsigned char y;
};

/* touch-screen chip info */
struct ts_qt5480_status {
	unsigned char update_flags;
	unsigned char general_status_1;
	unsigned char general_status_2;
	unsigned char key[6];
	unsigned char slider[6];
	struct ts_qt5480_touch touchscr[2];
	struct ts_qt5480_gesture gesture[2];
};

/* Driver data */
struct ts_qt5480_data {
	char phys[32];
	struct input_dev *input;
	int (*poll_status)(void);

	/* Control data */
	unsigned int used;
	struct mutex mutex;

	/* Chip Connections Configuration */
	struct i2c_client *client;
	struct i2c_adapter *i2c_adap;
	unsigned char dev_addr;
	ts_qt5480_mapping_t *phy_map;
	ts_qt5480_conf_reg_t *config;

	/* Chip Live Data */
	struct ts_qt5480_status chip_status;

	/* Touch state */
	bool touch1_down;
};

static struct ts_qt5480_data *ts_qt5480_data;

static int ts_qt5480_read_change(struct ts_qt5480_data *data,
				 unsigned char *buf);
static void ts_qt5480_parse_data(struct ts_qt5480_data *data,
				 unsigned char *buf,
				 struct ts_qt5480_status *chip_status);

static irqreturn_t ts_qt5480_isr_check(int irq, void *p)
{
	return IRQ_WAKE_THREAD;
}

static irqreturn_t ts_qt5480_isr(int irq, void *p)
{
	struct ts_qt5480_data *data = p;
	unsigned char buf[5] = { };

	while (!data->poll_status()) {
		mutex_lock(&data->mutex);

		if (!ts_qt5480_read_change(data, buf)) {
			ts_qt5480_parse_data(data, buf,
					     &data->chip_status);
		}

		mutex_unlock(&data->mutex);
	}

	return IRQ_HANDLED;
}

static int ts_qt5480_read_reg(struct ts_qt5480_data *data, short addr,
			      unsigned char *value)
{
	struct i2c_msg msg = { };
	struct ts_qt5480_frame frame = { };
	unsigned char buf[5] = { };
	unsigned int retries = 0;
	unsigned long timeout;
	short read_ptr = addr >> 2;

	dev_dbg(&data->client->dev, "ts_qt5480_read_reg()\n");

	do {
		/*
		 * If we have reach the maximum number of retries,
		 * lets report error.
		 */
		if (++retries == QT5480_MAX_I2C_RETRIES) {
			dev_err(&data->client->dev,
				"ts_qt5480_read_reg() - Error: Setting register %d. Max retries achieved\n",
				addr);
			return -ENODEV;
		}

		/* Prepare the address and the data */
		msg.addr = data->dev_addr;
		msg.flags = I2C_M_WR;
		msg.len = 3;
		msg.buf = (__u8 *) &frame;
		frame.addr = QT_ADDRESS_PTR;
		frame.data[0] = read_ptr;

		/* write the "read address pointer" */
		frame.stat = i2c_transfer(data->i2c_adap, &msg, 1);
		if (frame.stat < 0) {
			dev_warn(&data->client->dev,
				 "ts_qt5480_read_reg() - Error (%d): Failed writing the read address pointer\n",
				 frame.stat);
			continue;
		}

		udelay(500);

		/* Wait for roughly 10ms. */
		timeout = jiffies + (10 * HZ / 1000);

		/*
		 * Wait, with timeout, for the change line asserted low
		 * (requested data ready signal).
		 */
		while (time_before(jiffies, timeout)) {
			/* if change line asserted low, lets read the frame */
			if (!data->poll_status()) {
				/* Read back the data */
				frame.stat = ts_qt5480_read_change(data, buf);
				if (frame.stat < 0) {
					dev_warn(&data->client->dev,
						 "ts_qt5480_read_reg() - Error (%d): Failed reading reg %d\n",
						 frame.stat, addr);
				}
				break;
			}
		}

		if (frame.stat < 0)
			continue;

		/*
		 * If we haven't get the data we are asking for, lets
		 * request it again.
		 */
	} while (buf[0] != read_ptr);

	/* Copy the value back */
	(*value) = buf[(addr & 0x03) + 1];

	return 0;
}

static int ts_qt5480_write_reg(struct ts_qt5480_data *data, short addr,
			       unsigned char value)
{
	struct i2c_msg msg = { };
	struct ts_qt5480_frame frame = { };

	dev_dbg(&data->client->dev, "ts_qt5480_write_reg()\n");

	/* Prepare the address and the data */
	msg.addr = data->dev_addr;
	msg.flags = I2C_M_WR;
	msg.len = 3;
	msg.buf = (__u8 *) &frame;
	frame.addr = addr;
	frame.data[0] = value;

	/* Write the value into the register */
	frame.stat = i2c_transfer(data->i2c_adap, &msg, 1);
	if (frame.stat < 0) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_write_reg() - Error %d: Failed writing reg %d\n",
			 frame.stat, addr);
		return -ENODEV;
	}

	/*
	 * Writing registers too quickly appears to cause the device
	 * to issues NACKs.
	 */
	udelay(500);

	return 0;
}

static int ts_qt5480_reset(struct ts_qt5480_data *data)
{
	unsigned char buf[5] = { };
	unsigned long timeout;
	int ret;
	unsigned char chipid = 0, software = 0;

	dev_dbg(&data->client->dev, "ts_qt5480_reset()\n");

	/* Writing a non-zero value causes a reset. */
	ret = ts_qt5480_write_reg(data, QT_RESET, 1);
	if (ret < 0) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - Error %d: Failed writing reset reg\n",
			 ret);
		return -ENODEV;
	}

	/*
	 * Wait for 40ms. This is necessary because the device sometimes
	 * starts up with the CHANGE line asserted and we have to wait for
	 * it to be deasserted before looking for the reset response. 
	 */
	msleep(40);

	/* Timeout after roughly 15ms. */
	timeout = jiffies + (15 * HZ / 1000);

	/* Wait for reset to be acknowledged. */
	while (data->poll_status() && time_before(jiffies, timeout))
		cpu_relax();

	if (data->poll_status()) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - Error: did not respond to reset\n");
	}

	/* Read response. */
	ret = ts_qt5480_read_change(data, buf);
	if (ret < 0) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - Error %d: reading status\n",
			 ret);
	}

	/* Test for reset bit. */
	if (!(buf[3] & 0x80)) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - incorrect reset response: %#hhx\n",
			 buf[3]);
	}

	ret = ts_qt5480_read_reg(data, QT_CHIP_ID, &chipid);
	if (ret < 0) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - Error %d: reading chip id\n",
			 ret);
		return -ENODEV;
	}

	ret = ts_qt5480_read_reg(data, QT_CODE_VERSION, &software);
	if (ret < 0) {
		dev_warn(&data->client->dev,
			 "ts_qt5480_reset() - Error %d: reading chip id\n",
			 ret);
		return -ENODEV;
	}

	dev_info(&data->client->dev,
		 "Found device, chip ID %#hhx, software %d.%d\n",
		 chipid, (software >> 4), software & 0xf);

	return 0;
}

static int ts_qt5480_send_config(struct ts_qt5480_data *data)
{
	int reg_addr = 0;
	int ret = -1;
	ts_qt5480_conf_reg_t *config = data->config;

	dev_dbg(&data->client->dev, "ts_qt5480_send_config()");

	for (reg_addr = 0; reg_addr < QT_MAX_REG; reg_addr++) {
		int i;

		if (!config[reg_addr].set)
			continue;

		for (i = 0; i < QT5480_MAX_I2C_RETRIES; i++) {
			/* write the register */
			ret = ts_qt5480_write_reg(data, reg_addr,
						  config[reg_addr].value);

			if (!ret)
				break;
		}

		if (ret < 0) {
			dev_err(&data->client->dev,
				"ts_qt5480_send_config() - Error: Failed write reg %d\n",
				reg_addr);
			return ret;
		}
	}

	/* Send a Calibration request with the new configuration */
	ret = ts_qt5480_write_reg(data, QT_CALIBRATE, 0x1);
	if (ret < 0) {
		dev_err(&data->client->dev,
			"ts_qt5480_send_config() - Error: Calibrating after Config\n");
		return ret;
	}

	return 0;
}

static int ts_qt5480_read_change(struct ts_qt5480_data *data,
				 unsigned char *buf)
{
	struct i2c_msg msg = { };
	int ret = -1;

	dev_dbg(&data->client->dev, "ts_qt5480_read_change()\n");

	/* Read back the data */
	msg.addr = data->dev_addr;
	msg.flags = I2C_M_RD;
	msg.len = 5;
	msg.buf = buf;

	ret = i2c_transfer(data->i2c_adap, &msg, 1);
	dev_dbg(&data->client->dev,
		"ts_qt5480_read_change() - i2c_transfer() ret %d - data %#x %#x %#x %#x %#x\n",
		ret, buf[0], buf[1], buf[2], buf[3], buf[4]);

	if (ret < 0) {
		dev_err(&data->client->dev,
			"ts_qt5480_read_change() - Warning: Failed read\n");
		return -ENODEV;
	}

	return 0;
}

static void add_finger_release_to_queue(struct ts_qt5480_data *data,
					struct ts_qt5480_touch *queue)
{
	struct ts_qt5480_touch *touch = kmalloc(sizeof(struct ts_qt5480_touch),
					   GFP_KERNEL);
	if (likely(touch)) {
		touch->x = 0;
		touch->y = 0;
		touch->size = 0;
		touch->area = 0;

		list_add_tail(&touch->link, &queue->link);
		dev_dbg(&data->client->dev,
			"Added %p %d %d %d\n", touch, touch->x, touch->y,
			touch->area);
	}

}

static void ts_qt5480_parse_data(struct ts_qt5480_data *data,
				 unsigned char *buf,
				 struct ts_qt5480_status *chip_status)
{
	unsigned int icnt;
	struct ts_qt5480_frame frame = { };
	struct input_dev *input = data->input;

	/* Safety check */
	if (buf == 0)
		return;

	/* get packet pkt_address */
	frame.addr = (unsigned short)buf[0] << 2;
	/* get 4 data bytes */
	frame.data[0] = buf[1];
	frame.data[1] = buf[2];
	frame.data[2] = buf[3];
	frame.data[3] = buf[4];

	dev_dbg(&data->client->dev, "ts_qt5480_parseData()\n");

	switch (frame.addr) {
	case QT_EEPROM_CHKSUM - 2:
		/* special case: isolate 16-bit checksum */
		frame.stat = (((unsigned short)frame.data[3] << 8) +
			      frame.data[2]);

		break;
	case QT_KEY_STATUS_0:
		for (icnt = 0; icnt < 4; icnt++)
			chip_status->key[icnt] = frame.data[icnt];
		/* flag key status changed */
		chip_status->update_flags |= KEY_0_UPDATE;

		break;
	case QT_KEY_STATUS_4:
		chip_status->key[4] = frame.data[0];
		chip_status->key[5] = frame.data[1];
		chip_status->general_status_1 = frame.data[2];
		chip_status->general_status_2 = frame.data[3];
		/* flag key status changed */
		chip_status->update_flags |= KEY_4_UPDATE;

		if ((!(chip_status->general_status_2 & TS0_DET))
		    && (chip_status->touchscr[0].area != 0)) {
			chip_status->touchscr[0].x = 0;
			chip_status->touchscr[0].y = 0;
			chip_status->touchscr[0].size = 0;
			chip_status->touchscr[0].area = 0;

			input_report_key(input, BTN_TOUCH, 0);
			input_report_abs(input, ABS_PRESSURE, 0);
			input_sync(input);
			data->touch1_down = false;

			add_finger_release_to_queue(data,
						    &chip_status->touchscr[0]);
		}
		/*
		 * If touch two is not active and is the first time we
		 * detect it, set a Finger-Up data.
		 */
		if ((!(chip_status->general_status_2 & TS1_DET))
		    && (chip_status->touchscr[1].area != 0)) {
			chip_status->touchscr[1].x = 0;
			chip_status->touchscr[1].y = 0;
			chip_status->touchscr[1].size = 0;
			chip_status->touchscr[1].area = 0;
		}
		break;
	case QT_TOUCHSCR_0_X:
	case QT_TOUCHSCR_1_X:
		icnt = (frame.addr == QT_TOUCHSCR_0_X) ? 0 : 1;

		chip_status->touchscr[icnt].x = frame.data[0];
		chip_status->touchscr[icnt].y = frame.data[2];
		chip_status->touchscr[icnt].size = frame.data[1] & 0x3f;
		chip_status->touchscr[icnt].area = frame.data[3] & 0x3f;

		if (frame.addr == QT_TOUCHSCR_0_X
		    && (chip_status->general_status_2 & TS0_DET)) {
			int listEmpty = list_empty(&chip_status->touchscr[0].link);
			int addEntry = 1;
			struct ts_qt5480_touch *last = NULL;

			if (!data->touch1_down) {
				data->touch1_down = true;
				input_report_key(input, BTN_TOUCH, 1);
			}

			input_report_abs(input, ABS_X,
					 chip_status->touchscr[0].x);
			input_report_abs(input, ABS_Y,
					 chip_status->touchscr[0].y);
			input_report_abs(input, ABS_PRESSURE,
					 chip_status->touchscr[0].size);
			input_sync(input);

			if (!listEmpty) {
				last = list_entry(chip_status->touchscr[0].link.prev,
						  struct ts_qt5480_touch, link);
					addEntry = (last->area == 0);
			}

			if (!addEntry) {
				/* update last unread entry in the list with current coordinates */
				last->x = chip_status->touchscr[0].x;
				last->y = chip_status->touchscr[0].y;
				last->area = chip_status->touchscr[0].area;
				last->size = chip_status->touchscr[0].size;
				dev_dbg(&data->client->dev,
					"Updat %p %d %d %d\n", last,
					last->x, last->y, last->area);
			} else {
				struct ts_qt5480_touch *touch = kmalloc(sizeof(struct ts_qt5480_touch),
								   GFP_KERNEL);
				if (likely(touch)) {
					*touch = chip_status->touchscr[0];
					list_add_tail(&touch->link,
						      &chip_status->touchscr[0].link);
					dev_dbg(&data->client->dev,
						"Added %p %d %d %d\n",
						touch, touch->x, touch->y,
						touch->area);
				}
			}
		} else if (frame.addr == QT_TOUCHSCR_1_X) {
			/* save slider info for Touch 1 packet */
			for (icnt = 0; icnt < 4; icnt++)
				chip_status->slider[icnt] = frame.data[icnt];
		}

		break;
	case QT_SLIDER_4:
		for (icnt = 0; icnt < 2; icnt++)
			chip_status->slider[icnt + 4] = frame.data[icnt];

		break;
	case QT_TOUCH_0_GESTURE:
	case QT_TOUCH_1_GESTURE:
		icnt = (frame.addr == QT_TOUCH_0_GESTURE) ? 0 : 1;

		chip_status->gesture[icnt].event = frame.data[0] & 0x0f;
		chip_status->gesture[icnt].dir =
			(frame.data[0] & 0x70) >> 4;
		chip_status->gesture[icnt].dist =
			(frame.data[1] & 0xf0) >> 4;
		chip_status->gesture[icnt].x =
			(frame.data[2] << 2) +
			((frame.data[1] >> 2) & 0x03);
		chip_status->gesture[icnt].y = frame.data[3];
		/* flag gesture received */
		chip_status->update_flags |= GESTURE_0_UPDATE << icnt;

		break;
	default:
		break;
	}
}

static void ts_qt5480_mapping(struct ts_qt5480_data *data,
			      const struct ts_qt5480_touch *touch,
			      long *pX, long *pY)
{
	ts_qt5480_mapping_t *phy_map = data->phy_map;
	unsigned short x_sen_res = phy_map->x_sensor_res;
	unsigned short x_scr_res = phy_map->x_screen_res;
	unsigned short x_flip = phy_map->x_flip;
	unsigned short x_sen_size = phy_map->x_sensor_size;
	unsigned short x_scr_size = phy_map->x_screen_size;
	short x_sen_ofst = phy_map->x_sensor_offset;

	unsigned short y_sen_res = phy_map->y_sensor_res;
	unsigned short y_scr_res = phy_map->y_screen_res;
	unsigned short y_flip = phy_map->y_flip;
	unsigned short y_sen_size = phy_map->y_sensor_size;
	unsigned short y_scr_size = phy_map->y_screen_size;
	short y_sen_ofst = phy_map->y_sensor_offset;

	long x = 0;
	long y = 0;

	x = touch->x;
	y = touch->y;

	/* Resolution and orientation */
	x *= x_scr_res;
	x /= x_sen_res;
	if (x_flip)
		x = x_scr_res - x;

	/* Physical Mapping */
	x *= x_sen_size;
	x /= x_scr_size;
	x += (x_sen_ofst * x_scr_res) / x_scr_size;

	/* Resolution and orientation */
	y *= y_scr_res;
	y /= y_sen_res;
	if (y_flip)
		y = y_scr_res - y;

	/* Physical Mapping */
	y *= y_sen_size;
	y /= y_scr_size;
	y += (y_sen_ofst * y_scr_res) / y_scr_size;

	/* Return the data */
	(*pX) = x;
	(*pY) = y;
}

static int ts_qt5480_start_input(struct ts_qt5480_data *data)
{
	/* Set Mutex protection */
	mutex_lock(&data->mutex);

	if (data->used) {
		data->used++;
		dev_dbg(&data->client->dev,
			"ts_qt5480_open() - Warning: It is was already in use!!!\n");
	} else {
		data->used++;

		dev_dbg(&data->client->dev,
			"ts_qt5480_open() - Device Opened!!!\n");
	}

	/* Release Mutex protecction */
	mutex_unlock(&data->mutex);

	return 0;
}

static int ts_qt5480_open(struct inode *inode, struct file *file)
{
	struct ts_qt5480_data *data = ts_qt5480_data;
	int ret;

	if (!data) {
		pr_err("ts_qt5480_open() - Error: Device not attached\n");
		return -ENODEV;
	}

	dev_dbg(&data->client->dev,
		"ts_qt5480_open(inode %#x, file %#x)\n",
		(unsigned int)inode, (unsigned int)file);

	ret = ts_qt5480_start_input(data);

	if (!ret)
		file->private_data = data;

	dev_dbg(&data->client->dev,
		"ts_qt5480_open returned %d (inode %#x, file %#x)\n",
		ret, (unsigned int)inode, (unsigned int)file);
	return ret;
}

static int ts_qt5480_release(struct inode *inode, struct file *file)
{
	struct ts_qt5480_data *data = file->private_data;

	dev_dbg(&data->client->dev, "ts_qt5480_release(inode %#x, file %#x)\n",
		(unsigned int)inode, (unsigned int)file);

	mutex_lock(&data->mutex);

	if (data->used) {
		data->used--;

		if (!data->used) {

			dev_dbg(&data->client->dev,
				"ts_qt5480_release() - Device Closed!!!\n");

		} else {
			dev_dbg(&data->client->dev,
				"ts_qt5480_release() - Warning: It is still in use!!!\n");
		}
	} else {
		dev_err(&data->client->dev,
			"ts_qt5480_release() - Error: Device never opened!!!\n");

		mutex_unlock(&data->mutex);
		return -EFAULT;
	}

	mutex_unlock(&data->mutex);
	return 0;
}

static ssize_t ts_qt5480_read(struct file *file, char *buf, size_t count,
			      loff_t *ppos)
{
	struct ts_qt5480_data *data = file->private_data;
	ts_event event = { };

	dev_dbg(&data->client->dev,
	      "ts_qt5480_read(file %#x, buf %#x, count %#x, ppos %#x)\n",
	      (unsigned int)file, (unsigned int)buf, count,
	      (unsigned int)ppos);

	mutex_lock(&data->mutex);

	if (count >= sizeof(event)) {
		if (!list_empty(&data->chip_status.touchscr[0].link)) {
			struct ts_qt5480_touch *touch =
			    list_entry(data->chip_status.touchscr[0].link.next,
				       struct ts_qt5480_touch, link);

			event.pressure = touch->area;
			if (event.pressure == 0) {
				event.x = 0;
				event.y = 0;
			} else {
				unsigned long x, y;

				ts_qt5480_mapping(data, touch, &x, &y);

				event.x = (unsigned short)x;
				event.y = (unsigned short)y;
			}
			dev_dbg(&data->client->dev,
				"Read_ %p %d %d %d\n", touch,
				touch->x, touch->y, touch->area);

			list_del(&touch->link);
			kfree(touch);

			/* Copy Data to user */
			if (copy_to_user((ts_event *) buf, &event,
					 sizeof(event))) {
				dev_dbg(&data->client->dev,
					"ts_qt5480_read() - Error: Copying data to the user area\n");

				mutex_unlock(&data->mutex);
				return -EFAULT;
			}

		} else {
			/* No data to send */
			count = 0;
		}
	}

	mutex_unlock(&data->mutex);
	return count;
}

static long ts_qt5480_ioctl(struct file *file,
			    unsigned int cmd, unsigned long arg)
{
	struct ts_qt5480_data *data = file->private_data;
	long err = -EINVAL;

	dev_dbg(&data->client->dev,
		"ts_qt5480_ioctl(file %#x, cmd %i, arg %i)\n",
		(unsigned int)file, cmd, (unsigned int)arg);

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != QT5480_IOCTL) {
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - Error: Don't do family %#x\n",
			_IOC_TYPE(cmd));
		return -ENOTTY;
	}

	/*
	 * the direction is a bitmask, and VERIFY_WRITE catches R/W
	 * transfers. `Type' is user-oriented, while
	 * access_ok is kernel-oriented, so the concept of "read" and
	 * "write" is reversed
	 */
	if (_IOC_DIR(cmd) & _IOC_READ) {
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - read - arg %#x\n",
			(int)arg);
		err = !access_ok(VERIFY_WRITE, (void *)arg, _IOC_SIZE(cmd));
	} else if (_IOC_DIR(cmd) & _IOC_WRITE) {
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - write - arg %#x\n",
			(int)arg);
		err = !access_ok(VERIFY_READ, (void *)arg, _IOC_SIZE(cmd));
	}

	if (err) {
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - Error: access error!\n");
		return -EFAULT;
	} else {
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - access OK\n");
	}

	mutex_lock(&data->mutex);

	switch (cmd) {
	case QT5480_CALIBRATE:
		/* Send a Calibrate Command */
		dev_dbg(&data->client->dev,
			"ts_qt5480_ioctl() - QT5480_CALIBRATE\n");

		err = ts_qt5480_write_reg(data, QT_CALIBRATE, 0x1);

		if (err < 0) {
			dev_err(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_CALIBRATE - Failed i2c_transfer\n");
			goto out;
		}

		break;
	case QT5480_POWER: {
		/* Switch ON / OFF the device */
		unsigned int power_on;

		if (copy_from_user(&power_on, (int *)arg, sizeof(int))) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_POWER - failed copy_from_user\n");
			err = -EFAULT;
			goto out;
		}

		err = ts_qt5480_write_reg(data, QT_CALIBRATE,
					  ((power_on) ?
					   data->config[QT_LP_MODE].value :
					   0x00));

		if (err < 0) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_POWER - Failed i2c_transfer\n");
			goto out;
		}

		break;
	}
	case QT5480_DEBUG:
		/* Enable / Disable Debug tracking in the PDP Memory (TFT) */
		/* not implemented */
		return -EINVAL;
		break;
	case QT5480_GETREG: {
		/* Read Register */
		struct ts_qt5480_frame frame;

		if (copy_from_user(&frame, (int *)arg,
				   sizeof(struct ts_qt5480_frame))) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_GETREG - failed copy_from_user\n");
			err = -EFAULT;
			goto out;
		}

		frame.stat = ts_qt5480_read_reg(data, frame.addr, frame.data);
		if (frame.stat < 0) {
			dev_err(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_GETREG - Failed i2c_transfer\n");
			err = frame.stat;
			goto out;
		}

		if (copy_to_user((int *)arg, &frame,
				 sizeof(struct ts_qt5480_frame))) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_GETREG - failed copy_to_user\n");
				err = -EFAULT;
				goto out;
		}

		break;
	}
	case QT5480_SETREG: {
		/* Write Register */
		struct ts_qt5480_frame frame;

		if (copy_from_user(&frame, (int *)arg,
				   sizeof(struct ts_qt5480_frame))) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_SETREG - failed copy_from_user\n");
			err = -EFAULT;
			goto out;
		}

		frame.stat = ts_qt5480_write_reg(data, frame.addr,
						 frame.data[0]);
		if (frame.stat < 0) {
			dev_err(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_SETREG - Failed i2c_transfer\n");
			err = frame.stat;
			goto out;
		}

		if (copy_to_user((int *)arg, &frame,
				 sizeof(struct ts_qt5480_frame))) {
			dev_dbg(&data->client->dev,
				"ts_qt5480_ioctl() - Error: QT5480_SETREG - failed copy_to_user\n");
				err = -EFAULT;
				goto out;
		}

		break;
	}
	}

out:
	/* Clean up */
	mutex_unlock(&data->mutex);

	return err;
}

static int ts_qt5480_input_open(struct input_dev *dev)
{
	struct ts_qt5480_data *data = input_get_drvdata(dev);

	return ts_qt5480_start_input(data);
}

/* attach to an instance of the device that was probed on a bus.
 * This function is only called if i2c_probe determined that some device
 * does actually exist at this address
 */
static int ts_qt5480_probe(struct i2c_client *client,
			   const struct i2c_device_id *idp)
{
	struct qt5480_platform_data *pdata = client->dev.platform_data;
	struct ts_qt5480_data *data;
	struct input_dev *input_dev;
	int err = 0;

	if (!pdata || !pdata->poll_status || !pdata->phy_map ||
	    !pdata->config) {
		dev_err(&client->dev, "valid platform data is required!\n");
		return -EINVAL;
	}

	data = kzalloc(sizeof(struct ts_qt5480_data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		err = -ENOMEM;
		goto err_mem_alloc;
	}

	data->dev_addr = client->addr;
	data->i2c_adap = client->adapter;
	data->client = client;
	data->input = input_dev;
	data->poll_status = pdata->poll_status;
	data->phy_map = pdata->phy_map;
	data->config = pdata->config;

	input_set_drvdata(input_dev, data);

	mutex_init(&data->mutex);

	INIT_LIST_HEAD(&data->chip_status.touchscr[0].link);
	/* Not used now, but better keep it initialised */
	INIT_LIST_HEAD(&data->chip_status.touchscr[1].link);

	snprintf(data->phys, sizeof(data->phys),
		 "%s/input0", dev_name(&client->dev));

	input_dev->name = "QT5480 Touchscreen";
	input_dev->phys = data->phys;
	input_dev->id.bustype = BUS_I2C;

	input_dev->open = ts_qt5480_input_open;

	input_dev->evbit[0] = BIT_MASK(EV_KEY) | BIT_MASK(EV_ABS);
	input_dev->keybit[BIT_WORD(BTN_TOUCH)] = BIT_MASK(BTN_TOUCH);

	input_set_abs_params(input_dev, ABS_X, 0, data->phy_map->x_sensor_res,
			     0, 0);
	input_set_abs_params(input_dev, ABS_Y, 0, data->phy_map->y_sensor_res,
			     0, 0);
	input_set_abs_params(input_dev, ABS_PRESSURE, 0, MAX_PRESSURE_VALUE,
			     0, 0);

	/* Reset the controller. */
	err = ts_qt5480_reset(data);
	if (err < 0) {
		dev_warn(&client->dev, "could not reset controller\n");
		goto err_mem_alloc;
	}

	/* Configure and calibrate the controller. */
	if (ts_qt5480_send_config(data) < 0) {
		dev_warn(&client->dev, "could not configure device\n");
	}

	if (request_threaded_irq(client->irq, ts_qt5480_isr_check,
				 ts_qt5480_isr, IRQF_ONESHOT,
				 "ts_qt5480", data)) {
		dev_err(&client->dev, "failed to register irq\n");
		goto err_mem_alloc;
	}

	err = input_register_device(input_dev);
	if (err) {
		dev_err(&client->dev, "failed to register input device: %d\n",
			err);
		goto err_request_irq;
	}

	i2c_set_clientdata(client, data);

	return 0;

err_request_irq:
	free_irq(client->irq, data);
err_mem_alloc:
	input_free_device(input_dev);
	kfree(data);
	return err;
}

static int ts_qt5480_remove(struct i2c_client *client)
{
	struct ts_qt5480_data *data = i2c_get_clientdata(client);

	input_unregister_device(data->input);
	free_irq(client->irq, data);
	kfree(data);
	i2c_set_clientdata(client, NULL);

	return 0;
}

/* i2c Support Data */
static const unsigned short normal_i2c[] = {
	QT5480_SLAVE_ADDRESS, I2C_CLIENT_END
};

static const struct i2c_device_id ts_qt5480_id[] = {
	{QT5480_NAME, 0},
	{}
};

MODULE_DEVICE_TABLE(i2c, ts_qt5480_id);

static struct i2c_driver i2c_driver_qt5480 = {
	.driver = {
	      .name	= QT5480_NAME,
	},
	.probe		= ts_qt5480_probe,
	.remove		= ts_qt5480_remove,
	.id_table	= ts_qt5480_id,
	.address_list	= normal_i2c,
};

/* File Ops Support Data */
static const struct file_operations ts_qt5480_fops = {
	.owner		= THIS_MODULE,
	.open		= ts_qt5480_open,
	.release	= ts_qt5480_release,
	.read		= ts_qt5480_read,
	.unlocked_ioctl	= ts_qt5480_ioctl,
};

static struct miscdevice ts_qt5480_misc_device = {
	.minor          = MISC_DYNAMIC_MINOR,
	.name           = "qt5480",
	.fops           = &ts_qt5480_fops,
};

int __init ts_qt5480_init(void)
{
	int err;

	err = i2c_add_driver(&i2c_driver_qt5480);
	err = 0;
	if (err) {
		pr_err("ts_qt5480: failed to add i2c driver: %d\n", err);
		return err;
	}

	/* Now create the /dev part. */
	err = misc_register(&ts_qt5480_misc_device);
	if (err < 0) {
		i2c_del_driver(&i2c_driver_qt5480);
		pr_err("ts_qt5480: cannot register misc device: %d\n", err);
		return err;
	}

	return 0;
}

static void __exit ts_qt5480_exit(void)
{
	misc_deregister(&ts_qt5480_misc_device);
	i2c_del_driver(&i2c_driver_qt5480);
}

module_init(ts_qt5480_init);
module_exit(ts_qt5480_exit);

MODULE_AUTHOR("Pedro Teixido-Rovira <pedro.teixido-rovira@pure.com>");
MODULE_AUTHOR("Marcin Nowakowski <marcin.nowakowski@pure.com>");
MODULE_DESCRIPTION("Atmel QT5480 Touchscreen driver");
MODULE_LICENSE("GPL");
