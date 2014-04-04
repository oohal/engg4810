/*
 * firmware.h
 *
 *  Created on: 25/03/2014
 *      Author: Oliver
 */

#ifndef FIRMWARE_H_
#define FIRMWARE_H_

void die_horribly();

// SD card interfacing, etc

void sd_init(void);

// debugging routines, these get NOPed out for non-debug builds

void debug_init(void);
void debug_clear(void);
void debug_printf(const char *fmt, ...);

#endif /* FIRMWARE_H_ */
