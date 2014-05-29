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
#include "inc/hw_uart.h"

#define GPS_BAUD_RATE 9600

const char *m1 = "\xB5\x62\x06\x09\x0D\x00\xFF\xFB\x00\x00\x00\x00\x00\x00\xFF\xFF\x00\x00\x17\x2B\x7E"; // UBX CFG revert all but antenna config
const char *m2 = "\xB5\x62\x06\x01\x03\x00\xF0\x00\x00\xFA\x0F"; // disable nema fix data msg
const char *m3 = "\xB5\x62\x06\x01\x03\x00\xF0\x01\x00\xFB\x11"; // disable lat/long msg
const char *m4 = "\xB5\x62\x06\x01\x03\x00\xF0\x02\x00\xFC\x13"; // disable dop & active satellites
const char *m5 = "\xB5\x62\x06\x01\x03\x00\xF0\x03\x00\xFD\x15"; // disable satellites in view
const char *m6 = "\xB5\x62\x06\x01\x03\x00\xF0\x05\x00\xFF\x19"; // disable course over ground speed msg

volatile int buf_index = 0;
volatile int gps_msg_ready = 0;
volatile int gps_data_ready = 0;
volatile char gps_buffer[199];

/* always leave room in the gps_buffer for the nul terminator */
#define TRANSFER_SIZE (sizeof(gps_buffer) - 1)

char *intreason = "";

void gps_uart_int_handler(void)
{
    uint32_t status = UARTIntStatus(UART2_BASE, true);

    UARTIntClear(UART2_BASE, status);

    /* DMA completion interrupt */
    if(uDMAIntStatus() & 1) { // UART2 is on DMA channel 0, apparently
    	uDMAIntClear(1);
    	intreason = "DMA complete";
    	gps_msg_ready = 1;
    }

    /* RX timeout interrupt */
    if(status & UART_INT_RT) {
    	uDMAChannelDisable(UDMA_SEC_CHANNEL_UART2RX_0);
    	gps_msg_ready = 1;
    	intreason = "timeout";
    }
}

static void uart_send(const char *msg, int len)
{
	do {
		UARTCharPut(UART2_BASE, *msg);
		msg++; len--;
	} while(len);
}

static void setup_dma_transfer(void)
{
	uDMAChannelAttributeEnable(UDMA_SEC_CHANNEL_UART2RX_0, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);

    uDMAChannelTransferSet(UDMA_SEC_CHANNEL_UART2RX_0,
   		UDMA_MODE_BASIC,
    	(void *) (UART2_BASE + UART_O_DR),
    	(char *) gps_buffer,
    	TRANSFER_SIZE
    );

    uDMAChannelEnable(UDMA_SEC_CHANNEL_UART2RX_0);
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

    // Configure the UART for 9600, 8-N-1 operation.
    UARTConfigSetExpClk(UART2_BASE, SysCtlClockGet(), GPS_BAUD_RATE,
    		(UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE |
    		UART_CONFIG_PAR_NONE));

    /*
     * In order for RX timeout interrupts to be generated there must be  32 bit periods must
     * pass with no activity AND data in the RX FIFO. Burst requests are generated when the
     * FIFO level is exceeded, so if we only use bursts, have an DMA arbitration size of 4
     * and set the RX FIFO level to 6/8 we can ensure that there is data in the FIFO so the
     * timeout interrupt will be generated.
     *
     * There is a pathological case where the bus is in use for long enough that the FIFO fills up and
     * successive burst requests empty it, but considering the low bit rate in use here, it's pretty
     * unlikely. Even if this does occur the next GPS message will fill the buffer resulting in a that
     * DMA completion interrupt which ensures there is at most a two second delay between GPS updates.
     */

    UARTFIFOEnable(UART2_BASE);
    UARTFIFOLevelSet(UART2_BASE, UART_FIFO_TX4_8, UART_FIFO_RX6_8);

    uDMAChannelAssign(UDMA_CH0_UART2RX);
    uDMAChannelAttributeDisable(UDMA_SEC_CHANNEL_UART2RX_0, UDMA_ATTR_ALL);
	uDMAChannelAttributeEnable(UDMA_SEC_CHANNEL_UART2RX_0, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);

    uDMAChannelControlSet(UDMA_SEC_CHANNEL_UART2RX_0,
    	UDMA_PRI_SELECT |
    	UDMA_SIZE_8 |
    	UDMA_DST_INC_8 |
    	UDMA_SRC_INC_NONE |
    	UDMA_ARB_4
    );

    UARTIntRegister(UART2_BASE, gps_uart_int_handler);
    UARTIntEnable(UART2_BASE, UART_INT_RT);// | UART_INT_RX);

    memset((void*) gps_buffer, 0, sizeof(gps_buffer));

    UARTDMAEnable(UART2_BASE, UART_DMA_RX); // | UART_DMA_ERR_RXSTOP); // );

	// setup interrupts

    buf_index = 0;
    gps_msg_ready = 0;

    setup_dma_transfer();
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

    char *date_s = NULL, *time_s = NULL, *lat_s = NULL, *lng_s = NULL;
    char *end, *start = msg;
    int field = 0;
    bool no_fix = false;

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
                	no_fix = true;
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

    if(no_fix) {
    	return GPS_NO_FIX;
    }

    return GPS_OK;
}

