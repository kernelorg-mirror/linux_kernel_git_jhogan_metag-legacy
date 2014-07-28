/*
 * ImgTec PowerDown Controller (PDC) RTC
 *
 * Copyright 2010-2012 Imagination Technologies Ltd.
 *
 */

#include <linux/init.h>
#include <linux/rtc.h>
#include <linux/io.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/notifier.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/slab.h>
/* needed for clk32k interface */
#include <asm/soc-tz1090/clock.h>

#define HW_EPOCH		2000
#define HW_YEARS		 100
#define LIN_EPOCH		1900
#define HW2LIN_EPOCH	(HW_EPOCH - LIN_EPOCH)
#define LIN2HW_EPOCH	(LIN_EPOCH - HW_EPOCH)

/* Registers */
#define PDC_RTC_CONTROL			0x00
#define PDC_RTC_SEC			0x04
#define PDC_RTC_MIN			0x08
#define PDC_RTC_HOUR			0x0c
#define PDC_RTC_DAY			0x10
#define PDC_RTC_MON			0x14
#define PDC_RTC_YEAR			0x18
#define PDC_RTC_ASEC			0x1c
#define PDC_RTC_AMIN			0x20
#define PDC_RTC_AHOUR			0x24
#define PDC_RTC_ADAY			0x28
#define PDC_RTC_AMON			0x2c
#define PDC_RTC_AYEAR			0x30
#define PDC_RTC_IRQ_STATUS		0x34
#define PDC_RTC_IRQ_CLEAR		0x38
#define PDC_RTC_IRQ_EN			0x3c

/* Register field masks */
#define PDC_RTC_CONTROL_GAE		0x08	/* global alarm enable */
#define PDC_RTC_CONTROL_FAST		0x04
#define PDC_RTC_CONTROL_UPDATE		0x02
#define PDC_RTC_CONTROL_CE		0x01	/* clock enable */
#define PDC_RTC_SEC_SEC			0x3f
#define PDC_RTC_MIN_MIN			0x3f
#define PDC_RTC_HOUR_HOUR		0x1f
#define PDC_RTC_DAY_DAY			0x1f
#define PDC_RTC_MON_MON			0x0f
#define PDC_RTC_YEAR_YEAR		0x7f
#define PDC_RTC_ASEC_EN			0x40
#define PDC_RTC_ASEC_ASEC		0x3f
#define PDC_RTC_AMIN_EN			0x40
#define PDC_RTC_AMIN_AMIN		0x3f
#define PDC_RTC_AHOUR_EN		0x20
#define PDC_RTC_AHOUR_AHOUR		0x1f
#define PDC_RTC_ADAY_EN			0x20
#define PDC_RTC_ADAY_ADAY		0x1f
#define PDC_RTC_AMON_EN			0x10
#define PDC_RTC_AMON_AMON		0x0f
#define PDC_RTC_AYEAR_EN		0x80
#define PDC_RTC_AYEAR_AYEAR		0x7f
#define PDC_RTC_IRQ_ALARM		0x04
#define PDC_RTC_IRQ_MIN			0x02
#define PDC_RTC_IRQ_SEC			0x01

/**
 * struct pdc_rtc_priv - Private PDC RTC data.
 * @rtc_dev:		RTC device structure.
 * @dev:		Platform device (used for dev_dbg messages etc).
 * @irq:		IRQ number of RTC device.
 * @reg_base:		Base of registers memory.
 * @nonvolatile_base:	Base of non-volatile registers if provided.
 * @nonvolatile_len:	Length of non-volatile registers.
 * @time_set_delay:	Number of seconds it takes to set the time.
 * @alarm_irq_delay:	Number of seconds the alarm IRQ is delayed.
 * @clk_nb:		Notifier block for clock notify events.
 * @alarm_pending:	Whether an alarm has fired and hasn't been handled.
 * @lock:		Protects PDC_RTC_CONTROL, control_reg, and
 *			softalrm_sec.
 * @control_reg:	Back up of PDC_RTC_CONTROL to work around buggy
 *			hardware. It takes time for some last written values to
 *			take effect, and later writes can replace a write that
 *			hasn't taken effect yet.
 * @softalrm_sec:	Software alarm second, for emulating alarms which need
 *			to fire very soon, which would otherwise foil our work
 *			around for late alarm interrupts.
 * @hardalrm_offset:	Offset from desired alarm time given to hardware.
 * @hardstop_time:	Time the alarm was stopped (or possibly second before).
 * @alrm_time:		Time of current alarm, for filtering out delayed
 *			cancelled alarm interrupts.
 * @adj_alrm_time:	Time of current adjusted alarm (for wrong clock rate).
 * @time_update_diff:	Difference between the old time and the new time, which
 *			should be used while hardware is still updating the
 *			time.
 * @suspended:		Whether the device is in suspend mode, in which case rtc
 *			interrupt events should be postponed until resume (see
 *			postponed_rtc_int).
 * @wakeup:		Whether the device can wake the system from a sleep
 *			state.
 * @postponed_rtc_int:	Postponed rtc interrupt flags to submit on resume.
 * @last_irq_en:	Preserved IRQ enable state when wakeup is in use.
 */
