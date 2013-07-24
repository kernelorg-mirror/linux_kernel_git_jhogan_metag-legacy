#ifndef _CHORUS2_CLOCK_H_
#define _CHORUS2_CLOCK_H_

struct meta_clock_desc;

unsigned long chorus2_get_coreclock(void);
/* passed through machine descriptor (init only) */
extern struct meta_clock_desc chorus2_meta_clocks;

unsigned long get_sysclock(void);

void pix_clk_set_limits(unsigned long min, unsigned long max);

#endif /* _CHORUS2_CLOCK_H_ */
