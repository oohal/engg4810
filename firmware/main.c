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

#include <string.h>

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

	// SPI0 and I2C0 both use the port A pins, so enable that first
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C1);

	/* configure I2C1 pins for accelerometer and whatever else */
	GPIOPinConfigure(GPIO_PA6_I2C1SCL);
	GPIOPinConfigure(GPIO_PA7_I2C1SDA);
	GPIOPinTypeI2CSCL(GPIO_PORTA_BASE, GPIO_PIN_6);
	GPIOPinTypeI2C(GPIO_PORTA_BASE, GPIO_PIN_7);

	// ADC0_BASE

	// Initialise ADC
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
	GPIOPinTypeADC(GPIO_PORTB_BASE, GPIO_PIN_5);

	// TODO: work out what's up with the clocking of the ADC, tivaware docs says only the TM4x129 devices can use the PLL as a clock source
	//
	//ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 0);


	// Accelerometer Outputs
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ACD0_BASE, 0, 0, ADC_CTL_CH1);
	ADCSequenceStepConfigure(ACD0_BASE, 0, 1, ADC_CTL_CH2);
	ADCSequenceStepConfigure(ACD0_BASE, 0, 2, ADC_CTL_CH4 | ADC_CTL_END);
	ADCSequenceEnable(ACD0_BASE, 0);


	// Temp sensor output
	ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_END | ADC_CTL_TS); // conversion from A11, trigger interrupt on completion
	ADCSequenceEnable(ADC0_BASE, 3);

	// initalise UART0 which runs the debug interface. This gets NOPed out for release builds
	debug_init();

	// UART1 is the GPS serial interface
	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
	GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PB0_U1RX);
	GPIOPinConfigure(GPIO_PB1_U1TX);

	UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFOEnable(UART1_BASE);
	UARTEnable(UART1_BASE);

	//sd_init();

	uint32_t i = 0, sample = 0;

	debug_printf("asdfasdfsdfdsa\r\n");

	while(1) {
		const uint32_t max = 0x3FF;

		if(ticked) {
			ticked = 0;
			sample = 0;
			ADCProcessorTrigger(ADC0_BASE, 3);
			while(ADCBusy(ADC0_BASE)) {};
			ADCSequenceDataGet(ADC0_BASE, 3, &sample);
			ADCIntClear(ADC0_BASE, 3);

			float temp = 147.5f - ((75.0f * 3.3f) * sample / 4096);

			debug_printf("internal temp: %f (%d)\r\n", temp, sample);
		}

		if(i > max) {
			GPIOPinWrite(GPIO_PORTF_BASE, pins, pins);
			i = 0;
		} else if(i > sample) {
			GPIOPinWrite(GPIO_PORTF_BASE, pins, 0x00);
		}

		i++;
	}

	return 0;
}
