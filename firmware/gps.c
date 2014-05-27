/*
 * gps.c
 *
 *  Created on: 22/05/2014
 *      Author: oliver
 */

#include <string.h>
#include <stdlib.h>
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


#define GPS_BAUD_RATE 9600


const char *m1 = "\xB5\x62\x06\x09\x0D\x00\xFF\xFB\x00\x00\x00\x00\x00\x00\xFF\xFF\x00\x00\x17\x2B\x7E"; // UBX CFG revert all but antenna config
const char *m2 = "\xB5\x62\x06\x01\x03\x00\xF0\x00\x00\xFA\x0F"; // disable nema fix data msg
const char *m3 = "\xB5\x62\x06\x01\x03\x00\xF0\x01\x00\xFB\x11"; // disable lat/long msg
const char *m4 = "\xB5\x62\x06\x01\x03\x00\xF0\x02\x00\xFC\x13"; // disable dop & active satellites
const char *m5 = "\xB5\x62\x06\x01\x03\x00\xF0\x03\x00\xFD\x15"; // disable satellites in view
const char *m6 = "\xB5\x62\x06\x01\x03\x00\xF0\x05\x00\xFF\x19"; // disable course over ground speed msg

volatile int buf_index = 0;
volatile int gps_msg_ready = 0;
volatile char gps_buffer[100];

void gps_uart_int_handler(void)
{
    uint32_t status, c;

    status = UARTIntStatus(UART2_BASE, true);
    UARTIntClear(UART2_BASE, status);

    while(UARTCharsAvail(UART2_BASE)) {
    	c = UARTCharGetNonBlocking(UART2_BASE);

    	if(buf_index < sizeof(gps_buffer) - 1) {
    		gps_buffer[buf_index++] = c;
    	}
    }

    if(status & UART_INT_RT) {
    	gps_msg_ready = 1;
    	UARTDisable(UART2_BASE);
    }
}

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

    // PD7 is special and needs to be unlocked before we can reconfigure it
    HWREG(GPIO_PORTD_BASE + GPIO_O_LOCK) = GPIO_LOCK_KEY;
    HWREG(GPIO_PORTD_BASE + GPIO_O_CR) = 0x80;

    GPIOPinConfigure(GPIO_PD6_U2RX);
    GPIOPinConfigure(GPIO_PD7_U2TX);

    GPIOPinTypeUART(GPIO_PORTD_BASE, GPIO_PIN_6 | GPIO_PIN_7);

    /*
    uDMAChannelAttributeDisable(UDMA_CHANNEL_ADC0, UDMA_ATTR_ALL);
    uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);
    uDMAChannelAssign(UDMA_CH14_ADC0_0);


    //uDMAChannelAttributeEnable(UDMA_CHANNEL_ADC0, UDMA_ATTR_USEBURST);
    uDMAChannelControlSet(UDMA_CHANNEL_UART2,
    	UDMA_PRI_SELECT |
    	UDMA_SIZE_8 |
    	UDMA_DST_INC_8 |
    	UDMA_SRC_INC_NONE |
    	UDMA_ARB_1
    );
*/

    // Configure the UART for 9600, 8-N-1 operation.
    UARTConfigSetExpClk(UART2_BASE, SysCtlClockGet(), GPS_BAUD_RATE,
    		(UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
    		UART_CONFIG_PAR_NONE));

    UARTFIFOEnable(UART2_BASE);

    buf_index = 0;
    gps_msg_ready = 0;

    /* send all the gps config messages */

    /*
	UARTEnable(UART2_BASE);

    int i = 0;
    uart_send(m1, sizeof(m1) - 1);
    uart_send(m2, sizeof(m2) - 1);
    uart_send(m3, sizeof(m3) - 1);
    uart_send(m4, sizeof(m4) - 1);
    uart_send(m5, sizeof(m5) - 1);
    uart_send(m6, sizeof(m6) - 1);

	UARTDisable(UART2_BASE);
*/

	// setup interrupts
    UARTIntRegister(UART2_BASE, gps_uart_int_handler);
    UARTIntEnable(UART2_BASE, UART_INT_RX | UART_INT_RT);

    UARTEnable(UART2_BASE);
}

