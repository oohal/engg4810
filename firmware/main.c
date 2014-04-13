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

tDMAControlTable dma_channel_list[64];
#pragma DATA_ALIGN(dma_channel_list, 1024)

/* 1Hz system tick interrupt */
// FIXME: this isn't actually 1Hz right now

volatile int ticked = 1;

void SysTicker(void)
{
	ticked = 1;
	return;
}

int main(void)
{
	uint8_t pins =  GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;

	// gotta go slow, 4MHz ought to be enough for anyone
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

	/* configure I2C1 pins for accelerometer and whatever else */
/*
	SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C1);
	GPIOPinConfigure(GPIO_PA6_I2C1SCL);
	GPIOPinConfigure(GPIO_PA7_I2C1SDA);
	GPIOPinTypeI2CSCL(GPIO_PORTA_BASE, GPIO_PIN_6);
	GPIOPinTypeI2C(GPIO_PORTA_BASE, GPIO_PIN_7);
*/

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

	sd_init();
	adc_init();

	uint32_t i = 0, sample = 0;
	debug_printf("asdfasdfsdfdsa\r\n");

	for(i = 0; i < 7; i++) {
		sample_buffer[i] = 0;
	}

	int conversion = 0;

	while(1) {
		const uint32_t max = 0x3FF;

		if(ticked) {
			ticked = 0;

			if(udma_done) {
				debug_printf("%d - (%d, %d, %d)  - %d | (%d, %d, %d) - %d\r\n", conversion,
					(int) sample_buffer[0], (int) sample_buffer[1], (int) sample_buffer[2], (int) sample_buffer[3],
					(int) sample_buffer[4], (int) sample_buffer[5], (int) sample_buffer[6], (int) sample_buffer[7]
				);

				adc_reinit();

				conversion = 0;
	  			udma_done  = 0;

				sample++;
			}

			ADCProcessorTrigger(ADC0_BASE, 0);
			conversion++;
		}


		/*
		 float temp = 147.5f - ((75.0f * 3.3f) * sample / 4096);
		 debug_printf("internal temp: %f (%d)\r\n", temp, sample);
		 */
	}

	return 0;
}
