#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

volatile int ticked = 1;

extern void disk_timerproc(void);

// The magnitude of the acceleration vector is sqrt(x^2 + y^2 + z^2), but
// we don't care about the actual magnitude, just relative size. As a result
// doing the sqrt is unnecessary

static int get_sqmag(uint16_t *samples)
{
	// at 0g the outputs 1.65 when in free fall - FIXME: CHECK THIS, PRETTY SURE IT'S WRONG
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

int firstrun = 1;

/*
 * fatfs file system structures, these are global since they're actually quite large so having them on the
 * stack wrecks everything.
 */

#define NO_LOGFILE -1

FATFS fs;
FIL bin_log, txt_log;

char log_file_name[14];

uint32_t bin_log_open = 0, txt_log_open = 0;
int log_file_index = NO_LOGFILE;

/*
 * Attempts to open the specified logfiles, if log_index is -1 it will search the SD card root for existing files
 * and create a new log.
 *
 * Returns: The index of the log or NO_LOGFILE on error
 */

int open_logfiles(int log_index)
{
	FRESULT res = f_mount(&fs, "/", 1);

	if(res != FR_OK)
		return NO_LOGFILE;

	if(log_index == NO_LOGFILE) { /* no set log file, create a new one */
		DIR dir;

		res = f_opendir(&dir, "/");
		uint32_t logs = 0, max = 0;

		if(res == FR_OK) {
			FILINFO info;

			do {
				res = f_readdir(&dir, &info);
				if(res != FR_OK) {
					return NO_LOGFILE;
				}

				if(!info.fname[0]) {
					break; // end of dir marker
				}

				logs = atoi(info.fname);
				if(logs > max) {
					max = logs;
				}
			} while(1);
			f_closedir(&dir);

			debug_printf("max: %d\r\n", max);
		} else {
			debug_printf("failed to open dir\r\n");
			return -1;
		}

		log_index = max + 1;
	}

	snprintf(log_file_name, sizeof(log_file_name), "/%d.bin", log_index);
	res = f_open(&bin_log, log_file_name, FA_OPEN_ALWAYS | FA_WRITE);
	if(res == FR_OK) {
		if(bin_log.fsize) {
			f_lseek(&bin_log, bin_log.fsize);
		}

		bin_log_open = 1;
	} else {
		debug_printf("failed to open binlog: %s\r\n", log_file_name);
		return NO_LOGFILE;
	}

	return log_index;
}

#pragma FUNC_NEVER_RETURNS(main);
int main(void)
{
	// Datasheet says that if you want to change ADC clocks it needs to be done with the PLL enabled,
	// makes no sense to me but ok
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_PLL | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_ADC0);
	ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 1);

	// disable PLL
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_USE_OSC | SYSCTL_SYSDIV_1 | SYSCTL_XTAL_16MHZ);

	// setup systick to provide 10ms ticks for the SD card stuff
	SysTickIntRegister(disk_timerproc);
	SysTickPeriodSet(SysCtlClockGet() / 100);
	SysTickIntEnable();

	FPUEnable();
	FPUStackingDisable(); 	// no float in interrupts, not now, not ever

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOA);

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UDMA);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_UDMA);
	uDMAControlBaseSet(dma_channel_list);
	uDMAEnable();

	// configure LED GPIOs for mad rad blinkenlites
	uint8_t pins = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, pins);
	GPIOPinWrite(GPIO_PORTF_BASE, pins, 0x00); // turn off LEDs

	debug_printf("-- started --\r\n");

	/*
	 * Attempt to mount the SD card and read/write to files
	 */

	SysTickEnable();

	log_file_index = open_logfiles(NO_LOGFILE);

	if(log_file_index == NO_LOGFILE) { // turn on the blue LED if we can't open the logfile
		led_set(LED_BLUE, 1);
	}

	SysTickDisable();

	memset(sample_buffer, 0, sizeof(sample_buffer));

	adc_init();
	gps_init();

	adc_start();

	/* sensor data */
	uint16_t temp_sample = 0, accel_sample[3];
	uint32_t time = 0, date = 0;
	uint32_t fix_status = 0;
	float lat = 0.0f, lng = 0.0f;

	int peak_accel = 0;

	uint32_t sample_count = 0, gps_count = 0, adc_count = 0, gps_sleep = 0;

	memset(accel_sample, 0, sizeof(accel_sample));

	while(1) {
		extern int dma_err;

		if(dma_err) {
			dma_err = 0;
			debug_printf("dma_error\r\n");
		}

		/* process GPS updates */
		if(gps_msg_ready) {
			gps_sleep = gps_update(&lat, &lng, &time, &date, &fix_status);

			gps_count = 1;
			gps_msg_ready = 0;
		}

		/* process ADC samples */
		if(adc_done) {
			uint16_t *buf;

			int max = accel_analyze(sample_buffer, &buf);

			if(max > peak_accel) {
				peak_accel = max;

				accel_sample[0] = buf[0];
				accel_sample[1] = buf[1];
				accel_sample[2] = buf[2];
				temp_sample     = buf[3];
			}

			adc_start();

			adc_count++;
			adc_done = 0;
		}

		// end of sampling period, clear everything out and get new values
		if(adc_count >= 10 && (gps_count || gps_sleep)) {
			FRESULT res;

			/* console log*/

			debug_printf("%u time: %u-%u ", fix_status, date, time);
			debug_printf("accel: (%d,%d,%d) ", (int) accel_sample[0], (int) accel_sample[1], (int) accel_sample[2]);
			debug_printf("temp: %d ", (int) temp_sample);
			debug_printf("loc: (%f,%f)\r\n", lat, lng);

			SysTickEnable(); // enable systick so the sd card driver can detect timeouts

			/* binary log */

			/* record format:
			 * items | bytes per item | desc
			 * ------|----------------|----------
			 *    1  |    4           | 32bit: bit 31 = fix indicator : 30:0 sample count
			 *    2  |    4           | time/date
			 *    4  |    2           | int16 x, y, z
			 *    4  |    2           | float lat, long
			 *    1  |    2           | temp
			 */

			res = FR_OK;

			int file = open_logfiles(log_file_index);

			if(file != NO_LOGFILE) { /* write to binary log */
				uint32_t tmp = sample_count | (fix_status << 31);
				UINT dontcare;

				log_file_index = file;

				res |= f_write(&bin_log, &tmp,         sizeof(tmp),          &dontcare);
				res |= f_write(&bin_log, &time,        sizeof(time),         &dontcare);
				res |= f_write(&bin_log, &date,        sizeof(date),         &dontcare);
				res |= f_write(&bin_log, accel_sample, sizeof(accel_sample), &dontcare);
				res |= f_write(&bin_log, &lat,         sizeof(lat),          &dontcare);
				res |= f_write(&bin_log, &lng,         sizeof(lng),          &dontcare);
				res |= f_write(&bin_log, &temp_sample, sizeof(temp_sample),  &dontcare);

				res |= f_sync(&bin_log);

				if(res == FR_OK) {
					led_set(LED_BLUE, 0);
				} else {
					led_set(LED_BLUE, 1); // SD card error indicator
				}
			} else {
				led_set(LED_BLUE, 1);
			}

			SysTickDisable();

			peak_accel = 0;
			gps_count = 0;
			adc_count = 0;

			sample_count++;
		}

		firstrun = 0;
		SysCtlSleep();
	}
}