enum gps_state {
	GPS_OK,
	GPS_NO_FIX,
	GPS_ERR,
	GPS_WRONG_MSG
};

enum gps_state parse(char *msg, float *lat, float *lng, uint32_t *time, uint32_t *date)
{
    const char *prefix = "$GPRMC";

    char *end, *start = msg;
    char *date_s = NULL, *time_s = NULL, *lat_s = NULL, *lng_s = NULL;
    int field = 0;

    do {
        end = strchr(start, ',');

        if(end) {
            *end = 0;
        }

        switch(field) {
            case 0:
                if(strncmp(start, prefix, sizeof(prefix) - 1)) { // not the message we're looking for
                    return GPS_WRONG_MSG;
                }
                break;

            case 1: time_s = start; break;
            case 2:
                if(*start == 'V') { // no fix or bad fix, exit
                	debug_printf("no fix\r\n");
                    return GPS_NO_FIX;
                }
                break;

            case 3: lat_s  = start; break;
            case 5: lng_s  = start; break;
            case 9: date_s = start; break;
        };

        start = end + 1;
        field++;
    } while(end);

    *lat = atof(lat_s);
    *lng = atof(lng_s);
    *time = atoi(time_s);
    *date = atoi(date_s);

  //  debug_printf("lat: (%s,%s) -> (%f,%f)\r\n", lat_s, lng_s, *lat, *lng);
    //debug_printf("time: %s-%s -> %d-%d\r\n", date_s, time_s, *date, *time);

    //debug_printf("lat: '%s' - %f\r\n %s (%s, %s)\r\n", date, time, lat, lng);

    return GPS_OK;
}

int verify_checksum(char *str)
{
	int my_sum = 0;
	char *end;

	str++; // skip the $

	/* the NEMA checksum is the xor of everything between $ and * not inclusive */

	while(*str != '*') {
		if(*str == 0)
			return 1;

		my_sum ^= *str++;
	}

	str++;

	int s = strtol(str, &end, 16);
	// two chars after the * are the checksum in hex, convert them to an int and
	// compare sums. Also check that only two chars were converted to the sum.
	if(strtol(str, &end, 16) != my_sum || end != str+2)
		return 1;

	return 0;
}

void gps_update(float *lat, float *lng, uint32_t *time, uint32_t *date)
{
	char *start = (char *) gps_buffer;

	if(!gps_msg_ready) {
		return;
	}


	gps_buffer[buf_index] = '\0';

	//debug_printf("buffer: %s\r\n", gps_buffer);

	/* sanity checking */
	start = strchr(start, '$');

	if(start && !verify_checksum(start)) {
		uint32_t new_time, new_date;
		float new_lat, new_lng;

		switch(parse(start,  &new_lat, &new_lng, &new_time, &new_date)) {
		case GPS_WRONG_MSG:
        	debug_printf("bad prefix\r\n");
        	break;
		case GPS_ERR: // this really shouldn't ever happen
			debug_printf("gps err\r\n");
			//gps_reconfigure();
			break;

		case GPS_NO_FIX:
			debug_printf("no gps fix\r\n");
			break;

		case GPS_OK: // update fix variables
			//debug_printf("gps fix ok: %u-%u (%f, %f)\r\n", new_date, new_time, new_lat, new_lng);

			*lat  = new_lat;
			*lng  = new_lng;
			*time = new_time;
			*date = new_date;
			break;
		}

	}else {
		debug_printf("chk failed\r\n");
	}

	buf_index = 0;
	gps_msg_ready = 0;
	UARTEnable(UART2_BASE);
}
