/*
 * debug.c - Provides a printf for spewing crap over the debug UART
 *
 *  Created on: 30/03/2014
 *      Author: oliver
 */

#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_ints.h>
#include <inc/hw_uart.h>
#include <inc/hw_gpio.h>

#include <driverlib/udma.h>
#include <driverlib/gpio.h>
#include <driverlib/sysctl.h>
#include <driverlib/uart.h>
#include <driverlib/pin_map.h>

#include "firmware.h"

// debug printing via UART0, disable this crap for release builds

static volatile int buffered = 0, sent = 0;
static char print_buffer[256];

void debug_ISR(void)
{
	UARTIntClear(UART0_BASE, UART_INT_TX);

	if(sent == buffered) {
		buffered = 0;
		sent = 0;
	} else {
		UARTCharPutNonBlocking(UART0_BASE, print_buffer[sent]);
		sent++;
	}
}

void debug_init(void)
{
	// UART0 is the debug port interface

	SysCtlPeripheralEnable(SYSCTL_PERIPH_UART0);
	GPIOPinTypeUART(GPIO_PORTA_BASE, GPIO_PIN_0 | GPIO_PIN_1);
	GPIOPinConfigure(GPIO_PA0_U0RX);
	GPIOPinConfigure(GPIO_PA1_U0TX);

	UARTConfigSetExpClk(UART0_BASE, SysCtlClockGet(), 115200, UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE);
	UARTFIFOEnable(UART0_BASE);

	UARTTxIntModeSet(UART0_BASE, UART_TXINT_MODE_EOT);
	UARTIntRegister(UART0_BASE, debug_ISR);
	UARTIntEnable(UART0_BASE, UART_INT_TX);
	UARTEnable(UART0_BASE);
}

void debug_printf(const char *fmt, ...)
{
	va_list args;
	int length;

	va_start(args, fmt);
		length = vsnprintf(print_buffer + buffered, sizeof(print_buffer) - buffered, fmt, args);
	va_end(args);

	// shove stuff into the transmit FIFO until it's full.

	buffered += length;

	// add a notification if the buffer doesn't have enough room for the whole message
	if(buffered == sizeof(print_buffer)) {
		*(print_buffer + sizeof(print_buffer) - 1) = 0;
	/*
		const char clip_msg[] = "CLIPPED\r\n";
		char *end = print_buffer + sizeof(print_buffer) - sizeof(clip_msg);
		strcpy(end, clip_msg);
		*/
	}

	// if we aren't already transmitting, get things started
	if(!UARTBusy(UART0_BASE)) {
		UARTCharPutNonBlocking(UART0_BASE, print_buffer[0]);
		sent++;
	}
}
