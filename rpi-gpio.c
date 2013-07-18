/**
 * gpio.c
 * Raspberry Pi button handling via GPIO
 *
 */

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <syslog.h>

#include "app.h"
#include "rpi-gpio.h"

#define RPI_PLATFORM_GPIO "/sys/bus/platform/devices/bcm2708_gpio"


static int rpi_gpio_configure(void);

int rpi_gpio_init(void) {
	int fd;
	char filename[1024];
	struct stat sb;

	/* Avoid doing GPIO stuff on non-Raspberry Pi hardware */
	if(stat(RPI_PLATFORM_GPIO, &sb) < 0 || !S_ISDIR(sb.st_mode)) {
		syslog(LOG_INFO, "Raspberry Pi GPIO: Not on R-Pi hardware");
		return -1;
	}

	/* this will likely fail for non-root users but we'll try anyway */
	rpi_gpio_configure();

	snprintf(filename, sizeof(filename), "%s/gpio%s/value",
		SYS_CLASS_GPIO_PATH, RPI_GPIO_BUTTON_PIN);

	fd = open(filename, O_RDONLY);
	if(fd < 0)
		syslog(LOG_INFO, "Raspberry Pi GPIO: Unable to open file '%s'", filename);

	return fd;
}

static int rpi_gpio_configure(void) {
	char filename[1024];
	FILE *fd;

	/* Attempt to export pin */
	snprintf(filename, sizeof(filename), "%s/export", SYS_CLASS_GPIO_PATH);
	fd = fopen(filename, "w");
	if(fd == NULL)
		return -1;

	fwrite(RPI_GPIO_BUTTON_PIN "\n", strlen(RPI_GPIO_BUTTON_PIN) + 1, 1, fd);
	fclose(fd);

	/* Enable pin for poll() */
	snprintf(filename, sizeof(filename), "%s/gpio%s/edge", SYS_CLASS_GPIO_PATH, RPI_GPIO_BUTTON_PIN);
	fd = fopen(filename, "w");
	if(fd == NULL)
		return -1;

	fwrite("falling\n", 8, 1, fd);
	fclose(fd);

	return 0;
}

void rpi_gpio_release(int fd) {

	close(fd);
}
