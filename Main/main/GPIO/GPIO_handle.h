#ifndef GPIO_HANDLE_H
#define GPIO_HANDLE_H

#define LED_ON 0
#define LED_OFF 1

void irq_func_btn_init(void);
void irq_int_init(void);
void gpio_init(void);

#endif // GPIO_HANDLE_H