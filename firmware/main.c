#include <stdint.h>
#include <stdbool.h>

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
// we don't care about the actual magnitude, just wether it's bigger or
// smaller than
// calculating this for comparison we can skip
// the sqrt.

float get_sqmag(uint16_t *samples)
{
	// at 0g the outputs 2.5V when in free fall,
	const int FF_OFFSET = 2844;
	int i, mag = 0;

	for(i = 0; i < 3; i++) {
		int corrected = samples[i] - FF_OFFSET;
		mag += corrected * corrected;
	}

	return mag;
}

// returns the index
int accel_analyze(uint16_t *samples, int sample_count)
{
	int i, mag = 0, max_mag, max_index = 0;

	for(i = 0; i < sample_count; i++) {
		mag = get_sqmag(samples);

		if(mag > max_mag) {
			max_index = i;
		}

		samples += 4;
	}

	return max_index;
}

#define SAMPLES (sizeof(sample_buffer) / sizeof(*sample_buffer))

#pragma FUNC_NEVER_RETURNS(main);
int main(void)
{
	uint8_t pins = GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;

	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

	// configure systick to give an tick interrupt once per second
	// NB: maximum systick period is 16,777,216 so don't set it too high.
	SysTickPeriodSet(SysCtlClockGet()/2);
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

	// UART1 is the GPS serial interface
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PB0_U1RX);
	GPIOPinConfigure(GPIO_PB1_U1TX);

	UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFOEnable(UART1_BASE);
	UARTEnable(UART1_BASE);

	GPIOPinWrite(GPIO_PORTF_BASE, pins, 0x00); // turn off LEDs

	adc_init();

	debug_printf("-- started --\r\n");

	/*
	 * Attempt to mount the SD card and read/write to files
	 */

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

	while(1) {
		if(ticked > 2) {
			ticked = 0;

			if(udma_done) {
	  			udma_done = 0;
				adc_reinit();

				int index = accel_analyze(sample_buffer, 4);
				uint16_t *buf = sample_buffer + index * 4;

				debug_printf("peak: (%d, %d, %d)\r\n", buf[0], buf[1], buf[2], buf[3]);
			}

			ADCProcessorTrigger(ADC0_BASE, 0);
		}
	}
}
