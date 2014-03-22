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
	uint8_t output = pins;

	// gotta go slow, 4MHz ought to be enough for anyone
	SysCtlClockSet(SYSCTL_OSC_MAIN | SYSCTL_XTAL_16MHZ);

	// configure systick to give an tick interrupt once per second
	// NB: maximum systick period is 16,777,216 so don't set it too high.
	SysTickPeriodSet(SysCtlClockGet());
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

	ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 0);
	ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_END | ADC_CTL_CH11 | ADC_CTL_IE); // conversion from A11, trigger interrupt on completion
	ADCSequenceEnable(ADC0_BASE, 3);

	while(1) {
		if(ticked) {
			ticked = 0;
			output ^= pins;
			GPIOPinWrite(GPIO_PORTF_BASE, pins, output);
			ADCProcessorTrigger(ADC0_BASE, 3);
		}
	};

	return 0;
}
