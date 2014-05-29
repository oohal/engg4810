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
	GPIOPinWrite(CS_BASE, CS_PIN, i ? CS_PIN : 0x00);
}

static uint8_t spi_exchange(uint8_t send)
{
	uint32_t recv;
	SSIDataPutNonBlocking(SSI_SD, send);
	SSIDataGet(SSI_SD, &recv);

	return recv & 0xFF;
}

#define MAX_WAIT 100

static uint8_t sd_cmd_raw(const char *tx, int txlen, char *rx, int rxlen)
{
	int i = 0, store = 0;
	uint32_t recv;

	spi_exchange(0xFF);
	spi_exchange(0xFF);

	for(i = 0; i < txlen; i++) {
		recv = spi_exchange(tx[i]);

		if(recv != 0xFF) { // shouldn't happen
			die_horribly();
		}
	}

	for(i = 0; i < MAX_WAIT; i++) {
		recv = spi_exchange(0xFF);

		// wait until we get a non-0xFF
		if(store || recv != 0xFF) {
			rx[store++] = recv;

			if(store >= rxlen)
				return rx[0];
		}
	}

	// didn't get a response within MAX_WAIT, so call it a failure
//	die_horribly();
	return 0xFF;
}

uint8_t sd_cmd(int cmd_index, int arg, char *response, int rxlen)
{
	uint8_t crc7 = 0xFE;
	char cmd[6];

	cmd[0] = 0x40 | (cmd_index & 0x3F); // 47:40 start, tx, cmd
	cmd[1] = (0xFF000000 & arg) >> 24;  // 39:32 arg 31:24
	cmd[2] = (0x00FF0000 & arg) >> 16;  // 31:24 arg 23:16
	cmd[3] = (0x0000FF00 & arg) >> 8;   // 23:16 arg 15:8
	cmd[4] = (0x000000FF & arg);        // 15:8  arg  7:0
	cmd[5] = (crc7 << 1) | 1;           //  7:0  crc (unused) and stop bit

	return sd_cmd_raw(cmd, sizeof(cmd), response, rxlen);
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
	debug_printf(" illg: %d\r\n", res & R1_ILLEGAL_CMD);
	debug_printf(" crce: %d\r\n", res & R1_CRC_ERR);

	debug_printf(" eras: %d\r\n", res & R1_ERASE_ERR);
	debug_printf(" addr: %d\r\n", res & R1_ADDR_ERR);
	debug_printf(" para: %d\r\n", res & R1_PARAM_ERR);
	debug_printf(" zero: %d\r\n", res & R1_ZERO);
}

int sd_read_block(int i, char *into)
{
	uint32_t response;
	char a;

	//sd_setcs(0);
		sd_cmd(17, i, &a, 1);

		if(a) {
			debug_printf("sd read failed %x\r\n");
			interp_r1(a);
			return 1;
		}

		// wait for the data start token

	do {
		response = spi_exchange(0xFF);
	} while(response == 0xFF);

	// error tokens look like: 0000 xxxx
	if(!(response & 0xF0) && (response & 0x0F)) {
		return 1;
	}

	// read the sector, CRC needs to be read also, but we don't care about it
	for(i = 0; i < 512; i++) {
		into[i] = spi_exchange(0xFF);
	}

	// receive the CRC
	spi_exchange(0xFF);
	spi_exchange(0xFF);

	//sd_setcs(1);

	return 0;
}

#define TIMEOUT 1000

int sd_write_block(int index, const char *block)
{
	uint32_t i, timeout = 0, response = 0;
	const uint8_t START = 0xFE;
	char a;

	sd_cmd(24, index, &a, 1);

	if(a) {
		debug_printf("sd write failed %x\r\n");
		interp_r1(a);
		return 1;
	}

	// send the start of block token
	spi_exchange(START);

	for(i = 0; i < 512; i++) {
		spi_exchange(block[i]);
	}

	// output idles high until we get a data response token, dummy crc of 0xFFFF is sent as part of this
	do {
		response = spi_exchange(0xFF);
	} while(response == 0xFF); // busy wait until we've got a data response

	// idle low until we write is complete
	while((a = spi_exchange(0xFF)) != 0xFF) {
		if(timeout > TIMEOUT)
			return 1;

		timeout++;
	}

	return !(response & 0x05); // return the data response token
}

void sd_speed(int speed)
{
	//
	//const uint32_t new_clock = speed ? 12500000 : 400000;
	uint32_t new_clock = 400000;

	SSIDisable(SSI_SD);

	SSIConfigSetExpClk(SSI_SD,
		SysCtlClockGet(),
		SSI_FRF_MOTO_MODE_3,
		SSI_MODE_MASTER,
		new_clock,
		8
	);

	SSIEnable(SSI_SD);
}

static void sd_hw_init(void)
{
	// enable clocks
	SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOA);
	SysCtlPeripheralEnable(SYSCTL_PERIPH_SSI0);

	// configure the SPI TX and RX DMA channels


	// initialise SSI0, 400kHz clock, 8 bit transfers
	sd_speed(0);

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
}

int sd_init(void)
{
	// these both need valid CRCs, so send a premade version
	const char cmd0[] = "\xFF\x40\x00\x00\x00\x00\x95"; // SD reset command \w CRC
	const char cmd8[] = "\x48\x00\x00\x01\xAA\x87";
	char response, r7[5];
	int i;

	sd_hw_init();

	// send the inital clock train of 80 clocks
	for(i = 0; i < 10; i++) {
		spi_exchange(0xFF);
	}

	sd_setcs(0); // Assert /CS and send CMD0

		sd_cmd_raw(cmd0, sizeof(cmd0) - 1, &response, 1);

		if(response == 0xFF) { // not an SD card, or not behaving like one
			sd_setcs(1);
			return 1;
		}

		sd_cmd_raw(cmd8, sizeof(cmd8) - 1, r7, 5);

		// send ACMD41 to initalise the card
		response = 1;
		while(response & R1_IDLE) {
			sd_cmd(55, 0, &response, 1);
			sd_cmd(41, 0x40000000, &response, 1);

			/* hang tight for a bit while the card does it's thing */
			for(i = 0; i < 50000; i++) {};
		}

		sd_cmd(58, 0, r7, sizeof(r7));
		debug_printf("cmd58: %x %x %x %x %x\r\n", (uint32_t) r7[0], (uint32_t) r7[1],
				(uint32_t) r7[2], (uint32_t) r7[3], (uint32_t) r7[4]);

		return 0;
}


