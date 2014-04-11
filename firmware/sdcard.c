#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <inc/hw_memmap.h>
#include <inc/hw_types.h>
#include <inc/hw_ints.h>
#include <inc/hw_ssi.h>
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

#include "firmware.h"

/* SSI interface dedicated to the SD card, if you change this update
 * the pin configuration in sd_init() to match
 */

#define SSI_SD SSI0_BASE
#define CS_BASE GPIO_PORTA_BASE
#define CS_PIN GPIO_PIN_3

static volatile int sd_available; // There is a SD message ready for processing

void ssi_interrupt(void)
{
	//SSIIntClear(SSI_SD);
	sd_available = 1;
}


static void sd_setcs(int i) // sets the state of the CS pin
{
	if(i) {
		GPIOPinWrite(CS_BASE, CS_PIN, CS_PIN);
	} else {
		GPIOPinWrite(CS_BASE, CS_PIN, 0x00);
	}
}

/* Sends and recieves a message over the SPI bus
 *
 * tx    - pointer to txlen characters to transmit
 * txlen - number of characters to transmit from txbuf
 *
 * rx - pointer to a buffer that is atleast rxlen long
 * offset - the first offset characters that are recieved will not be written into rx
 */

static void spi_exchange(char *tx, int txlen, char *rx, int rxlen, int skip)
{
	int i = 0, rxed = 0;

	for(i = 0; i < rxlen + skip; i++) {
		uint8_t c;
		uint32_t recv;

		/* if we've sent the whole tx buffer, pad it out with 0xFF */
		if(i < txlen) {
			c = tx[i];
		} else {
			c = 0xFF;
		}

		SSIDataPutNonBlocking(SSI_SD, c);
		SSIDataGet(SSI_SD, &recv);

		if(i > skip) {
			rx[rxed++] = recv;
		}
	}
}

void sd_init(void)
{
	// initialise the SSI interface, 400kHz clock, 8 bit transfers
	SSIConfigSetExpClk(SSI_SD,
		SysCtlClockGet(),
		SSI_FRF_MOTO_MODE_3,
		SSI_MODE_MASTER,
		400000,
		8
	);

	//SSIIntRegister(SSI_SD, ssi_interrupt);
	//SSIIntEnable(SSI_SD, SSI_RXTO); // enable recieve timeout interrupt
	SSIEnable(SSI_SD);

	// in CPOL = 1 mode the clock pin needs to be pulled up, so enable the pull up
	// Apparently the SD card itself requires 50 klohm pullups on each I/O
	// and the internal pullups are specced as 13-30 klohm so this may not be
	// all that effective overall

	GPIOPadConfigSet(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_3 | GPIO_PIN_4 | GPIO_PIN_5, GPIO_STRENGTH_8MA, GPIO_PIN_TYPE_STD_WPU);
	GPIOPinTypeSSI(GPIO_PORTA_BASE, GPIO_PIN_2 | GPIO_PIN_4 | GPIO_PIN_5);
	GPIOPinTypeGPIOOutput(GPIO_PORTA_BASE, GPIO_PIN_3);

	// now that they're configured, switch the to pins to peripherial control
	GPIOPinConfigure(GPIO_PA2_SSI0CLK);
	GPIOPinConfigure(GPIO_PA4_SSI0RX);
	GPIOPinConfigure(GPIO_PA5_SSI0TX);

	sd_setcs(1);

	// now do the actual card initialisation

	/* On power up the card is in SD mode, to switch to SPI mode we need to
	 * 1. Assert /CS
	 * 2. Issue CMD0, CMD0 with /CD asserted puts the card into SPI mode.
	 * 3. recieve the R1 response of the reset packet.
	 */

	int i;

	// after power up the SD card needs atleast 74 clock cycles to clear internal state
	// so feed it these before trying to communicate

	char idle_msg[] = "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF";

	spi_exchange(idle_msg, sizeof(idle_msg), NULL, 0, sizeof(idle_msg));

	sd_setcs(0); // Assert /CS and send CMD0

	char response[10];
	char msg[] = "\xFF\x40\x00\x00\x00\x00\x95"; // SD reset command \w CRC
	spi_exchange(msg, sizeof(msg), response, sizeof(response), sizeof(msg));

	sd_setcs(1); // clock

	debug_printf("rxed:");
	for(i = 0; i < sizeof(response); i++) {
		debug_printf(" %x", (uint32_t) response[i]);
	}

	debug_printf("\r\n");
}