struct pdc_rtc_priv {
	struct rtc_device *rtc_dev;
	struct device *dev;
	int irq;
	void __iomem *reg_base;
	void __iomem *nonvolatile_base;
	unsigned long nonvolatile_len;
	unsigned int time_set_delay;
	unsigned int alarm_irq_delay;
	struct notifier_block clk_nb;

	int alarm_pending;
	spinlock_t lock;
	u32 control_reg;
	int softalrm_sec;
	int hardalrm_offset;
	unsigned long hardstop_time;
	unsigned long alrm_time;
	unsigned long adj_alrm_time;
	unsigned long time_update_diff;

	/* suspend data */
	bool suspended;
	bool wakeup;
	unsigned int postponed_rtc_int;
	unsigned int last_irq_en;
};

static void pdc_rtc_write(struct pdc_rtc_priv *priv,
		   unsigned int reg_offs, unsigned int data)
{
	iowrite32(data, priv->reg_base + reg_offs);
}

static unsigned int pdc_rtc_read(struct pdc_rtc_priv *priv,
			  unsigned int reg_offs)
{
	return ioread32(priv->reg_base + reg_offs);
}

static int pdc_rtc_write_nonvolatile(struct pdc_rtc_priv *priv,
				     unsigned int reg_offs, unsigned long in)
{
	if (reg_offs >= priv->nonvolatile_len)
		return -EINVAL;

	iowrite32(in, priv->nonvolatile_base + reg_offs);
	return 0;
}

static int pdc_rtc_read_nonvolatile(struct pdc_rtc_priv *priv,
				    unsigned int reg_offs, unsigned long *out)
{
	if (reg_offs >= priv->nonvolatile_len)
		return -EINVAL;

	*out = ioread32(priv->nonvolatile_base + reg_offs);
	return 0;
}


/* caller must hold lock. does not handle read during time update. */
static void _pdc_rtc_read_time_raw(struct pdc_rtc_priv *priv,
				   struct rtc_time *tm, int *updating)
{
	int min, upd = 0;

	/*
	 * If it takes time for the time to get set and an update is in
	 * progress, we need to check that the update is still in progress
	 * afterwards otherwise it will have changed while we were reading it.
	 */
	if (priv->time_set_delay)
		upd = pdc_rtc_read(priv, PDC_RTC_CONTROL)
			& PDC_RTC_CONTROL_UPDATE;
start_again:

	/*
	 * We re-read the minute at the end of the loop to check it hasn't
	 * changed while we were reading the others. If it has then we didn't
	 * read atomically and should try again. The second is allowed to
	 * change by itself as that won't result in an inconsistent time.
	 */
	min = pdc_rtc_read(priv, PDC_RTC_MIN) & PDC_RTC_MIN_MIN;
	do {
		tm->tm_sec  = (pdc_rtc_read(priv, PDC_RTC_SEC) &
			       PDC_RTC_SEC_SEC);
		tm->tm_min  = min;
		tm->tm_hour = (pdc_rtc_read(priv, PDC_RTC_HOUR) &
			       PDC_RTC_HOUR_HOUR);
		tm->tm_mday = (pdc_rtc_read(priv, PDC_RTC_DAY) &
			       PDC_RTC_DAY_DAY);
		tm->tm_mon  = (pdc_rtc_read(priv, PDC_RTC_MON) &
			       PDC_RTC_MON_MON) - 1;
		tm->tm_year = (pdc_rtc_read(priv, PDC_RTC_YEAR) &
			       PDC_RTC_YEAR_YEAR) + HW2LIN_EPOCH;
		if (upd) {
			upd = pdc_rtc_read(priv, PDC_RTC_CONTROL)
				& PDC_RTC_CONTROL_UPDATE;
			/* did the update finish while we were reading */
			if (!upd)
				goto start_again;
		}
		min = pdc_rtc_read(priv, PDC_RTC_MIN) & PDC_RTC_MIN_MIN;
		if (min != tm->tm_min)
			dev_dbg(priv->dev,
				"time read %02d:%02d:%02d nonatomic, retrying\n",
				tm->tm_hour, tm->tm_min, tm->tm_sec);
	} while (min != tm->tm_min);

	if (updating)
		*updating = upd;
}

/*
 * caller must hold lock.
 * does handle read during time update.
 * does not initialise tm->wday, tm->yday, or tm->tm_isdst
 */
static void _pdc_rtc_read_time(struct pdc_rtc_priv *priv, struct rtc_time *tm)
{
	int upd;
	unsigned long time;

	_pdc_rtc_read_time_raw(priv, tm, &upd);

	dev_dbg(priv->dev, "time read %02d:%02d:%02d\n",
		tm->tm_hour, tm->tm_min, tm->tm_sec);

	/* if we got the old time during an update, add the difference */
	if (upd && priv->time_update_diff) {
		rtc_tm_to_time(tm, &time);
		time += priv->time_update_diff;
		rtc_time_to_tm(time, tm);

		dev_dbg(priv->dev,
			"update was in progress, adjusting to %02d:%02d:%02d\n",
			tm->tm_hour, tm->tm_min, tm->tm_sec);
	}
}

static int pdc_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	_pdc_rtc_read_time(priv, tm);
	spin_unlock_irqrestore(&priv->lock, flags);

	tm->tm_wday  = -1;
	tm->tm_yday  = -1;
	tm->tm_isdst = -1;

	return 0;
}

