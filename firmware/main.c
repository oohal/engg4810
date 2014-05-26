#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>

#include <driverlib/sysctl.h>
#include <driverlib/systick.h>
#include <driverlib/gpio.h>
#include <driverlib/adc.h>
#include <driverlib/i2c.h>
#include <driverlib/ssi.h>
#include <driverlib/udma.h>
#include <driverlib/uart.h>
#include <driverlib/pin_map.h>
#include <driverlib/timer.h>

#include "firmware.h"

#include "fatfs/ff.h"

tDMAControlTable dma_channel_list[64];
#pragma DATA_ALIGN(dma_channel_list, 1024)

/* 1Hz system tick interrupt */
// FIXME: this isn't actually 1Hz right now

volatile int ticked = 1;

void SysTicker(void)
{
	ticked++;
	return;
}

// The magnitude of the acceleration vector is sqrt(x^2 + y^2 + z^2), but
// we don't care about the actual magnitude, just relative size. As a result
// doing the sqrt is unnecessary

static int get_sqmag(uint16_t *samples)
{
	// at 0g the outputs 2.5V when in free fall - FIXME: CHECK THIS, PRETTY SURE IT'S WRONG
	//const int FF_OFFSET = 2844;
	const int FF_OFFSET = 0;

	int i, mag = 0;

	for(i = 0; i <= 2; i++) {
		int corrected = samples[i] - FF_OFFSET;
		mag += corrected * corrected;
	}

	return mag;
}

uint16_t *accel_analyze(uint16_t *samples)
{
	uint16_t *largest = samples;
	int mag = 0, max_mag = 0;
	int i;

	for(i = 0; i < SAMPLE_RATE; i++) {
		mag = get_sqmag(samples);

		if(mag > max_mag) {
			max_mag = mag;
			largest = samples;
		}

		samples += 4;
	}

	return largest;
}

#pragma FUNC_NEVER_RETURNS(main);
int main(void)
{
	uint8_t pins = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); // silicon bug workaround, the adc just flat out won't work off MOSC if it's used as the system clock
	ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 1);

	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_OSC | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	// configure systick to give an tick interrupt once per second
	// NB: maximum systick period is 16,777,216 so don't set it too high.
	SysTickPeriodSet(SysCtlClockGet());
	SysTickIntEnable();
	SysTickEnable();

	// configure LED GPIOs for mad rad blinkenlites
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, pins);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

	// setup DMA, for specific details of how it's used check the relevant source file for that peripherial.
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
	uDMAControlBaseSet(dma_channel_list);
	uDMAEnable();

	// initalise UART0 which runs the debug interface. This gets NOPed out for release builds
	debug_init();


	adc_init();
	//gps_init(); // init uart2 and all the GPS bullshit

	GPIOPinWrite(GPIO_PORTF_BASE, pins, 0x00); // turn off LEDs

	debug_printf("-- started --\r\n");

	/*
	 * Attempt to mount the SD card and read/write to files
	 */

#if 0
	FATFS fs;
	FRESULT res;

	res = f_mount(&fs, "/", 1);

	if(res == FR_OK) {
		DIR dir;

		res = f_opendir(&dir, "/");

		if(res == FR_OK) {
			FILINFO info;

			do {
				res = f_readdir(&dir, &info);
				if(res != FR_OK) {
					debug_printf("err reading dir\r\n");
					break;
				}

				if(!info.fname[0]) break;

				debug_printf(" %s\r\n", info.fname);
			} while(1);

			f_closedir(&dir);
		} else {
			debug_printf("failed to open dir\r\n");
		}

		FIL fp;

		f_open(&fp, "/asdf.txt", FA_CREATE_ALWAYS | FA_WRITE);
			char msg[] = "PLEASEWORKPLZ\n";
			uint32_t written;
			f_write(&fp, msg, sizeof(msg) - 1, &written);
			f_sync(&fp);
		f_close(&fp);
	}
#endif


	memset(sample_buffer, 0, sizeof(sample_buffer));

	adc_start();

	while(1) {
		if(udma_done) {
			uint16_t *buf = accel_analyze(sample_buffer);

			adc_start();

			debug_printf("%d - peak: (%d, %d, %d) temp: %d \r\n", ticked,  (uint32_t) buf[0], (uint32_t) buf[1], (uint32_t)buf[2], (uint32_t)buf[3]);

			//gps_update();

			ticked = 0;
		}
	}
}
