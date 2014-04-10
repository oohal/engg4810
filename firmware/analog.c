/*
 * analog.c
 *
 *  Created on: 04/04/2014
 *      Author: Oliver
 */

#include <stdint.h>
#include <stdbool.h>

#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_ints.h>
#include <inc/hw_gpio.h>

#include <driverlib/sysctl.h>
#include <driverlib/gpio.h>
#include <driverlib/adc.h>
#include <driverlib/udma.h>
#include <driverlib/pin_map.h>

#include <inc/hw_adc.h>

#include "firmware.h"

#define SAMPLES 1000

uint8_t active_buffer = 0;
volatile int udma_done = 0;
uint16_t sample_buffer[16];


/*
 * FIXME: I think this is only ever called for DMA errors, actual transfer completion notifications go on the relevant peripherial
 *        interrupt vector. So I should probably rejigger this sometime.
 */
void dma_int_handler(void)
{
	uDMAIntClear(UDMA_CHANNEL_ADC0);
	udma_done = 1;
}

void adc_int_handler(void)
{
	ADCIntClear(ADC0_BASE, 0);
	udma_done = 1;
}

void adc_init()
{
	// AIN1 - Accelerometer x/y/z axis
	// AIN2 - Accelerometer x/y/z axis
	// AIN4 - Accelerometer x/y/z axis

	// AIN5 - Fun's external temp sensor
	// ADC_CTL_TS - internal temp sensor, it's really inaccurate though (specced at +/- 5 degrees)

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);

	// configure analog inputs 1, 2 and 4
	GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3);
	GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_1 | GPIO_PIN_2);

	// It might be worth dedicating an ADC each to converting from the temp sensor and the accelerometer outputs

	uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC0, UDMA_ATTR_ALL);
	uDMAChannelAssign(UDMA_CH14_ADC0_0);

	uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST);
	uDMAChannelControlSet(UDMA_CHANNEL_ADC0,
		UDMA_PRI_SELECT |
		UDMA_SIZE_16 |
		UDMA_DST_INC_16 |
		UDMA_SRC_INC_NONE |
		UDMA_ARB_4
	);

	uDMAIntClear(UDMA_CHANNEL_ADC0);
	uDMAIntRegister(UDMA_CHANNEL_ADC0, dma_int_handler);

	// TODO: work out what's up with the clocking of the ADC, tivaware docs are kind of confusing
	//ADCClockConfigSet(ADC0_BASE, ADC_CLOCK_SRC_PIOSC, 0);

	ADCIntRegister(ADC0_BASE, 0, adc_int_handler);
	ADCIntEnable(ADC0_BASE, 0);

	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH1);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH2);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH4);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_TS | ADC_CTL_END | ADC_CTL_IE);

	ADCSequenceDMAEnable(ADC0_BASE, 0);

	ADCSequenceEnable(ADC0_BASE, 0);

	adc_reinit(); // configure and enable the next DMA transfer

/*
	// temp sensor output
	ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
	ADCSequenceStepConfigure(ADC0_BASE, 3, 0, ADC_CTL_END | ADC_CTL_TS); // conversion from A11, trigger interrupt on completion
	ADCSequenceEnable(ADC0_BASE, 3);
*/
}

void adc_reinit(void)
{
	udma_done = 0;

	//ADCSequenceUnderflowClear(ADC0_BASE, 0);

	// read the DMA dest pointer to find how much of the buffer was used
	uDMAChannelTransferSet(UDMA_CHANNEL_ADC0,
		UDMA_PRI_SELECT | UDMA_MODE_BASIC,
		(void *) (ADC0_BASE + ADC_O_SSFIFO0),
		sample_buffer,
		sizeof(sample_buffer) / sizeof(uint16_t)
	);

	uDMAChannelEnable(UDMA_CHANNEL_ADC0);
}
