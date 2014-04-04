#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>

#include <driverlib/sysctl.h>
#include <driverlib/systick.h>
#include <driverlib/gpio.h>
#include <driverlib/i2c.h>
#include <driverlib/ssi.h>
#include <driverlib/udma.h>
#include <driverlib/pin_map.h>

//{GPIGPIO_PORTF_BASE, GPIO_PIN_1, GPIO_OUTPUT}, /* EK_TM4C123GXL_LED_RED */
//{GPIO_PORTF_BASE, GPIO_PIN_2, GPIO_OUTPUT},    /* EK_TM4C123GXL_LED_BLUE */
//{GPIO_PORTF_BASE, GPIO_PIN_3, GPIO_OUTPUT},    /* EK_TM4C123GXL_LED_GREEN */

/*************** Default interrupt handlers ********************/

enum leds {
	LED_RED = GPIO_PIN_1,
	LED_GREEN = GPIO_PIN_3,
	LED_BLUE = GPIO_PIN_2
};

static flash_leds(uint8_t colour, int flash)
{
	int i = 0;
	uint8_t c;

	if(!flash) {
		GPIOPinWrite(GPIO_PORTF_BASE, colour, colour);
		while(1) {};
	}

	while(1) {
		if(i == 200000) {
			i = 0;
			c ^= colour;

			GPIOPinWrite(GPIO_PORTF_BASE, colour, c);
		}

		i++;
	}
}

// Fault glows red and makes you dead
void FaultISR(void)
{
	flash_leds(LED_RED, 0);
}

// NMI? OHGODWHY - green
void NmiISR(void)
{
	flash_leds(LED_GREEN, 0);
}

void IntDefaultHandler(void)
{
	flash_leds(LED_RED, 300000);
}

void die_horribly(void) {
	flash_leds(LED_BLUE, 0);

}