static int pdc_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned int hw_year;
	unsigned int ctrl;
	unsigned long flags;
	unsigned long time, rtime;
	struct rtc_time tm_adj;

	dev_dbg(priv->dev, "time set %02d:%02d:%02d\n",
		tm->tm_hour, tm->tm_min, tm->tm_sec);
	/*
	 * Due to a hardware quirk the time may only be set after several
	 * seconds.
	 */
	if (priv->time_set_delay) {
		rtc_tm_to_time(tm, &time);
		rtc_time_to_tm(time + priv->time_set_delay, &tm_adj);
		tm = &tm_adj;
	}

	hw_year = tm->tm_year + LIN2HW_EPOCH;
	/* year must be in range */
	if (hw_year >= HW_YEARS)
		return -EINVAL;

	spin_lock_irqsave(&priv->lock, flags);

	/* write out the values */
	pdc_rtc_write(priv, PDC_RTC_SEC, tm->tm_sec);
	pdc_rtc_write(priv, PDC_RTC_MIN, tm->tm_min);
	pdc_rtc_write(priv, PDC_RTC_HOUR, tm->tm_hour);
	pdc_rtc_write(priv, PDC_RTC_DAY, tm->tm_mday);
	pdc_rtc_write(priv, PDC_RTC_MON, tm->tm_mon + 1);
	pdc_rtc_write(priv, PDC_RTC_YEAR, hw_year);

	/* update the clock with the written values */
	ctrl = priv->control_reg | PDC_RTC_CONTROL_UPDATE;
	pdc_rtc_write(priv, PDC_RTC_CONTROL, ctrl);

	/*
	 * Record the offset of the new time so that we can calculate the
	 * current time before the update is complete.
	 */
	if (priv->time_set_delay) {
		_pdc_rtc_read_time_raw(priv, &tm_adj, NULL);
		rtc_tm_to_time(&tm_adj, &rtime);
		priv->time_update_diff = time - rtime;
	}
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int pdc_rtc_alarm_enabled(struct pdc_rtc_priv *priv)
{
	return !!(priv->control_reg & PDC_RTC_CONTROL_GAE);
}

static int pdc_rtc_read_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long flags;
	unsigned long scheduled;
	int offset;

	spin_lock_irqsave(&priv->lock, flags);

	offset = priv->hardalrm_offset;

	/* Just get the register values */
	alrm->enabled = pdc_rtc_alarm_enabled(priv);
	alrm->pending = priv->alarm_pending;

	alrm->time.tm_sec  = pdc_rtc_read(priv, PDC_RTC_ASEC);
	alrm->time.tm_min  = pdc_rtc_read(priv, PDC_RTC_AMIN);
	alrm->time.tm_hour = pdc_rtc_read(priv, PDC_RTC_AHOUR);
	alrm->time.tm_mday = pdc_rtc_read(priv, PDC_RTC_ADAY);
	alrm->time.tm_mon  = pdc_rtc_read(priv, PDC_RTC_AMON);
	alrm->time.tm_year = pdc_rtc_read(priv, PDC_RTC_AYEAR);

	spin_unlock_irqrestore(&priv->lock, flags);

	/* Any misisng _EN bit translated to -1 */

	if (alrm->time.tm_sec & PDC_RTC_ASEC_EN)
		alrm->time.tm_sec &= PDC_RTC_ASEC_ASEC;
	else
		alrm->time.tm_sec = -1;

	if (alrm->time.tm_min & PDC_RTC_AMIN_EN)
		alrm->time.tm_min &= PDC_RTC_AMIN_AMIN;
	else
		alrm->time.tm_min = -1;

	if (alrm->time.tm_hour & PDC_RTC_AHOUR_EN)
		alrm->time.tm_hour &= PDC_RTC_AHOUR_AHOUR;
	else
		alrm->time.tm_hour = -1;

	if (alrm->time.tm_mday & PDC_RTC_ADAY_EN)
		alrm->time.tm_mday &= PDC_RTC_ADAY_ADAY;
	else
		alrm->time.tm_mday = -1;

	if (alrm->time.tm_wday & PDC_RTC_ADAY_EN)
		alrm->time.tm_wday &= PDC_RTC_ADAY_ADAY;
	else
		alrm->time.tm_wday = -1;

	if (alrm->time.tm_mon & PDC_RTC_AMON_EN)
		alrm->time.tm_mon = (alrm->time.tm_mon & PDC_RTC_AMON_AMON) - 1;
	else
		alrm->time.tm_mon = -1;

	if (alrm->time.tm_year & PDC_RTC_AYEAR_EN)
		alrm->time.tm_year = (alrm->time.tm_year & PDC_RTC_AYEAR_AYEAR)
					+ HW2LIN_EPOCH;
	else
		alrm->time.tm_year = -1;

	alrm->time.tm_wday  = -1;
	alrm->time.tm_yday  = -1;
	alrm->time.tm_isdst = -1;

	/*
	 * The alarm time in the hardware is offset to compensate for the late
	 * alarm interrupts, so we need to adjust it to get the original alarm.
	 */

	rtc_tm_to_time(&alrm->time, &scheduled);
	scheduled -= offset;
	rtc_time_to_tm(scheduled, &alrm->time);

	return 0;
}