int verify_checksum(char *str)
{
	char *end;
	int sum;

	/* The NEMA checksum is the XOR of everything between $ and * not inclusive */
	for(sum = 0, str++; *str != '*'; str++) {
		if(*str == 0)
			return 1;

		sum ^= *str;
	}

	// The two chars after the * are the checksum in hex. Convert them to an int and
	// compare sums. Also check that only two chars were converted to the sum.
	if(strtol(str + 1, &end, 16) != sum || end != str + 3)
		return 1;

	return 0;
}

/* returns the number of items left to transfer
 *
 * NB: channel must be the channel index not bit mask
 */

int dma_remaining(uint32_t channel)
{
	tDMAControlTable *table = uDMAControlBaseGet();

	return (table[channel].ui32Control & 0x3FF0) >> 4;
}

void gps_update(float *lat, float *lng, uint32_t *time, uint32_t *date)
{
	/* the difference between TRANSFER_SIZE and the remaining chars is the number recieved */
    int offset = (TRANSFER_SIZE - dma_remaining(UDMA_SEC_CHANNEL_UART2RX_0)) - 1;
	char *start;

    /* Empty the FIFO */

    while(UARTCharsAvail(UART2_BASE)) {
    	uint32_t c = UARTCharGet(UART2_BASE);

    	if(offset < sizeof(gps_buffer) - 2) {
    		gps_buffer[offset++] = c;
    	}
    }

    gps_buffer[offset] = 0;

	//debug_printf("%s buffer: %s\r\n", intreason, gps_buffer);

	/* look for a NEMA message start char */

	start = strchr((char *) gps_buffer, '$');

	if(start && !verify_checksum(start)) { /* valid NEMA message */
		uint32_t new_time, new_date;
		float new_lat, new_lng;

		//debug_printf("chk passed\r\n", start);

		switch(parse(start,  &new_lat, &new_lng, &new_time, &new_date)) {
		case GPS_WRONG_MSG:
        	debug_printf("non RMC msg\r\n");
        	break;

		case GPS_ERR: // this really shouldn't ever happen
			//debug_printf("gps err\r\n");
			//gps_reconfigure();
			break;

		case GPS_NO_FIX:
			//debug_printf("no gps fix\r\n");
			*time = new_time;
			*date = new_date;
			break;

		case GPS_OK: // update fix variables
			*lat  = new_lat;
			*lng  = new_lng;
			*time = new_time;
			*date = new_date;
			break;
		}
	} else {
		debug_printf("chk failed\r\n");
	}

	gps_msg_ready = 0;
	setup_dma_transfer();
}
