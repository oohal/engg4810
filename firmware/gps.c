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

const char sleep10m[]   = "\xB5\x62\x02\x41\x08\x00\xC0\x27\x09\x00\x02\x00\x00\x00\x3D\x82";
const char cyclic_msg[] = "\xB5\x62\x06\x11\x02\x00\x08\x01\x22\x92";

volatile int gps_msg_ready = 0;
volatile int gps_data_ready = 0;
volatile char gps_buffer[399];

/* always leave room in the gps_buffer for the nul terminator */
#define TRANSFER_SIZE (sizeof(gps_buffer) - 1)

char *intreason = "";

void gps_uart_int_handler(void)
{
    uint32_t status = UARTIntStatus(UART1_BASE, true);

    UARTIntClear(UART1_BASE, status);

    /* DMA completion interrupt */
    if(uDMAIntStatus() & (1 << UDMA_CHANNEL_UART1RX)) { // UART1 is on DMA channel 0, apparently
    	uDMAIntClear((1 << UDMA_CHANNEL_UART1RX));
    	intreason = "DMA complete";
    	gps_msg_ready = 1;
    }

    if(uDMAIntStatus() & (1 << UDMA_CHANNEL_UART1TX)) { // TX DMA complete interrupt, ignore
    	uDMAIntClear((1 << UDMA_CHANNEL_UART1TX));
    }

    /* RX timeout interrupt */
    if(status & UART_INT_RT) {
    	uDMAChannelDisable(UDMA_CHANNEL_UART1RX);
    	gps_msg_ready = 1;
    	intreason = "timeout";
    }
}

static void gps_send(const char *msg, int length)
{
	/* configure transmitter DMA */
	static char buffer[50];

	memcpy(buffer, msg, length);

    uDMAChannelTransferSet(UDMA_CHANNEL_UART1TX,
   		UDMA_MODE_BASIC,
    	(char *) buffer,
    	(void *) (UART1_BASE + UART_O_DR),
    	length
    );

    uDMAChannelEnable(UDMA_CHANNEL_UART1TX);
}

static void setup_dma_transfer(void)
{
	uDMAChannelAttributeEnable(UDMA_CHANNEL_UART1RX, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);

    uDMAChannelTransferSet(UDMA_CHANNEL_UART1RX,
   		UDMA_MODE_BASIC,
    	(void *) (UART1_BASE + UART_O_DR),
    	(char *) gps_buffer,
    	TRANSFER_SIZE
    );

    uDMAChannelEnable(UDMA_CHANNEL_UART1RX);
}

void gps_init(void)
{
	// Using UART1, PB0 PB1
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);

    SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_GPIOB);
    SysCtlPeripheralSleepEnable(SYSCTL_PERIPH_UART1);

    GPIOPinConfigure(GPIO_PB0_U1RX);
    GPIOPinConfigure(GPIO_PB1_U1TX);

    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
    //GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0);

    // Configure the UART for 9600, 8-N-1 operation.
    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), GPS_BAUD_RATE,
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

    UARTFIFOEnable(UART1_BASE);
    UARTFIFOLevelSet(UART1_BASE, UART_FIFO_TX4_8, UART_FIFO_RX6_8);

//    uDMAChannelAssign(UDMA_CH23_UART1RX);
    uDMAChannelAttributeDisable(UDMA_CHANNEL_UART1RX, UDMA_ATTR_ALL);
	uDMAChannelAttributeEnable(UDMA_CHANNEL_UART1RX, UDMA_ATTR_USEBURST | UDMA_ATTR_HIGH_PRIORITY);

    uDMAChannelControlSet(UDMA_CHANNEL_UART1RX,
    	UDMA_PRI_SELECT |
    	UDMA_SIZE_8 |
    	UDMA_DST_INC_8 |
    	UDMA_SRC_INC_NONE |
    	UDMA_ARB_4
    );

    uDMAChannelAttributeDisable(UDMA_CHANNEL_UART1TX, UDMA_ATTR_ALL);
    uDMAChannelControlSet(UDMA_CHANNEL_UART1TX,
        UDMA_PRI_SELECT |
       	UDMA_SIZE_8 |
       	UDMA_DST_INC_NONE |
       	UDMA_SRC_INC_8 |
       	UDMA_ARB_1
      );

    UARTIntRegister(UART1_BASE, gps_uart_int_handler);
    UARTIntEnable(UART1_BASE, UART_INT_RT);

    memset((void*) gps_buffer, 0, sizeof(gps_buffer));

    UARTDMAEnable(UART1_BASE, UART_DMA_RX | UART_DMA_TX);

	// setup interrupts
    gps_msg_ready = 0;

    setup_dma_transfer();
    UARTEnable(UART1_BASE);
}

