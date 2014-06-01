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

uint16_t sample_buffer[4 * SAMPLE_RATE]; // 4 values per sample instant
volatile int adc_done = 0;

int dma_err = 0;

void dma_int_handler(void)
{
	FaultISR();
}

void adc_int_handler(void)
{
	uint32_t dmaint = uDMAIntStatus();

	ADCIntClear(ADC0_BASE, 0);

	if(dmaint & (1 << UDMA_CHANNEL_ADC0)) {
		uDMAIntClear(UDMA_CHANNEL_ADC0);

		TimerDisable(TIMER0_BASE, TIMER_A);
		adc_done = 1;
	} else {
		FaultISR();
	}
}

void timer_int_handler(void)
{
	TimerIntClear(TIMER0_BASE, TIMER_TIMA_TIMEOUT);
}


/*
 * Peripherial scatter gather DMA structure, we need this to support high-ish sampling rates since the DMA
 * controller can only transfer 1024 items per transfer. To support more items the DMA controller is used in
 * scatter gather mode and blocks of 1024 items are chained together.
 */

#define DMA_TASKS (SAMPLE_RATE * 4 / 1024)

tDMAControlTable adc_dma_task_list[] = {
	uDMATaskStructEntry(
		1024, UDMA_SIZE_16,
		UDMA_SRC_INC_NONE, (void *) (ADC0_BASE + ADC_O_SSFIFO0),
		UDMA_DST_INC_16, sample_buffer,
		UDMA_ARB_4, UDMA_MODE_PER_SCATTER_GATHER
	),
	uDMATaskStructEntry(
			1024, UDMA_SIZE_16,
			UDMA_SRC_INC_NONE, (void *) (ADC0_BASE + ADC_O_SSFIFO0),
			UDMA_DST_INC_16, sample_buffer + 1024,
			UDMA_ARB_4, UDMA_MODE_PER_SCATTER_GATHER
	),
	uDMATaskStructEntry(
			1024, UDMA_SIZE_16,
			UDMA_SRC_INC_NONE, (void *) (ADC0_BASE + ADC_O_SSFIFO0),
			UDMA_DST_INC_16, sample_buffer + 2048,
			UDMA_ARB_4, UDMA_MODE_PER_SCATTER_GATHER
	),
	uDMATaskStructEntry(
		1024, UDMA_SIZE_16,
		UDMA_SRC_INC_NONE, (void *) (ADC0_BASE + ADC_O_SSFIFO0),
		UDMA_DST_INC_16, sample_buffer +  3072,
		UDMA_ARB_4, UDMA_MODE_BASIC // final transfer in a scatter gather task must be AUTO or BASIC
	)
};

void adc_init()
{
	/*
	 * setup TIMER0A to provide a periodic ADC tigger, this timer isn't started until adc_start() is called
	 */

	SysCtlPeripheralEnable(SYSCTL_PERIPH_TIMER0);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_TIMER0);

	TimerDisable(TIMER0_BASE, TIMER_A);
	TimerConfigure(TIMER0_BASE, TIMER_CFG_PERIODIC);

	TimerLoadSet(TIMER0_BASE, TIMER_A, SysCtlClockGet() / SAMPLE_RATE);
	TimerControlTrigger(TIMER0_BASE, TIMER_A, true); // enable ADC triggering

	/*
	 * configure the ADC and DMA operation
	 */

	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);

	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOE);
	SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOD);

	//SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0); // done elsewhere

	GPIOPinTypeADC(GPIO_PORTD_BASE, GPIO_PIN_2 | GPIO_PIN_3);
	GPIOPinTypeADC(GPIO_PORTE_BASE, GPIO_PIN_1 | GPIO_PIN_2);

	uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC0, UDMA_ATTR_ALL);
	uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);
	uDMAChannelAssign(UDMA_CH14_ADC0_0);
	uDMAChannelDisable(UDMA_CHANNEL_ADC0);
	uDMAIntClear(UDMA_CHANNEL_ADC0);
	uDMAIntRegister(UDMA_INT_ERR, dma_int_handler);

	/* ADC configuration */

	ADCHardwareOversampleConfigure(ADC0_BASE, 4);

	ADCIntDisableEx(ADC0_BASE, ADC_INT_SS0 | ADC_INT_SS1 | ADC_INT_SS2 | ADC_INT_SS3);
	ADCIntClearEx(ADC0_BASE, ADC_INT_SS0 | ADC_INT_SS1 | ADC_INT_SS2 | ADC_INT_SS3);
	ADCIntRegister(ADC0_BASE, 0, adc_int_handler);

	ADCSequenceDisable(ADC0_BASE, 0);
	ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_TIMER, 0);
	//ADCSequenceConfigure(ADC0_BASE, 0, ADC_TRIGGER_PROCESSOR, 0);

	// AIN1 - Accelerometer x/y/z axis
	// AIN2 - Accelerometer x/y/z axis
	// AIN4 - Accelerometer x/y/z axis

	// AIN1 - Fun's external temp sensor

	ADCSequenceStepConfigure(ADC0_BASE, 0, 0, ADC_CTL_CH5);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 1, ADC_CTL_CH4);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 2, ADC_CTL_CH2);
	ADCSequenceStepConfigure(ADC0_BASE, 0, 3, ADC_CTL_CH1 | ADC_CTL_END | ADC_CTL_IE);

	ADCSequenceUnderflowClear(ADC0_BASE, 0);
	ADCSequenceOverflowClear(ADC0_BASE, 0);
	ADCIntClear(ADC0_BASE, 0);
	ADCIntEnable(ADC0_BASE, 0);

	ADCSequenceDMAEnable(ADC0_BASE, 0);
	ADCSequenceEnable(ADC0_BASE, 0);
}

/*
 * From the Tiva 123C errata sheet:
 *
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
 * laffo
 *
 */

void adc_start(void)
{
	adc_done = 0;

	uDMAChannelScatterGatherSet(UDMA_CHANNEL_ADC0,
		sizeof(adc_dma_task_list) / sizeof(adc_dma_task_list[0]),
		adc_dma_task_list,
		true // peripherial scatter gather since we want the ADC DMA requests to drive the thing
	);

	/* flush out ADC FIFO */
	uint32_t buffer[8];
	ADCSequenceDataGet(ADC0_BASE, 0, buffer);
	ADCSequenceUnderflowClear(ADC0_BASE, 0);
	ADCSequenceOverflowClear(ADC0_BASE, 0);

	uDMAChannelEnable(UDMA_CHANNEL_ADC0);
	TimerEnable(TIMER0_BASE, TIMER_A);
}

