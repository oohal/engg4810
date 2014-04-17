/*-----------------------------------------------------------------------*/
/* Low level disk I/O module skeleton for FatFs     (C)ChaN, 2013        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control module to the FatFs module with a defined API.        */
/*-----------------------------------------------------------------------*/
#include <stdbool.h>

#include "diskio.h"		/* FatFs lower layer API */
#include "../firmware.h"

/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

static int init_done = 0;

DSTATUS disk_initialize (BYTE pdrv)
{
	if(pdrv > 0) {
		return STA_NOINIT;
	}

	if(!init_done)
		sd_init();

	init_done = 1;

	return 0;
}

/*-----------------------------------------------------------------------*/
/* Get Disk Status                                                       */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (BYTE pdrv)
{
	char r2[2];

	if(pdrv > 0)
		return STA_NODISK;

	if(!init_done)
		return STA_NOINIT;

	// send CMD13 to get the card status,
	sd_cmd(13, 0, r2, 2);

	if(r2[0] & 0x01)
		return STA_NOINIT;

	if(r2[1] & 0x01)
		return STA_PROTECT;

	return 0;
}

/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE *buffer,	/* Data buffer to store read data */
	DWORD sector,	/* Sector address (LBA) */
	UINT count		/* Number of sectors to read (1..128) */
)
{
	DWORD final = sector + count;

	if(pdrv > 0) {
		return RES_NOTRDY;
	}

	while(sector < final) {
		sd_read_block(sector, (char *) buffer);

		buffer += 512;
		sector++;
	}

	return RES_OK;
}

/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if _USE_WRITE
DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber (0..) */
	const BYTE *buffer,	/* Data to be written */
	DWORD sector,		/* Sector address (LBA) */
	UINT count			/* Number of sectors to write (1..128) */
)
{
	if(pdrv > 0) {
		return RES_NOTRDY;
	}

	UINT final = sector + count;

	while(sector < final) {
		sd_write_block(sector, (char *) buffer);

		buffer += 512;
		sector++;
	}

	return RES_OK;
}
#endif

/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

#if _USE_IOCTL
DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	return RES_OK;
}
#endif

DWORD get_fattime(void) {
	return 0xFF1FFFFF;
}
