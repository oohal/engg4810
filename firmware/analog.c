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
#include <driverlib/timer.h>

#include <inc/hw_adc.h>

#include "firmware.h"

volatile int udma_done = 0;
uint16_t sample_buffer[4 * SAMPLE_RATE]; // 4 values per sample instant

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
	uint32_t dmaint = uDMAIntStatus();

	ADCIntClear(ADC0_BASE, 0);

	if(dmaint & (1 << UDMA_CHANNEL_ADC0)) {
		ADCIntClear(ADC0_BASE, 0);
		uDMAIntClear(UDMA_CHANNEL_ADC0);
		TimerDisable(TIMER0_BASE, TIMER_A);
		udma_done = 1;
	}
}

void timer_int_handler(void)
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
}

void adc_init()
{
	/*
	 * setup TIMER0A to provide a periodic ADC tigger, this timer isn't started until adc_start() is called
	 */

	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);

	TimerIntDisable(TIMER0_BASE, 0xFFFFFFFF); // disable all interrupts
	TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);

	TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet() / SAMPLE_RATE);

	//TimerADCEventSet(TIMER0_BASE, TIMER_ADC_TIMEOUT_A); 	// enable ADC triggering from this timer
	TimerControlTrigger(TIMER0_BASE, TIMER_A, true);

	TimerIntRegister(TIMER0_BASE, TIMER_A, timer_int_handler);
	//TimerIntEnable(TIMER0_BASE, TIMER_A);

	/*
	 * configure the ADC and DMA operation
	 */

	// AIN1 - Accelerometer x/y/z axis
	// AIN2 - Accelerometer x/y/z axis
	// AIN4 - Accelerometer x/y/z axis
	// AIN5 - Fun's external temp sensor

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);

	//SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);

	GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3);
	GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_1 | GPIO_PIN_2);

	uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC0, UDMA_ATTR_ALL);
	uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);
	uDMAChannelAssign(UDMA_CH14_ADC0_0);

	//uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST);
	uDMAChannelControlSet(UDMA_CHANNEL_ADC0,
		UDMA_PRI_SELECT |
		UDMA_SIZE_16 |
		UDMA_DST_INC_16 |
		UDMA_SRC_INC_NONE |
		UDMA_ARB_4
	);

	ADCIntRegister(ADC0_BASE, 0, adc_int_handler);
	ADCIntDisableEx(ADC0_BASE, ADC_INT_SS0 | ADC_INT_SS1 | ADC_INT_SS2 | ADC_INT_SS3);
	ADCIntClearEx(ADC0_BASE, ADC_INT_SS0 | ADC_INT_SS1 | ADC_INT_SS2 | ADC_INT_SS3);

	ADCIntEnable(ADC0_BASE, 0);

	ADCSequenceDisable(ADC0_BASE, 0);
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_TIMER, 0);

	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH1);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH2);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH4);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_TS | ADC_CTL_END | ADC_CTL_IE);

	ADCSequenceUnderflowClear(ADC0_BASE, 0);
	ADCSequenceOverflowClear(ADC0_BASE, 0);
	ADCIntClear(ADC0_BASE, 0);

	ADCSequenceDMAEnable(ADC0_BASE, 0);
	ADCSequenceEnable(ADC0_BASE, 0);
}

/*
 * ADC#08 ADC Sample Sequencer Only Samples When Using Certain Clock Configurations
 * Revision(s) Affected: 6 and 7.
 *
 * Description: The ADC sample sequencer does not sample if using either the MOSC or the PIOSC as
 * both the system clock source and the ADC clock source.
 * Workaround(s): There are three possible workarounds:
 *
 * • Enable the PLL and use it as the system clock source.
 * • Configure the MOSC as the system clock source and the PIOSC as the ADC clock source.
 * • Enable the PLL, configure the PIOSC as the ADC clock source and as the system
 *   clock source, then subsequently disable the PLL using HWREG(0x400fe060) !=
 *   0x00000200.
 *
 *
 *
 */


void adc_start(void)
{
	udma_done = 0;

	// prepare new transfer
	uDMAChannelTransferSet(UDMA_CHANNEL_ADC0 | UDMA_PRI_SELECT,
		UDMA_MODE_BASIC,
		(void *) (ADC0_BASE + ADC_O_SSFIFO0),
		sample_buffer,
		SAMPLE_RATE * 4
	);

	/* flush out ADC FIFO */
	uint32_t buffer[8];
	ADCSequenceDataGet(ADC0_BASE, 0, buffer);
	ADCSequenceUnderflowClear(ADC0_BASE, 0);
	ADCSequenceOverflowClear(ADC0_BASE, 0);

	uDMAChannelEnable(UDMA_CHANNEL_ADC0);

	TimerEnable(TIMER0_BASE, TIMER_A);
}
