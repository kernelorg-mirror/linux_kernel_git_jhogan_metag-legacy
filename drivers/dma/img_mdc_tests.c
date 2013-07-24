/*
 * img_mdc_tests.c
 *
 *  Created on: 15-Jul-2009
 *  Modified on: 02-Apr-2013
 *      Author: neil jones, markos chandras
 *
 *  Module to test the simple mdc_dma driver using the DMA Engine API.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/io.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/img_mdc_dma.h>

MODULE_LICENSE("GPL");

u8 *buffer1;
u8 *buffer2;
static dma_addr_t hw_address1;
static dma_addr_t hw_address2;

static int do_single_shot(int chan, u32 src, u32 dest, u32 size,
		enum img_dma_priority prio)
{
	int channel;
	struct dma_chan *dchan;
	struct mdc_dma_cookie cookie;
	dma_cap_mask_t mask;
	unsigned long delay;
	struct dma_device *dev;
	struct dma_async_tx_descriptor *tx;
	dma_cookie_t dma_cookie;
	struct mdc_dma_tx_control tx_control;

	tx_control.flags = MDC_PRIORITY|MDC_NEED_THREAD;
	tx_control.thread_type = MDC_THREAD_FAST;
	tx_control.prio = IMG_DMA_PRIO_BULK;

	dma_cap_zero(mask);
	dma_cap_set(DMA_MEMCPY, mask);

	cookie.periph = 0;
	cookie.req_channel = chan;

	dchan = dma_request_channel(mask, &mdc_dma_filter_fn, &cookie);

	if (!dchan) {
		pr_err("Failed to allocate channel %d\n",
		       cookie.req_channel);
		return -EBUSY;
	}

	channel = cookie.req_channel;

	dev = dchan->device;

	/* We store the desired priority in channel's private struct */

	dchan->private = (void *)&tx_control;
	/*
	 * Never pass DMA_PREP_INTERRUPT to flags
	 * Use the dmatest module to test this
	 * codepath in the MDC driver.
	 */
	tx = dev->device_prep_dma_memcpy(dchan,
					 dest, src,
					 size,
					 DMA_CTRL_ACK);
	dma_cookie = dmaengine_submit(tx);
	/* Make the transfer */
	dma_async_issue_pending(dchan);

	/*wait a little*/
	udelay(100);

	/*check for completion*/
	delay = jiffies + size * HZ / 1000;
	/*allow 1 ms per byte anything slower than this and
	 you might as well move the data a bit at a time by hand!!*/
	while (time_before(jiffies, delay) &&
	       (dma_async_is_tx_complete(dchan, dma_cookie, NULL, NULL) ==
		DMA_IN_PROGRESS))
		cpu_relax();

	if (dma_async_is_tx_complete(dchan, dma_cookie, NULL, NULL) ==
	    DMA_IN_PROGRESS)
		pr_err("mdc_tests: Single shot operation timed out\n");

	dma_release_channel(dchan);

	return 0;
}

#define TEST_BUFF_SIZE 0x2000

struct test_data_tag {
	u32 src_addr_offset;
	u32 dest_addr_offset;
	u32 size;
	u8 fill_patten;
	const char *test_name;
};

static const struct test_data_tag test_data[] = {
{ 0x00, 0x01, 1, 0x55,
		"single byte from word aligned addr to byte aligned addr" },
{ 0x00, 0x02, 1, 0xA5,
		"single byte from word aligned addr to short aligned addr" },
{ 0x00, 0x04, 1, 0x5A,
		"single byte from word aligned addr to word aligned addr" },
{ 0x00, 0x08, 1, 0x11,
		"single byte from word aligned addr to double aligned addr" },
{ 0x01, 0x00, 1, 0x12,
		"single byte from byte aligned addr to aligned addr" },
{ 0x02, 0x00, 1, 0x13,
		"single byte from short aligned addr to aligned addr" },
{ 0x00, 0x01, 2, 0x14,
		"2 bytes from word aligned addr to byte aligned addr" },
{ 0x00, 0x02, 2, 0x15,
		"2 bytes from word aligned addr to short aligned addr" },
{ 0x00, 0x04, 2, 0x16,
		"2 bytes from word aligned addr to word aligned addr" },
{ 0x01, 0x00, 2, 0x17,
		"2 bytes from byte aligned addr to word  aligned addr" },
{ 0x02, 0x00, 2, 0x18,
		"2 bytes from short aligned addr to word aligned addr" },
{ 0x00, 0x01, 4, 0x19,
		"4 bytes from word aligned addr to byte aligned addr" },
{ 0x00, 0x02, 4, 0x1A,
		"4 bytes from word aligned addr to short aligned addr" },
{ 0x00, 0x04, 4, 0x1B,
		"4 bytes from word aligned addr to word aligned addr" },
{ 0x01, 0x00, 4, 0x1C,
		"4 bytes from byte aligned addr to word  aligned addr" },
{ 0x02, 0x00, 4, 0x1D,
		"4 bytes from short aligned addr to word aligned addr" },
{ 0x00, 0x100, 0x100, 0x1E,
		"0x100 byte block, from/to aligned address" },
{ 0x00, 0x101, 0x100, 0x1F,
		"0x100 byte block, to odd address" },
{ 0x01, 0x100, 0x100, 0x20,
		"0x100 byte block, from odd address" },
{ 0x00, 0x00, 0x1000, 0xDA, "big block" } };

