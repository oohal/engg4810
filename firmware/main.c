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
#include <driverlib/fpu.h>

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

int accel_analyze(uint16_t *samples, uint16_t **largest)
{
	int mag = 0, max_mag = 0;
	int i;

	for(i = 0; i < SAMPLE_RATE; i++) {
		mag = get_sqmag(samples);

		if(mag > max_mag) {
			max_mag = mag;
			*largest = samples;
		}

		samples += 4;
	}

	return max_mag;
}

#pragma FUNC_NEVER_RETURNS(main);
int main(void)
{
	uint8_t pins = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 1);

	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_OSC | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	// configure systick to give an tick interrupt once per second
	// NB: maximum systick period is 16,777,216 so don't set it too high.
	SysTickPeriodSet(SysCtlClockGet() / 100000);
	SysTickIntEnable();
	SysTickEnable();

	FPUEnable();
	FPUStackingDisable();

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
	gps_init(); //

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

	uint16_t temp_sample, accel_sample[3];
	uint32_t time, date;
	float lat, lng;
	int peak_accel = 0;

	uint32_t gps_timer = 0, adc_timer = 0;

	while(1) {
		extern int dma_err;

		if(dma_err) {
			dma_err = 0;
			debug_printf("dma_error\r\n");
		}

		if(gps_msg_ready) {
			uint32_t start = ticked;

			gps_update(&lat, &lng, &time, &date);

			gps_timer = ticked - start;
			if(!gps_timer) gps_timer = 1;
		}

		if(udma_done) {
			uint32_t start = ticked;
			uint16_t *buf;

			int max = accel_analyze(sample_buffer, &buf);

			if(max > peak_accel) {
				peak_accel = max;
				accel_sample[0] = buf[0];
				accel_sample[1] = buf[1];
				accel_sample[2] = buf[2];
				//memcpy(accel_sample, buf, sizeof(accel_sample));
				temp_sample = buf[4];
			}

			adc_start();

			adc_timer = ticked - start;
			if(!adc_timer) adc_timer = 1;
		}

		if(adc_timer && gps_timer) { // do 1Hz updates
			debug_printf("timers: adc %d gps %d - ", adc_timer, gps_timer);

			// end of sampling period, clear everything out and get new values
			peak_accel = 0;

			debug_printf("time: %u-%u ", date, time);
			debug_printf("accel: (%d,%d,%d) ", (int) accel_sample[0], (int) accel_sample[1], (int) accel_sample[2]);
			debug_printf("temp: %d ", (int) temp_sample);
			debug_printf("loc: (%f,%f)\r\n", lat, lng);

			ticked = 0;
			adc_timer = 0;
			gps_timer = 0;
		}
	}
}
