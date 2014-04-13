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
		uint32_t recv;
		uint8_t c;

		/* if we've sent the whole tx buffer, pad it out with 0xFF */
		if(i < txlen) {
			c = tx[i];
		} else {
			c = 0xFF;
		}

		SSIDataPutNonBlocking(SSI_SD, c);
		SSIDataGet(SSI_SD, &recv);

		if(i >= skip) {
			rx[rxed++] = recv;
		}
	}
}

#define MAX_WAIT 100

static void sd_cmd_raw(char *tx, int txlen, char *rx, int rxlen)
{
	int i = 0, store = 0;
	uint32_t recv;

	// Add a few dummy blocks to the start of each command. despite what the SD card spec says
	// SD cards are devious pieces of shit and won't necessarily byte align their responses in SPI mode
	// if they're busy.

	SSIDataPut(SSI_SD, 0xFF);
	SSIDataPut(SSI_SD, 0xFF);

	for(i = 0; i < txlen; i++) {
		SSIDataPut(SSI_SD, tx[i]);
		SSIDataGet(SSI_SD, &recv);

		if(recv != 0xFF) { // shouldn't happen
			die_horribly();
		}
	}

	for(i = 0; i < MAX_WAIT; i++) {
		SSIDataPutNonBlocking(SSI_SD, 0xFF);
		SSIDataGet(SSI_SD, &recv);

		// wait until we get a
		if(store || recv != 0xFF) {
			rx[store++] = recv;

			if(store >= rxlen)
				return;
		}
	}

	// didn't get a response within MAX_WAIT, so call it a failure
	die_horribly();
}

static sd_cmd(int cmd_index, int arg, char *response, int rxlen)
{
	// the inital reset needs this as it's CRC, it's not check for anything
	// else by default so we can just use this for everything.
	uint8_t crc7 = 0x92;
	char cmd[6];

	cmd[0] = 0x40 | (cmd_index & 0x3F); // 47:40 start, tx, cmd
	memcpy(cmd+1, &arg, sizeof(arg));     // 39:8  command argument
	cmd[5] = (crc7 << 1) | 1;           // 7:0   crc and stop bit

	sd_cmd_raw(cmd, sizeof(cmd), response, rxlen);
}


enum data_resp_token {
	DR_ONE    = 0x01,
	DR_STATUS = 0x0E,
	DR_ZERO   = 0x10
};

#define DATA_SINGLE 0xFE
#define DATA_START  0xFD
#define DATA_STOP   0xFC

enum r1_flags {
	R1_IDLE        = 0x01,
	R1_ERASE_RST   = 0x02,
	R1_ILLEGAL_CMD = 0x04,
	R1_CRC_ERR     = 0x08,

	R1_ERASE_ERR   = 0x10,
	R1_ADDR_ERR    = 0x20,
	R1_PARAM_ERR   = 0x40,
	R1_ZERO        = 0x80
};

static void interp_r1(uint8_t res) {
	debug_printf("res: %x\r\n", (uint32_t) res);
	debug_printf(" idle: %d\r\n", res & R1_IDLE);
	debug_printf(" erst: %d\r\n", res & R1_ERASE_RST);
	debug_printf(" ille: %d\r\n", res & R1_ILLEGAL_CMD);
	debug_printf(" crce: %d\r\n", res & R1_CRC_ERR);

	debug_printf(" eras: %d\r\n", res & R1_ERASE_ERR);
	debug_printf(" addr: %d\r\n", res & R1_ADDR_ERR);
	debug_printf(" para: %d\r\n", res & R1_PARAM_ERR);
	debug_printf(" zero: %d\r\n", res & R1_ZERO);
}

/*
void sd_read(void)
{
	char msg = "\x";
}
*/

void sd_init(void)
{
	// enable clocks
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);

	// initialise SSI0, 400kHz clock, 8 bit transfers
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
	// all that effective overall.

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

	// these both need valid CRCs, so send a premade version

	char cmd0[] = "\xFF\x40\x00\x00\x00\x00\x95"; // SD reset command \w CRC
	char cmd8[] = "\x48\x00\x00\x01\xAA\x87";

	char response = 0;

	char r3[50], r7[5];

	// after the command is recieved there is a delay between before the response is sent
	//
	// FIXME: For the cards i've tested with it's 1 byte delay, but this might be larger for other
	//        cards.

	sd_setcs(0);
		spi_exchange("\xFF", 1, &response, 1, 10);
	sd_setcs(1);

	spi_exchange("\xFF", 1, &response, 1, 2);

	sd_setcs(0); // Assert /CS and send CMD0
		sd_cmd_raw(cmd0, sizeof(cmd0) - 1, &response, sizeof(response));

		if(response == 0xFF) { // not an SD card, or not behaving like one
			sd_setcs(1);
			return;
		}

		sd_cmd_raw(cmd8, sizeof(cmd8) - 1, r7, 5);

		r3[0] = response;

		// send ACMD41 to initalise the card

		sd_cmd(58, 0, r7, sizeof(r7));
		debug_printf("cmd58: %x %x %x %x %x\r\n", (uint32_t) r7[0], (uint32_t) r7[1],
				(uint32_t) r7[2], (uint32_t) r7[3], (uint32_t) r7[4]);
		i = 0;

		while(response & R1_IDLE) {
			sd_cmd(55, 0, &response, 1);
			sd_cmd(41, 0x00000040, &response, 1);

			/* hang tight for a bit while the card does it's thing */

			for(i = 0; i < 20000; i++) {};
/*
			if(i > 10000) {
				sd_cmd(58, 0, r7, sizeof(r7));
				debug_printf("cmd58: %x %x %x %x %x\r\n", (uint32_t) r7[0], (uint32_t) r7[1],
								(uint32_t) r7[2], (uint32_t) r7[3], (uint32_t) r7[4]);

				i = 0;

				//debug_printf("sd card failed to initialise\r\n");
				//return;
			}
			i++; */
		}

		sd_setcs(1);
}
