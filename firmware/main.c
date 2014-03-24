#define PART_TM4C123GH6PM

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

/* 1Hz system tick interrupt */
// FIXME: this isn't actually 1Hz right now

volatile int ticked = 1;

void SysTicker(void)
{
	ticked = 1;
	return;
}

/* */

int main(void)
{
	uint8_t pins =  GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3;

	// gotta go slow, 4MHz ought to be enough for anyone
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

	// configure systick to give an tick interrupt once per second
	// NB: maximum systick period is 16,777,216 so don't set it too high.
	SysTickPeriodSet(SysCtlClockGet()/1000);
	SysTickIntEnable();
	SysTickEnable();

	// configure LED GPIOs for mad rad blinkenlites
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
	GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, pins);

	// SPI0 and I2C0 both use the port A pins, so enable that first
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);

	/* configure SPI0 */
	SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_I2C1);

	GPIOPinConfigure(GPIO_PA2_SSI0CLK);
	GPIOPinConfigure(GPIO_PA3_SSI0FSS);
	GPIOPinConfigure(GPIO_PA4_SSI0RX);
	GPIOPinConfigure(GPIO_PA5_SSI0TX);
	GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_5 | GPIO_PIN_4 | GPIO_PIN_3 | GPIO_PIN_2);

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
	ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_END | ADC_CTL_CH11); // conversion from A11, trigger interrupt on completion
	ADCSequenceEnable(ADC0_BASE, 3);


	// Let get UARTed

	// UART0 is the debug port interface
	// TODO: this stuff needs to be debug build only

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0); // 115200
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);

	UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFOEnable(UART0_BASE);
	UARTEnable(UART0_BASE);

	// UART1 is the GPS serial interface

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
	GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PB0_U1RX);
	GPIOPinConfigure(GPIO_PB1_U1TX);

	UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFOEnable(UART1_BASE);
	UARTEnable(UART1_BASE);

	uint32_t i = 0, sample = 0;
	char str[] = "test\r\n";

	while(1) {
		const uint32_t max = 0x3FF;

		if(ticked) {
			int j;

			for(j = 0; j < sizeof(str); j++) {
				UARTCharPutNonBlocking(UART0_BASE, str[j]);
			}

			ticked = 0;
			sample = 0;
			ADCProcessorTrigger(ADC0_BASE, 3);
			while(ADCBusy(ADC0_BASE)) {};
			ADCSequenceDataGet(ADC0_BASE, 3, &sample);
			ADCIntClear(ADC0_BASE, 3);
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