/* caller must hold priv->lock */
static void _pdc_rtc_stop_alarm(struct pdc_rtc_priv *priv, unsigned long now)
{
	/* disable the secondly interrupt */
	pdc_rtc_write(priv, PDC_RTC_IRQ_EN, PDC_RTC_IRQ_ALARM);
	priv->softalrm_sec = -1;
	/* disable the global alarm enable bit */
	priv->control_reg &= ~PDC_RTC_CONTROL_GAE;
	pdc_rtc_write(priv, PDC_RTC_CONTROL, priv->control_reg);

	priv->hardstop_time = now;
}

static void pdc_rtc_stop_alarm(struct pdc_rtc_priv *priv)
{
	unsigned long flags;
	struct rtc_time tm;
	unsigned long now = 0;

	spin_lock_irqsave(&priv->lock, flags);
	if (priv->alarm_irq_delay) {
		_pdc_rtc_read_time(priv, &tm);
		rtc_tm_to_time(&tm, &now);
	}
	_pdc_rtc_stop_alarm(priv, now);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static void pdc_rtc_start_alarm(struct pdc_rtc_priv *priv)
{
	unsigned long flags;

	spin_lock_irqsave(&priv->lock, flags);
	priv->control_reg |= PDC_RTC_CONTROL_GAE;
	pdc_rtc_write(priv, PDC_RTC_CONTROL, priv->control_reg);
	spin_unlock_irqrestore(&priv->lock, flags);
}

static int _pdc_rtc_set_alarm(struct pdc_rtc_priv *priv, bool temporary,
			      unsigned long scheduled, struct rtc_wkalrm *alrm)
{
	struct rtc_time tm;
	unsigned long flags;
	unsigned long now, adjusted;
	struct rtc_wkalrm alrm_adj, *alrm_orig = alrm;

	spin_lock_irqsave(&priv->lock, flags);
	/*
	 * If we're compensating for hardware bugs, don't change the stored
	 * alarm time.
	 */
	if (!temporary)
		priv->alrm_time = scheduled;
	/*
	 * But still record the alarm being set so we can tell whether we need
	 * to change it back again.
	 */
	priv->adj_alrm_time = scheduled;

	/*
	 * Due to a hardware quirk the alarm may fire several seconds late, so
	 * rewind the scheduled alarm time to compensate.
	 */
	if (priv->alarm_irq_delay) {
		alrm_adj = *alrm;

		_pdc_rtc_read_time(priv, &tm);
		rtc_tm_to_time(&tm, &now);

try_again_locked:
		adjusted = scheduled - priv->alarm_irq_delay;

		/* disable the alarm while we set it up */
		_pdc_rtc_stop_alarm(priv, now);

		/* Make sure we're not setting the alarm in the past */
		if (scheduled <= now) {
			spin_unlock_irqrestore(&priv->lock, flags);
			return -ETIME;
		}
		/* If adjusted time is in past, emulate with a soft alarm */
		if (adjusted <= now && alrm->enabled) {
			/* clear and enable secondly interrupt */
			pdc_rtc_write(priv, PDC_RTC_IRQ_CLEAR, PDC_RTC_IRQ_SEC);
			pdc_rtc_write(priv, PDC_RTC_IRQ_EN,
				      PDC_RTC_IRQ_ALARM | PDC_RTC_IRQ_SEC);
			priv->softalrm_sec = alrm_orig->time.tm_sec;
			/* still set the real alarm as early as possible */
			adjusted = now + 1;
		}
		priv->hardalrm_offset = adjusted - scheduled;
		spin_unlock_irqrestore(&priv->lock, flags);

		rtc_time_to_tm(adjusted, &alrm_adj.time);
		alrm = &alrm_adj;

		dev_dbg(priv->dev, "alarm setting %02d:%02d:%02d\n",
			alrm->time.tm_hour, alrm->time.tm_min,
			alrm->time.tm_sec);
	} else {
		/* disable the alarm while we set it up */
		_pdc_rtc_stop_alarm(priv, 0);

		adjusted = scheduled;
		priv->hardalrm_offset = 0;
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	/*
	 * don't use fields set to -1
	 * all smaller fields than a field in use must also be in use
	 */

	if (alrm->time.tm_year >= 0) {
		tm.tm_year = alrm->time.tm_year + LIN2HW_EPOCH;
		/* year must be in range */
		if ((unsigned int)tm.tm_year > HW_YEARS)
			return -EINVAL;
		tm.tm_year |= PDC_RTC_AYEAR_EN;
	} else
		tm.tm_year = 0;

	if (alrm->time.tm_mon >= 0)
		tm.tm_mon = (alrm->time.tm_mon + 1) | PDC_RTC_AMON_EN;
	else if (tm.tm_year & PDC_RTC_AYEAR_EN)
		return -EINVAL;
	else
		tm.tm_mon = 0;

	if (alrm->time.tm_mday >= 0)
		tm.tm_mday = alrm->time.tm_mday | PDC_RTC_ADAY_EN;
	else if (tm.tm_mon & PDC_RTC_AMON_EN)
		return -EINVAL;
	else
		tm.tm_mday = 0;

	if (alrm->time.tm_hour >= 0)
		tm.tm_hour = alrm->time.tm_hour | PDC_RTC_AHOUR_EN;
	else if (tm.tm_mday & PDC_RTC_ADAY_EN)
		return -EINVAL;
	else
		tm.tm_hour = 0;

	if (alrm->time.tm_min >= 0)
		tm.tm_min = alrm->time.tm_min | PDC_RTC_AMIN_EN;
	else if (tm.tm_hour & PDC_RTC_AHOUR_EN)
		return -EINVAL;
	else
		tm.tm_min = 0;

	if (alrm->time.tm_sec >= 0)
		tm.tm_sec = alrm->time.tm_sec | PDC_RTC_ASEC_EN;
	else if (tm.tm_min & PDC_RTC_AMIN_EN)
		return -EINVAL;
	else
		tm.tm_sec = 0;

	spin_lock_irqsave(&priv->lock, flags);
	pdc_rtc_write(priv, PDC_RTC_ASEC,  tm.tm_sec);
	pdc_rtc_write(priv, PDC_RTC_AMIN,  tm.tm_min);
	pdc_rtc_write(priv, PDC_RTC_AHOUR, tm.tm_hour);
	pdc_rtc_write(priv, PDC_RTC_ADAY,  tm.tm_mday);
	pdc_rtc_write(priv, PDC_RTC_AMON,  tm.tm_mon);
	pdc_rtc_write(priv, PDC_RTC_AYEAR, tm.tm_year);
	priv->alarm_pending = 0;
	spin_unlock_irqrestore(&priv->lock, flags);

	/* re-enable the alarm if applicable */
	if (alrm->enabled) {
		pdc_rtc_start_alarm(priv);

		/*
		 * Check that setting the alarm didn't lose the race against the
		 * next clock tick (which may be the one we're trying to set the
		 * alarm on). It may be that the interrupt just hasn't been
		 * handled yet (e.g. on another CPU), but it does no harm to
		 * handle it here instead.
		 */
		spin_lock_irqsave(&priv->lock, flags);
		if (!priv->alarm_pending) {
			_pdc_rtc_read_time(priv, &tm);
			rtc_tm_to_time(&tm, &now);
			/* If it's too late, immediately trigger the alarm */
			if (scheduled <= now) {
				_pdc_rtc_stop_alarm(priv, now);
				dev_dbg(priv->dev,
					"alarm set race lost, triggering immediately\n");
				spin_unlock_irqrestore(&priv->lock, flags);
				return -ETIME;
			}
			/*
			 * If we've missed the window of oportunity to set the
			 * alarm interrupt, we need to reconsider.
			 */
			if (adjusted <= now) {
				dev_dbg(priv->dev,
					"alarm set race lost, retrying\n");
				goto try_again_locked;
			}
		}
		spin_unlock_irqrestore(&priv->lock, flags);
	}

	return 0;
}

static int pdc_rtc_adjust_alarm_time(struct pdc_rtc_priv *priv,
				     unsigned long scheduled)
{
	struct rtc_wkalrm alrm;
	alrm.enabled = 1;
	alrm.pending = 0;

	rtc_time_to_tm(scheduled, &alrm.time);
	return _pdc_rtc_set_alarm(priv, true, scheduled, &alrm);
}

static int pdc_rtc_set_alarm(struct device *dev, struct rtc_wkalrm *alrm)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned long scheduled;

	dev_dbg(priv->dev, "alarm set %02d:%02d:%02d\n",
		alrm->time.tm_hour, alrm->time.tm_min, alrm->time.tm_sec);

	rtc_tm_to_time(&alrm->time, &scheduled);
	return _pdc_rtc_set_alarm(priv, false, scheduled, alrm);
}

static int pdc_rtc_alarm_irq_enable(struct device *dev, unsigned int enabled)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);

	dev_dbg(priv->dev, "alarm irq enable %d\n",
		enabled);

	if (enabled)
		pdc_rtc_start_alarm(priv);
	else
		pdc_rtc_stop_alarm(priv);

	return 0;
}

