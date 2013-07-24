#ifndef _CHORUS2_GPIO_H_
#define _CHORUS2_GPIO_H_

/* Chorus 2 has 128 GPIOs, in 8 banks of 16. */
#define NR_BUILTIN_GPIO 128

#define GPIO_A_BASE     0
#define GPIO_A_PIN(x)   (GPIO_A_BASE + (x))

#define GPIO_B_BASE     16
#define GPIO_B_PIN(x)   (GPIO_B_BASE + (x))

#define GPIO_C_BASE     32
#define GPIO_C_PIN(x)   (GPIO_C_BASE + (x))

#define GPIO_D_BASE     48
#define GPIO_D_PIN(x)   (GPIO_D_BASE + (x))

#define GPIO_E_BASE     64
#define GPIO_E_PIN(x)   (GPIO_E_BASE + (x))

#define GPIO_F_BASE     80
#define GPIO_F_PIN(x)   (GPIO_F_BASE + (x))

#define GPIO_G_BASE     96
#define GPIO_G_PIN(x)   (GPIO_G_BASE + (x))

#define GPIO_H_BASE     112
#define GPIO_H_PIN(x)   (GPIO_H_BASE + (x))

#define GPIO_EXP_BASE   NR_BUILTIN_GPIO
#define GPIO_EXP_PIN(x) (GPIO_EXP_BASE + (x))

/* Forward declaration of struct irq_data, defined in linux/irq.h */
struct irq_data;

int chorus2_gpio_disable(unsigned int gpio);
void chorus2_init_gpio(void);

#define GPIO_POLARITY_LOW          0x0
#define GPIO_POLARITY_HIGH         0x1

#define GPIO_LEVEL_TRIGGERED       0x0
#define GPIO_EDGE_TRIGGERED        0x1

#include <asm-generic/gpio.h>

#endif