enum gps_state {
	GPS_OK,
	GPS_NO_FIX,
	GPS_WRONG_MSG
};

enum gps_state parse(char *msg, float *lat, float *lng, uint32_t *time, uint32_t *date)
{
    const char *prefix = "$GPRMC";

    char *date_s = NULL, *time_s = NULL, *lat_s = NULL, *lng_s = NULL;
    float ns_indicator = 0.0, ew_indicator = 0.0;
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
                if(*start == 'V') { // no fix or bad fix
                	no_fix = true;
                }
                break;

            case 3: lat_s  = start; break;

            case 4:  // north south indicator
            	ns_indicator = (*start == 'S' ? -1.0 : 1.0);
            	break;

            case 5: lng_s  = start; break;

            case 6:
            	ew_indicator = (*start == 'W' ? -1 : 1);
            	break;

            case 9: date_s = start; break;
        };

        start = end + 1;
        field++;
    } while(end);

    *lat = atof(lat_s) * (float) ns_indicator;
    *lng = atof(lng_s) * (float) ew_indicator;
    *time = atoi(time_s);
    *date = atoi(date_s);

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

int gps_update(float *lat, float *lng, uint32_t *time, uint32_t *date, uint32_t *gps_fix)
{
	/* the difference between TRANSFER_SIZE and the remaining chars is the number recieved */
    int offset = (TRANSFER_SIZE - dma_remaining(UDMA_CHANNEL_UART1RX)) - 1;
	char *start = (char *)gps_buffer;

	int sleeping = 0;

	static int no_fix_timer = 0;
	static int fix_timer = 0;
	static int in_cyclic = 0;

    /* Empty the FIFO */

    while(UARTCharsAvail(UART1_BASE)) {
    	uint32_t c = UARTCharGet(UART1_BASE);

    	if(offset < sizeof(gps_buffer) - 2) {
    		gps_buffer[offset++] = c;
    	}
    }

    gps_buffer[offset] = 0;

	//debug_printf("%s buffer: %s\r\n", intreason, gps_buffer);

	/* look for a NEMA message start char */
    while(1) {
		start = strchr((char *) start, '$');

		if(!start) { // no more NEMA messages
			break;
		}

		if(!verify_checksum(start)) { /* valid NEMA message */
			char *end = strchr(start, '*');

			uint32_t new_time, new_date;
			float new_lat, new_lng;

			if(!end) {
				break;
			}

			*end = 0;

			//debug_printf("chk passed\r\n", start);

			switch(parse(start,  &new_lat, &new_lng, &new_time, &new_date)) {
			case GPS_WRONG_MSG:
				/* odds are this is a GPTXT message that's generated on reset by the GPS
				 * might be able to do something useful with this information
				 */

				debug_printf("non RMC msg\r\n");
				break;

			case GPS_NO_FIX:
				*time = new_time; // even if we have no fix we might have valid times
				*date = new_date;
				*gps_fix = 0;
				fix_timer = 0;
				in_cyclic = 0; // the GPS has a nice "feature" where it'll reset if it loses fix
				               // while in power cyclic tracking mode.

				no_fix_timer++;
			/* i have no idea how well or if this works at all, either way not enabling it for submission

				if(no_fix_timer >= 300) { // no fix for 5 minutes, turn off the GPS for 10 minutes before trying to get another fix
					no_fix_timer = 0;
					gps_send(sleep10m, sizeof(sleep10m) - 1);
					sleeping = 1;
				}
			*/

				break;

			case GPS_OK: // update fix variables
				*lat  = new_lat;
				*lng  = new_lng;
				*time = new_time;
				*date = new_date;

				if(!fix_timer) {
					debug_printf("fix reaquired after %d\r\n", no_fix_timer);
				}

				*gps_fix = 1;
				no_fix_timer = 0;

				fix_timer++;

				// once we've had a fix for a minute start using cyclic track mode to save power
				// the GPS module tends to reset if it loses track in cyclic mode, so it'll reboot
				// and operate in max performance mode.

				if(fix_timer > 60 && !in_cyclic) {
					gps_send(cyclic_msg, sizeof(cyclic_msg) - 1);
					in_cyclic = 1;

					debug_printf("switching to cyclic\r\n");
				}

				break;
			}

			start = end + 1;
		} else {
			start = start + 1; // skip over this $ and try find another message

			debug_printf("chk failed\r\n");
		}
    }

    memset((void *) gps_buffer, 0, sizeof(gps_buffer));

	gps_msg_ready = 0;
	setup_dma_transfer();

	return sleeping;
}