static struct rtc_class_ops pdc_rtc_ops = {
	.read_time		= pdc_rtc_read_time,
	.set_time		= pdc_rtc_set_time,
	.read_alarm		= pdc_rtc_read_alarm,
	.set_alarm		= pdc_rtc_set_alarm,
	.alarm_irq_enable	= pdc_rtc_alarm_irq_enable,
};

static irqreturn_t pdc_rtc_isr(int irq, void *dev_id)
{
	struct pdc_rtc_priv *priv = dev_id;
	unsigned int status;
	unsigned long events = RTC_IRQF;
	unsigned long flags;
	struct rtc_time tm;
	unsigned long now = 0;
	u32 sec;

	spin_lock_irqsave(&priv->lock, flags);
	status = pdc_rtc_read(priv, PDC_RTC_IRQ_STATUS);
	pdc_rtc_write(priv, PDC_RTC_IRQ_CLEAR, status);

	/* ignore delayed alarm interrupts after turned alarm off */
	if (pdc_rtc_alarm_enabled(priv)) {
		if (status & PDC_RTC_IRQ_ALARM) {
			/*
			 * Alarm interrupt.
			 * If an alarm was cancelled, we may still get the
			 * delayed interrupt, so we need to check the current
			 * time has actually exceeded the alarm time.
			 * Of course if we've corrected the time we may also get
			 * a legitimate interrupt early, so we use hardstop_time
			 * to check whether the alarm could be the second after
			 * we've disabled it, which would indicate an echo.
			 */
			_pdc_rtc_read_time(priv, &tm);
			rtc_tm_to_time(&tm, &now);
			if (now >= priv->alrm_time) {
				events |= RTC_AF;
				dev_dbg(priv->dev, "isr alarm %02d:%02d:%02d\n",
					tm.tm_hour, tm.tm_min, tm.tm_sec);
			} else if (now > priv->hardstop_time &&
				   now <= priv->hardstop_time + 2) {
				dev_dbg(priv->dev,
					"isr alarm %02d:%02d:%02d ignored (echo %lu disabled %lu)\n",
					tm.tm_hour, tm.tm_min, tm.tm_sec,
					now, priv->hardstop_time);
			} else {
				/*
				 * It looks like a legitimate interrupt since we
				 * haven't just cancelled an alarm.
				 */
				events |= RTC_AF;
				dev_dbg(priv->dev,
					"isr alarm %02d:%02d:%02d (early %lu < %lu)\n",
					tm.tm_hour, tm.tm_min, tm.tm_sec,
					now, priv->alrm_time);
			}
		} else if (status & PDC_RTC_IRQ_SEC) {
			/*
			 * Secondly interrupt.
			 * Check if the current second matches the soft alarm.
			 */
			sec = pdc_rtc_read(priv, PDC_RTC_SEC);
			if (priv->softalrm_sec == sec)
				events |= RTC_AF;
			dev_dbg(priv->dev,
				"isr second %02d:%02d:%02d (compare %02d)\n",
				pdc_rtc_read(priv, PDC_RTC_HOUR),
				pdc_rtc_read(priv, PDC_RTC_MIN),
				sec, priv->softalrm_sec);
		}

		/* rtc alarms are one-shot */
		priv->hardstop_time = 0;
		if (events & RTC_AF) {
			if (!now && priv->alarm_irq_delay) {
				_pdc_rtc_read_time(priv, &tm);
				rtc_tm_to_time(&tm, &now);
			}
			priv->alarm_pending = 1;
			_pdc_rtc_stop_alarm(priv, now);
		}
	} else {
		/* make absolutely sure that the alarm is properly stopped */
		if (priv->alarm_irq_delay) {
			_pdc_rtc_read_time(priv, &tm);
			rtc_tm_to_time(&tm, &now);
		}
		_pdc_rtc_stop_alarm(priv, now);
		dev_dbg(priv->dev,
			"isr irq %#x ignored (alarm disabled)\n",
			status);
	}

	spin_unlock_irqrestore(&priv->lock, flags);

	if (events != RTC_IRQF)
		rtc_update_irq(priv->rtc_dev, 1, events);

	return IRQ_HANDLED;
}

