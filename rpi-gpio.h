/**
 * rpi-gpio.h
 *
 */
#ifndef RPI_GPIO_H
#define RPI_GPIO_H

#define SYS_CLASS_GPIO_PATH "/sys/class/gpio"
#define RPI_GPIO_BUTTON_PIN "17"


int rpi_gpio_init(void);
void rpi_gpio_release(int fd);

#endif
