/*
 * firmware.h
 *
 *  Created on: 25/03/2014
 *      Author: Oliver
 */

#ifndef FIRMWARE_H_
#define FIRMWARE_H_

#include <stdint.h>
#include <driverlib/udma.h>

extern tDMAControlTable dma_channel_list[64];

void die_horribly(void);

// SD card interfacing, etc

int sd_init(void);

int sd_write_block(int index, const char *block);
int sd_read_block(int i, char *into);
uint8_t sd_cmd(int cmd_index, int arg, char *response, int rxlen);

// debugging routines, these get NOPed out for non-debug builds

void debug_init(void);
void debug_clear(void);
void debug_printf(const char *fmt, ...);

// ADC handlers

extern volatile int udma_done;
extern uint16_t sample_buffer[16];

void adc_init(void);
void adc_reinit(void);

#endif /* FIRMWARE_H_ */
