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

void die_horribly();

// SD card interfacing, etc

void sd_init(void);

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
