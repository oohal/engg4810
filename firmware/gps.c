/*
 * gps.c
 *
 *  Created on: 22/05/2014
 *      Author: oliver
 */

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "firmware.h"

#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "driverlib/debug.h"
#include "driverlib/fpu.h"
#include "driverlib/gpio.h"
#include "driverlib/interrupt.h"
#include "driverlib/pin_map.h"
#include "driverlib/rom.h"
#include "driverlib/sysctl.h"
#include "driverlib/uart.h"
#include "driverlib/pin_map.h"


#include "inc/hw_gpio.h"
#include "inc/hw_ints.h"
#include "inc/hw_memmap.h"
#include "inc/hw_sysctl.h"
#include "inc/hw_types.h"



const char *m1 = "\xB5\x62\x06\x09\x0D\x00\xFF\xFB\x00\x00\x00\x00\x00\x00\xFF\xFF\x00\x00\x17\x2B\x7E"; // UBX CFG revert all but antenna config
const char *m2 = "\xB5\x62\x06\x01\x03\x00\xF0\x00\x00\xFA\x0F"; // disable nema fix data msg
const char *m3 = "\xB5\x62\x06\x01\x03\x00\xF0\x01\x00\xFB\x11"; // disable lat/long msg
const char *m4 = "\xB5\x62\x06\x01\x03\x00\xF0\x02\x00\xFC\x13"; // disable dop & active satellites
const char *m5 = "\xB5\x62\x06\x01\x03\x00\xF0\x03\x00\xFD\x15"; // disable satellites in view
const char *m6 = "\xB5\x62\x06\x01\x03\x00\xF0\x05\x00\xFF\x19"; // disable course over ground speed msg

volatile int buf_index = 0;
volatile int gps_msg_ready = 0;
volatile char gps_buffer[100];

void GPSUARTInt(void)
{
    uint32_t ui32Status, c;

    ui32Status = UARTIntStatus(UART2_BASE, true);
    UARTIntClear(UART2_BASE, ui32Status);

    while(UARTCharsAvail(UART2_BASE)) {
    	c = UARTCharGetNonBlocking(UART2_BASE);

    	if(gps_msg_ready) {
    		continue;
    	}

    	if(c == '$') { // start of message indicator
    		buf_index = 0;
    	}

    	if(c == '*') { // end of msg indicator
    		gps_msg_ready = 1;
    		c = 0; // null out the newline char
    	}

    	gps_buffer[buf_index++] = c;
    }
}

#define BAUD 9600

static void uart_send(const char *msg, int len)
{
	do {
		UARTCharPut(UART2_BASE, *msg);
		msg++; len--;
	} while(len);
}

void gps_init(void)
{
	// Using UART2, PD6 PD7
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART2);

    // PD7 is special and needs to be unlocked before we can use it
    HWREG(GPIO_PORTD_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTD_BASE + GPIO_O_CR) = 0x80;

    GPIOPinConfigure(GPIO_PD6_U2RX);
    GPIOPinConfigure(GPIO_PD7_U2TX);

    GPIOPinTypeUART(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);

    // Configure the UART for 9600, 8-N-1 operation.

    UARTConfigSetExpClk(UART2_BASE, SysCtlClockGet(), BAUD,
    		(UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
    		UART_CONFIG_PAR_NONE));

	// setup interrupts

    buf_index = 0;
    gps_msg_ready = 0;

    UARTEnable(UART2_BASE);

    /* send all the gps config messages */

    /*
    int i = 0;
    uart_send(m1, sizeof(m1) - 1);
    uart_send(m2, sizeof(m2) - 1);
    uart_send(m3, sizeof(m3) - 1);
    uart_send(m4, sizeof(m4) - 1);
    uart_send(m5, sizeof(m5) - 1);
    uart_send(m6, sizeof(m6) - 1);
*/
    UARTDisable(UART2_BASE);

    UARTIntRegister(UART2_BASE, GPSUARTInt);
    UARTIntEnable(UART2_BASE, UART_INT_RX);

    UARTEnable(UART2_BASE);
}

int parse(char *msg)
{
    const char *prefix = "$GPRMC";

    char *end, *start = msg;
    char *date, *time, *lat, *lng;
    int field = 0;

    do {
        end = strchr(start, ',');

        if(end) {
            *end = 0;
        }

        //printf("%d - '%s'\n", field, start);

        switch(field) {
            case 0:
                if(strncmp(start, prefix, sizeof(prefix) - 1)) { // not the message we're looking for
                    return -1;
                }
                break;

            case 1: time = start; break;
            case 2:
                if(*start == 'V') { // no fix or bad fix, exit
                	debug_printf("no fix\r\n");
                    return -1;
                }
                break;

            case 3: lat  = start; break;
            case 5: lng  = start; break;
            case 9: date = start; break;
        };

        start = end + 1;
        field++;
    } while(end);

    debug_printf("%s - %s (%s, %s)\r\n", date, time, lat, lng);

    return 0;
}

void gps_update(void)
{
	if(gps_msg_ready) {
		/*
		char *start = strchr((const char*) gps_buffer, '$');

		if(start)
			parse(start);
		else
			debug_printf("argh '%s'\r\n", gps_buffer);
		 */

		gps_buffer[buf_index] = 0;
		buf_index = 0;
		debug_printf("'%s'\r\n", gps_buffer);

		gps_msg_ready = 0;
	}
}