/* Non-volatile registers available to the RTC */

/* The last time when the RTC was adjusted and should have been correct */
#define PDC_RTC_SRPROT_LASTTIME		0x0
/*
 * Sub-second accumulated skew due to the changes in clock frequency.
 * Fixed point number with shift of PDC_RTC_SWPROT_SKEW_BITS.
 * I.e. best_time = rtctime + skew
 * Must be in the range -0.5 <= X < 0.5.
 */
#define PDC_RTC_SRPROT_SKEW		0x4
#define PDC_RTC_SWPROT_SKEW_BITS	15

static void pdc_rtc_change_frequency(struct pdc_rtc_priv *priv,
				     struct clk32k_change_freq *change)
{
	unsigned long last_time;
	long skew, half;
	int skew_err, alrm_err, alrm_en;
	unsigned long now, adj;
	unsigned long orig_alrm_time, adj_alrm_time, alrm_time;
	unsigned long diff;
	u64 diff64;
	struct rtc_time tm;
	unsigned long flags;

	dev_dbg(priv->dev, "clk changed %lu HZ -> %lu HZ\n",
		change->old_freq, change->new_freq);

	/* adjust time since last recorded time */

	if (pdc_rtc_read_nonvolatile(priv, PDC_RTC_SRPROT_LASTTIME, &last_time))
		return;

	skew_err = pdc_rtc_read_nonvolatile(priv, PDC_RTC_SRPROT_SKEW,
					    (unsigned long *)&skew);
	/* sanity check range of skew */
	half = 1 << (PDC_RTC_SWPROT_SKEW_BITS - 1);
	if (!skew_err && (skew >= half ||
			  skew < -half))
		skew = 0;

	dev_dbg(priv->dev, "last_time = %lx, skew = %lx (%d)\n",
		last_time, skew, skew_err);

	pdc_rtc_read_time(priv->dev, &tm);
	rtc_tm_to_time(&tm, &now);

	if (last_time && change->old_freq != CLK32K_DESIRED_FREQUENCY) {
		diff = now - last_time;
		if ((long)diff > 0) {
			dev_dbg(priv->dev, "%#lx seconds since last change\n",
				diff);
			/* fixed point (using shift of SKEW_BITS) */
			diff64 = div_u64((u64)(CLK32K_DESIRED_FREQUENCY
					       << PDC_RTC_SWPROT_SKEW_BITS)
					 * diff, change->old_freq);
			/* add 0.5 so we round to closest */
			diff64 += half;
			if (!skew_err) {
				/* adjust using the accumulated clock skew */
				diff64 += skew;
				/*
				 * Update the skew from rounding the time to the
				 * nearest second.
				 */
				skew = diff64 &
					((1 << PDC_RTC_SWPROT_SKEW_BITS) - 1);
				skew -= half;
			}
			diff = diff64 >> PDC_RTC_SWPROT_SKEW_BITS;
			adj = last_time + diff;
			dev_dbg(priv->dev, "scaled to %#lx seconds\n",
				diff);
			if (adj != now) {
				rtc_time_to_tm(adj, &tm);
				pdc_rtc_set_time(priv->dev, &tm);
				now = adj;
			}
		}
	}