static const int test_data_size = ARRAY_SIZE(test_data);

static void single_shot_address_range_tests(
		const struct test_data_tag *test_data, const int no_of_tests)
{
	int i;
	int passed = 0;

	pr_debug("mdc_tests: Running %d single shot address range tests\n",
		 no_of_tests);
	for (i = 0; i < no_of_tests; i++) {
		/*fill the source buffer with test pattern*/
		memset((u8 *) (buffer1 + test_data[i].src_addr_offset),
				test_data[i].fill_patten, test_data[i].size);
		/*clear destination buffer*/
		memset((u8 *) (buffer2 + test_data[i].dest_addr_offset), 0x00,
				test_data[i].size);

		/*do the access*/
		if (!do_single_shot(-1,
				    hw_address1 + test_data[i].src_addr_offset,
				hw_address2 + test_data[i].dest_addr_offset,
				test_data[i].size, IMG_DMA_PRIO_BULK)) {

			/*test the result*/
			if (memcmp((u8 *) buffer1 +
				   test_data[i].src_addr_offset,
				   (u8 *) buffer2 +
				   test_data[i].dest_addr_offset,
				   test_data[i].size)) {
				pr_err("mdc_tests: Failing Test: %s\n",
				       test_data[i].test_name);
			} else
				passed++;
		}
	}
	if (passed == no_of_tests)
		pr_info("mdc_tests: All address range tests passed\n");

	else
		pr_err("mdc_tests: Address range tests: %d of %d tests passed\n",
		       passed, no_of_tests);

}

static int single_shot_simple_test(int channel, enum img_dma_priority priority)
{
	/*fill the source buffer with test pattern*/
	memset(buffer1, 0xA5, TEST_BUFF_SIZE);
	/*clear destination buffer*/
	memset(buffer2, 0x00, TEST_BUFF_SIZE);

	/*do the transfer*/
	if (!do_single_shot(channel, hw_address1, hw_address2,
			TEST_BUFF_SIZE, priority))
		/*test the result*/
		return memcmp(buffer1, buffer2, TEST_BUFF_SIZE);
	else
		return -1;
}

#define MAX_CHANNELS 7
#define START_CHANNEL 3 /* First channels are likely to be busy */
static int __init mdc_tests_init(void)
{
	int passed = 0, i;

	buffer1 = dma_alloc_coherent(NULL,
				TEST_BUFF_SIZE, &hw_address1, GFP_KERNEL);
	buffer2 = dma_alloc_coherent(NULL,
				TEST_BUFF_SIZE, &hw_address2, GFP_KERNEL);

	memset(buffer1, 0x00, TEST_BUFF_SIZE);
	memset(buffer2, 0x00, TEST_BUFF_SIZE);

	single_shot_address_range_tests(test_data, test_data_size);

	for (i = START_CHANNEL; i < MAX_CHANNELS; i++) {
		if (single_shot_simple_test(i, IMG_DMA_PRIO_BULK))
			pr_err("mdc_tests: single shot test on channel %d failed\n",
			       i);
		else
			passed++;

	}
	pr_info("mdc_tests: Per channel tests: %d of %d passed\n",
		 passed, MAX_CHANNELS - START_CHANNEL);

	passed = 0 ;

	/* Pick a higher channel */
	if (single_shot_simple_test(5, IMG_DMA_PRIO_BULK))
		pr_err("mdc_tests: single shot test at bulk priority failed\n");
	else
		passed++;

	if (single_shot_simple_test(5, IMG_DMA_PRIO_REALTIME))
		pr_err("mdc_tests: single shot test at real time priority failed\n");
	else
		passed++;

	pr_info("mdc_tests: Per priority tests: %d of 2 tests passed\n",
		 passed);

	return 0;

}
module_init(mdc_tests_init)

static void __exit mdc_tests_exit(void)
{
	struct device dma_tester;
	dma_free_coherent(&dma_tester, TEST_BUFF_SIZE,
			(void *)buffer1, hw_address1);
	dma_free_coherent(&dma_tester, TEST_BUFF_SIZE,
			(void *)buffer2, hw_address2);
}
module_exit(mdc_tests_exit)
