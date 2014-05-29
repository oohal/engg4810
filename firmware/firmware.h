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

// ADC stuff

// NOTE: if you change the sampling rate the DMA task list stuff in analog.c also
//       needs to be changed to match the new buffer size

#define SAMPLE_RATE 512

extern volatile int udma_done;
extern uint16_t sample_buffer[4 * SAMPLE_RATE];

void adc_init(void);
void adc_start(void);

// gps handling functions

extern volatile int gps_msg_ready;
extern volatile int gps_data_ready;

void gps_init(void);
void gps_update(float *lat, float *lng, uint32_t *time, uint32_t *date);

#endif /* FIRMWARE_H_ */