	/* adjust the alarm */
	spin_lock_irqsave(&priv->lock, flags);
	orig_alrm_time = priv->alrm_time;
	adj_alrm_time = priv->adj_alrm_time;
	alrm_en = pdc_rtc_alarm_enabled(priv);
	spin_unlock_irqrestore(&priv->lock, flags);

	if (orig_alrm_time && alrm_en) {
		if (change->new_freq == CLK32K_DESIRED_FREQUENCY) {
			dev_dbg(priv->dev, "%#lx seconds until alarm\n",
				orig_alrm_time - now);
			alrm_time = orig_alrm_time;
		} else {
			diff = orig_alrm_time - now;
			dev_dbg(priv->dev, "%#lx seconds until alarm\n",
				diff);
			/* scale time until alarm, rounding to closest second */
			diff = (2 * diff * change->new_freq + 1)
				/ (2 * CLK32K_DESIRED_FREQUENCY);
			alrm_time = now + diff;
			dev_dbg(priv->dev, "scaled to %#lx seconds\n",
				diff);
		}

		if (adj_alrm_time != alrm_time) {
			alrm_err = pdc_rtc_adjust_alarm_time(priv, alrm_time);
			if (alrm_err == -ETIME) {
				/* alarm has already expired, trigger */
				dev_dbg(priv->dev,
					"alarm expired during adjustment\n");
				priv->alarm_pending = 1;
				/* if suspended, postpone event until resume */
				if (priv->suspended)
					priv->postponed_rtc_int =
							RTC_IRQF | RTC_AF;
				else
					rtc_update_irq(priv->rtc_dev, 1,
						       RTC_IRQF | RTC_AF);
			}
		}
	}

	dev_dbg(priv->dev, "writing now = %lx, skew = %lx (%d)\n",
		now, skew, skew_err);

	pdc_rtc_write_nonvolatile(priv, PDC_RTC_SRPROT_LASTTIME, now);
	if (!skew_err)
		pdc_rtc_write_nonvolatile(priv, PDC_RTC_SRPROT_SKEW, skew);
}

static int pdc_rtc_clk_notify(struct notifier_block *self, unsigned long action,
			      void *data)
{
	struct pdc_rtc_priv *priv;

	priv = container_of(self, struct pdc_rtc_priv, clk_nb);
	switch (action) {
	case CLK32K_CHANGE_FREQUENCY:
		pdc_rtc_change_frequency(priv, data);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static int pdc_rtc_setup(struct pdc_rtc_priv *priv)
{
	unsigned long flags;

	/* enable appropriate interrupts */
	pdc_rtc_write(priv, PDC_RTC_IRQ_EN, PDC_RTC_IRQ_ALARM);

	/* make sure clock is enabled */

	spin_lock_irqsave(&priv->lock, flags);
	priv->control_reg |= PDC_RTC_CONTROL_CE;
	pdc_rtc_write(priv, PDC_RTC_CONTROL, priv->control_reg);
	spin_unlock_irqrestore(&priv->lock, flags);

	return 0;
}

static int pdc_rtc_probe(struct platform_device *pdev)
{
	struct pdc_rtc_priv *priv;
	struct resource *res_regs;
	struct resource *res_nonvolatile;
	struct device_node *node = pdev->dev.of_node;
	int irq, ret, error;
	u32 val;

	if (!node)
		return -ENOENT;

	/* Get resources from platform device */
	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "cannot find IRQ resource\n");
		error = irq;
		goto err_pdata;
	}

	res_regs = platform_get_resource_byname(pdev, IORESOURCE_MEM, "regs");
	if (res_regs == NULL) {
		dev_err(&pdev->dev, "cannot find registers resource\n");
		error = -ENOENT;
		goto err_pdata;
	}

	res_nonvolatile = platform_get_resource_byname(pdev, IORESOURCE_MEM,
						       "nonvolatile");
	/* nonvolatile registers are optional */

	/* Private driver data */
	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "cannot allocate device data\n");
		error = -ENOMEM;
		goto err_dev;
	}
	spin_lock_init(&priv->lock);
	platform_set_drvdata(pdev, priv);
	priv->dev = &pdev->dev;

	/* Get devicetree properties */
	ret = of_property_read_u32(node, "time-set-delay", &val);
	if (!ret)
		priv->time_set_delay = val;
	ret = of_property_read_u32(node, "alarm-irq-delay", &val);
	if (!ret)
		priv->alarm_irq_delay = val;

	/* Ioremap the registers */
	priv->reg_base = devm_ioremap(&pdev->dev, res_regs->start,
				      res_regs->end - res_regs->start);
	if (!priv->reg_base) {
		dev_err(&pdev->dev,
			"cannot ioremap registers\n");
		error = -EIO;
		goto err_regs;
	}

	/* Ioremap the non-volatile registers if available */
	if (res_nonvolatile) {
		priv->nonvolatile_len = res_nonvolatile->end -
						res_nonvolatile->start;
		priv->nonvolatile_base = devm_ioremap(&pdev->dev,
						      res_nonvolatile->start,
						      priv->nonvolatile_len);
		if (!priv->nonvolatile_base) {
			dev_err(&pdev->dev,
				"cannot ioremap nonvolatile registers\n");
			error = -EIO;
			goto err_regs;
		}
	}

	/* disable interrupts */
	pdc_rtc_write(priv, PDC_RTC_IRQ_EN, 0);

	priv->softalrm_sec = -1;
	priv->irq = irq;
	error = devm_request_irq(&pdev->dev, priv->irq, pdc_rtc_isr, 0,
				 "pdc-rtc", priv);
	if (error) {
		dev_err(&pdev->dev, "cannot register IRQ %u\n",
			priv->irq);
		error = -EIO;
		goto err_irq;
	}

	if (clk32k_bootfreq) {
		/* Compensate for boot time frequency */
		struct clk32k_change_freq change;
		change.old_freq = clk32k_bootfreq;
		change.new_freq = CLK32K_DESIRED_FREQUENCY;
		pdc_rtc_change_frequency(priv, &change);
	}

	/* Register a clock notifier */
	priv->clk_nb.notifier_call = pdc_rtc_clk_notify;
	clk32k_register_notify(&priv->clk_nb);

	/* Register our RTC with the RTC framework */
	device_init_wakeup(&pdev->dev, 1);
	priv->rtc_dev = rtc_device_register(pdev->name, &pdev->dev,
					    &pdc_rtc_ops,
					    THIS_MODULE);
	if (unlikely(IS_ERR(priv->rtc_dev))) {
		error = PTR_ERR(priv->rtc_dev);
		goto err_rtc;
	}

	pdc_rtc_setup(priv);

	return 0;

	rtc_device_unregister(priv->rtc_dev);
err_rtc:
	clk32k_unregister_notify(&priv->clk_nb);
err_irq:
err_regs:
err_dev:
err_pdata:
	return error;
}

static int pdc_rtc_remove(struct platform_device *pdev)
{
	struct pdc_rtc_priv *priv = platform_get_drvdata(pdev);

	clk32k_unregister_notify(&priv->clk_nb);
	rtc_device_unregister(priv->rtc_dev);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
/*
 * We use noirq callbacks because an ISR after the normal callbacks can clear
 * the secondly interrupt which would then get restored on resume and keep
 * firing.
 */
static int pdc_rtc_suspend_noirq(struct device *dev)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);
	unsigned int irq_en;

	priv->suspended = true;
	/* only wake if the alarm is enabled */
	if (device_may_wakeup(dev) && pdc_rtc_alarm_enabled(priv)) {
		/* disable interrupts other than the alarm */
		irq_en = priv->last_irq_en = pdc_rtc_read(priv, PDC_RTC_IRQ_EN);
		irq_en &= PDC_RTC_IRQ_ALARM;
		pdc_rtc_write(priv, PDC_RTC_IRQ_EN, irq_en);
		/* and enable wakeup on the interrupt */
		enable_irq_wake(priv->irq);
		priv->wakeup = true;
	}
	return 0;
}

static int pdc_rtc_resume_noirq(struct device *dev)
{
	struct pdc_rtc_priv *priv = dev_get_drvdata(dev);

	if (priv->wakeup) {
		/* disable wakeup */
		disable_irq_wake(priv->irq);
		/* and restore the previous interrupt enable bits */
		pdc_rtc_write(priv, PDC_RTC_IRQ_EN, priv->last_irq_en);
		priv->wakeup = false;
	}
	/* submit any postponed rtc interrupt */
	priv->suspended = false;
	if (priv->postponed_rtc_int) {
		dev_dbg(priv->dev,
			"submitting postponed rtc interrupt %x\n",
			priv->postponed_rtc_int);
		rtc_update_irq(priv->rtc_dev, 1, priv->postponed_rtc_int);
		priv->postponed_rtc_int = 0;
	}
	return 0;
}
#else
#define pdc_rtc_suspend NULL
#define pdc_rtc_resume NULL
#endif	/* CONFIG_PM_SLEEP */

static const struct dev_pm_ops pdc_rtc_pmops = {
#ifdef CONFIG_PM_SLEEP
	.suspend_noirq = pdc_rtc_suspend_noirq,
	.resume_noirq = pdc_rtc_resume_noirq,
	.freeze_noirq = pdc_rtc_suspend_noirq,
	.thaw_noirq = pdc_rtc_resume_noirq,
	.poweroff_noirq = pdc_rtc_suspend_noirq,
	.restore_noirq = pdc_rtc_resume_noirq,
#endif
};

static const struct of_device_id pdc_rtc_match[] = {
	{ .compatible = "img,pdc-rtc" },
	{}
};
MODULE_DEVICE_TABLE(of, pdc_rtc_match);

static struct platform_driver pdc_rtc_driver = {
	.driver = {
		.name = "pdc-rtc",
		.owner	= THIS_MODULE,
		.of_match_table	= pdc_rtc_match,
		.pm = &pdc_rtc_pmops,
	},
	.probe = pdc_rtc_probe,
	.remove = pdc_rtc_remove,
};

module_platform_driver(pdc_rtc_driver);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_DESCRIPTION("ImgTec PowerDown Controller RTC");
MODULE_LICENSE("GPL");
