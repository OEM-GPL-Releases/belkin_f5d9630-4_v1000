/************************************************************************/
/*                                                                      */
/*  AMD CFI Enabled Flash Memory Drivers                                */
/*  File name: CFIFLASH.C                                               */
/*  Revision:  1.0  5/07/98                                             */
/*                                                                      */
/* Copyright (c) 1998 ADVANCED MICRO DEVICES, INC. All Rights Reserved. */
/* This software is unpublished and contains the trade secrets and      */
/* confidential proprietary information of AMD. Unless otherwise        */
/* provided in the Software Agreement associated herewith, it is        */
/* licensed in confidence "AS IS" and is not to be reproduced in whole  */
/* or part by any means except for backup. Use, duplication, or         */
/* disclosure by the Government is subject to the restrictions in       */
/* paragraph (b) (3) (B) of the Rights in Technical Data and Computer   */
/* Software clause in DFAR 52.227-7013 (a) (Oct 1988).                  */
/* Software owned by                                                    */
/* Advanced Micro Devices, Inc.,                                        */
/* One AMD Place,                                                       */
/* P.O. Box 3453                                                        */
/* Sunnyvale, CA 94088-3453.                                            */
/************************************************************************/
/*  This software constitutes a basic shell of source code for          */
/*  programming all AMD Flash components. AMD                           */
/*  will not be responsible for misuse or illegal use of this           */
/*  software for devices not supported herein. AMD is providing         */
/*  this source code "AS IS" and will not be responsible for            */
/*  issues arising from incorrect user implementation of the            */
/*  source code herein. It is the user's responsibility to              */
/*  properly design-in this source code.                                */
/*                                                                      */ 
/************************************************************************/                        
#ifdef _CFE_                                                
#include "lib_types.h"
#include "lib_printf.h"
#include "lib_string.h"
#define printk  printf
#else       // linux
#include <linux/param.h>
#include <linux/sched.h>
#include <linux/timer.h>
#endif

#include "cfiflash.h"

static int flash_wait(byte sector, int offset, UINT16 data);
static UINT16 flash_get_device_id(void);
#if SUPPORT_SST
static UINT16 SST_flash_get_device_id(void);
#endif
static int flash_get_cfi(struct cfi_query *query, UINT16 *cfi_struct);
static int flash_write(byte sector, int offset, byte *buf, int nbytes);
static void flash_command(int command, byte sector, int offset, UINT16 data);

#if SUPPORT_INTEL_J3
//INTEL J3 Write to Buffer Implementation
static void flashbuffered_Write(UINT32 address, TMPL_FDATA value);
TMPL_FDATA_PTR  flashbuffered_GetFptr(UINT32 address);
static int FlashBuffered(byte sector, byte *buffer, int numbytes);
#endif

/*********************************************************************/
/* 'meminfo' should be a pointer, but most C compilers will not      */
/* allocate static storage for a pointer without calling             */
/* non-portable functions such as 'new'.  We also want to avoid      */
/* the overhead of passing this pointer for every driver call.       */
/* Systems with limited heap space will need to do this.             */
/*********************************************************************/
struct flashinfo meminfo; /* Flash information structure */
static int flashFamily = FLASH_UNDEFINED;
static int totalSize = 0;
static struct cfi_query query;

static UINT16 cfi_data_struct_29W160[] = {
    0x0020, 0x0049, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0x0051, 0x0052, 0x0059, 0x0002, 0x0000, 0x0040, 0x0000, 0x0000,
    0x0000, 0x0000, 0x0000, 0x0027, 0x0036, 0x0000, 0x0000, 0x0004,
    0x0000, 0x000a, 0x0000, 0x0004, 0x0000, 0x0003, 0x0000, 0x0015,
    0x0002, 0x0000, 0x0000, 0x0000, 0x0004, 0x0000, 0x0000, 0x0040,
    0x0000, 0x0001, 0x0000, 0x0020, 0x0000, 0x0000, 0x0000, 0x0080,
    0x0000, 0x001e, 0x0000, 0x0000, 0x0001, 0xffff, 0xffff, 0xffff,
    0x0050, 0x0052, 0x0049, 0x0031, 0x0030, 0x0000, 0x0002, 0x0001,
    0x0001, 0x0004, 0x0000, 0x0000, 0x0000, 0xffff, 0xffff, 0x0002,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0x0888, 0x252b, 0x8c84, 0x7dbc, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff,
    0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff, 0xffff
};

/*********************************************************************/
/* Init_flash is used to build a sector table from the information   */
/* provided through the CFI query.  This information is translated   */
/* from erase_block information to base:offset information for each  */
/* individual sector. This information is then stored in the meminfo */
/* structure, and used throughout the driver to access sector        */
/* information.                                                      */
/*                                                                   */
/* This is more efficient than deriving the sector base:offset       */
/* information every time the memory map switches (since on the      */
/* development platform can only map 64k at a time).  If the entire  */
/* flash memory array can be mapped in, then the addition static     */
/* allocation for the meminfo structure can be eliminated, but the   */
/* drivers will have to be re-written.                               */
/*                                                                   */
/* The meminfo struct occupies 653 bytes of heap space, depending    */
/* on the value of the define MAXSECTORS.  Adjust to suit            */
/* application                                                       */ 
/*********************************************************************/
byte flash_init(void)
{
    int i=0, j=0, count=0;
    int basecount=0L;
    UINT16 device_id;
    int flipCFIGeometry = FALSE;
#if (SUPPORT_SST||SUPPORT_INTEL_J3)
    int uniformGeometry = FALSE;
#endif
    /* First, assume
    * a single 8k sector for sector 0.  This is to allow
    * the system to perform memory mapping to the device,
    * even though the actual physical layout is unknown.
    * Once mapped in, the CFI query will produce all
    * relevant information.
    */
    meminfo.addr = 0L;
    meminfo.areg = 0;
    meminfo.nsect = 1;
    meminfo.bank1start = 0;
    meminfo.bank2start = 0;
    
    meminfo.sec[0].size = 8192;
    meminfo.sec[0].base = 0x00000;
    meminfo.sec[0].bank = 1;
        
    flash_command(FLASH_RESET, 0, 0, 0);

#if SUPPORT_SST
    /* Try SST first */
    device_id = SST_flash_get_device_id();
    if (device_id == ID_SST39LV160 || device_id == ID_SST39LV3201)
      printk("SST Device id = %x\n", device_id);
    else
      device_id = flash_get_device_id();
#else    
    device_id = flash_get_device_id();
#endif

    switch (device_id) {
#if SUPPORT_SST
        case ID_SST39LV160:
        case ID_SST39LV3201:
            flashFamily = FLASH_SST;
            break;
#endif
        case ID_I28F160C3B:
        case ID_I28F320C3B:
        case ID_I28F160C3T:
        case ID_I28F320C3T:
            flashFamily = FLASH_INTEL;
            break;
#if SUPPORT_INTEL_J3     
        case ID_E28F320J3A:
            flashFamily = FLASH_INTEL_J3;
            break;
#endif        
        case ID_AM29DL800B:
        case ID_AM29LV800B:
        case ID_AM29LV400B:   
        case ID_AM29LV160B:
        case ID_AM29LV320B:
        case ID_MX29LV320AB:
        case ID_AM29LV320MB:
        case ID_AM29DL800T:
        case ID_AM29LV800T:
        case ID_AM29LV160T:
        case ID_AM29LV320T:
        case ID_MX29LV320AT:
        case ID_AM29LV320MT:
            flashFamily = FLASH_AMD;
            break;
        default:
            printk("Flash memory not supported!  Device id = %x\n", device_id);
            return -1;           
    }

    if (flash_get_cfi(&query, 0) == -1) {
        if ((device_id == ID_AM29LV160T) || (device_id == ID_AM29LV160B)) {
            flash_get_cfi(&query, cfi_data_struct_29W160);
        }
        else {
            printk("CFI data structure not found. Device id = %x\n", device_id);
            return -1;           
        }
    }

    count=0;basecount=0L;

    for (i=0; i<query.num_erase_blocks; i++) {
        count += query.erase_block[i].num_sectors;
    }
    
    meminfo.nsect = count;
    count=0;

    // need to determine if it top or bottom boot here
    switch (device_id)
    {
#if (SUPPORT_SST||SUPPORT_INTEL_J3)
        case ID_SST39LV160:
        case ID_SST39LV3201:
        case ID_E28F320J3A:
           uniformGeometry = TRUE;
           break;
#endif
        case ID_AM29DL800B:
        case ID_AM29LV800B:
        case ID_AM29LV400B:   
        case ID_AM29LV160B:
        case ID_AM29LV320B:
        case ID_MX29LV320AB:
        case ID_AM29LV320MB:
        case ID_I28F160C3B:
        case ID_I28F320C3B:
        case ID_I28F160C3T:
        case ID_I28F320C3T:
            flipCFIGeometry = FALSE;
            break;
        case ID_AM29DL800T:
        case ID_AM29LV800T:
        case ID_AM29LV160T:
        case ID_AM29LV320T:
        case ID_MX29LV320AT:
        case ID_AM29LV320MT:
            flipCFIGeometry = TRUE;
            break;
        default:
            printk("Flash memory not supported!  Device id = %x\n", device_id);
            return -1;           
    }

#if (SUPPORT_SST||SUPPORT_INTEL_J3)
    if(uniformGeometry){
      if(device_id == ID_SST39LV160) {
        meminfo.nsect = 32;
        for (count=0; count<meminfo.nsect; count++) {
          meminfo.sec[count].size = 0x00010000;
          meminfo.sec[count].base = (int) basecount;
          basecount += (int) 0x00010000;
        }
      }
      else if (device_id == ID_SST39LV3201){
        meminfo.nsect = 64;
        for (count=0; count<meminfo.nsect; count++){
          meminfo.sec[count].size = 0x00010000;
          meminfo.sec[count].base = (int) basecount;
          basecount += (int) 0x00010000;
        }
      }
      if(device_id == ID_E28F320J3A){
        meminfo.nsect = 32;
        for (count=0; count<meminfo.nsect; count++){
          meminfo.sec[count].size = 0x00020000;
          meminfo.sec[count].base = (int) basecount;
          basecount += (int) 0x00020000;
        }
      }
    }
    else
#endif
    if (!flipCFIGeometry)
    {
       for (i=0; i<query.num_erase_blocks; i++) {
            for(j=0; j<query.erase_block[i].num_sectors; j++) {
                meminfo.sec[count].size = (int) query.erase_block[i].sector_size;
                meminfo.sec[count].base = (int) basecount;
                basecount += (int) query.erase_block[i].sector_size;
                count++;
            }
        }
    }
    else /* TOP BOOT */
    {
        for (i = (query.num_erase_blocks - 1); i >= 0; i--) {
            for(j=0; j<query.erase_block[i].num_sectors; j++) {
                meminfo.sec[count].size = (int) query.erase_block[i].sector_size;
                meminfo.sec[count].base = (int) basecount;
                basecount += (int) query.erase_block[i].sector_size;
                count++;
            }
        }
    }

    totalSize = meminfo.sec[count-1].base + meminfo.sec[count-1].size;

    return (0);
}

/*********************************************************************/
/* Flash_sector_erase_int() is identical to flash_sector_erase(),    */
/* except it will wait until the erase is completed before returning */
/* control to the calling function.  This can be used in cases which */
/* require the program to hold until a sector is erased, without     */
/* adding the wait check external to this function.                  */
/*********************************************************************/
byte flash_sector_erase_int(byte sector)
{
    int i;

    for( i = 0; i < 3; i++ ) {
        flash_command(FLASH_SERASE, sector, 0, 0);
        if (flash_wait(sector, 0, 0xffff) == STATUS_READY)
            break;
    }

    return(1);
}

/*********************************************************************/
/* flash_read_buf() reads buffer of data from the specified          */
/* offset from the sector parameter.                                 */
/*********************************************************************/
int flash_read_buf(byte sector, int offset, 
                        byte *buffer, int numbytes)
{
    byte *fwp;

    fwp = (byte *)flash_get_memptr(sector);

	while (numbytes) {
		*buffer++ = *(fwp + offset);
		numbytes--;
		fwp++;
    }

    return (1);
}

/*********************************************************************/
/* flash_write_buf() utilizes                                        */
/* the unlock bypass mode of the flash device.  This can remove      */
/* significant overhead from the bulk programming operation, and     */
/* when programming bulk data a sizeable performance increase can be */
/* observed.                                                         */
/*********************************************************************/
int flash_write_buf(byte sector, int offset, byte *buffer, int numbytes)
{
    int ret = -1;
    int i;
#if SUPPORT_INTEL_J3
    if (flashFamily == FLASH_INTEL_J3) {
        ret = FlashBuffered( sector,buffer,numbytes);
        if( ret == -1 )
            printk( "Flash J3 write error.\n" );
        return( ret );
    }
#endif


#if SUPPORT_SST
    if (flashFamily == FLASH_SST) {
        ret = flash_write(sector, offset, buffer, numbytes);
        if( ret == -1 )
            printk( "Flash SST write error.\n" );
        return( ret );
    }
#endif
    unsigned char *p = flash_get_memptr(sector) + offset;

    /* After writing the flash block, compare the contents to the source
     * buffer.  Try to write the sector successfully up to three times.
     */
    for( i = 0; i < 3; i++ ) {
        ret = flash_write(sector, offset, buffer, numbytes);
        if( !memcmp( p, buffer, numbytes ) )
            break;
        /* Erase and try again */
        flash_sector_erase_int(sector);
        ret = -1;
    }

    if( ret == -1 )
        printk( "Flash write error.  Verify failed\n" );

    return( ret );
}

/*********************************************************************/
/* Usefull funtion to return the number of sectors in the device.    */
/* Can be used for functions which need to loop among all the        */
/* sectors, or wish to know the number of the last sector.           */
/*********************************************************************/
int flash_get_numsectors(void)
{
    return meminfo.nsect;
}

/*********************************************************************/
/* flash_get_sector_size() is provided for cases in which the size   */
/* of a sector is required by a host application.  The sector size   */
/* (in bytes) is returned in the data location pointed to by the     */
/* 'size' parameter.                                                 */
/*********************************************************************/
int flash_get_sector_size(byte sector)
{
    return meminfo.sec[sector].size;
}

/*********************************************************************/
/* The purpose of flash_get_memptr() is to return a memory pointer   */
/* which points to the beginning of memory space allocated for the   */
/* flash.  All function pointers are then referenced from this       */
/* pointer. 							     */
/*                                                                   */
/* Different systems will implement this in different ways:          */
/* possibilities include:                                            */
/*  - A direct memory pointer                                        */
/*  - A pointer to a memory map                                      */
/*  - A pointer to a hardware port from which the linear             */
/*    address is translated                                          */
/*  - Output of an MMU function / service                            */
/*                                                                   */
/* Also note that this function expects the pointer to a specific    */
/* sector of the device.  This can be provided by dereferencing      */
/* the pointer from a translated offset of the sector from a         */
/* global base pointer (e.g. flashptr = base_pointer + sector_offset)*/
/*                                                                   */
/* Important: Many AMD flash devices need both bank and or sector    */
/* address bits to be correctly set (bank address bits are A18-A16,  */
/* and sector address bits are A18-A12, or A12-A15).  Flash parts    */
/* which do not need these bits will ignore them, so it is safe to   */
/* assume that every part will require these bits to be set.         */
/*********************************************************************/
unsigned char *flash_get_memptr(byte sector)
{
	unsigned char *memptr = (unsigned char*)(FLASH_BASE_ADDR_REG + meminfo.sec[sector].base);

	return (memptr);
}

/*********************************************************************/
/* The purpose of flash_get_blk() is to return a the block number    */
/* for a given memory address.                                       */
/*********************************************************************/
int flash_get_blk(int addr)
{
    int blk_start, i;
    int last_blk = flash_get_numsectors();
    int relative_addr = addr - (int) FLASH_BASE_ADDR_REG;

    for(blk_start=0, i=0; i < relative_addr && blk_start < last_blk; blk_start++)
        i += flash_get_sector_size(blk_start);

    if( i > relative_addr )
    {
        blk_start--;        // last blk, dec by 1
    }
    else
        if( blk_start == last_blk )
        {
            printk("Address is too big.\n");
            blk_start = -1;
        }

    return( blk_start );
}

/************************************************************************/
/* The purpose of flash_get_total_size() is to return the total size of */
/* the flash                                                            */
/************************************************************************/
int flash_get_total_size()
{
    return totalSize;
}

/*********************************************************************/
/* Flash_command() is the main driver function.  It performs         */
/* every possible command available to AMD B revision                */
/* flash parts. Note that this command is not used directly, but     */
/* rather called through the API wrapper functions provided below.   */
/*********************************************************************/
static void flash_command(int command, byte sector, int offset, UINT16 data)
{
    volatile UINT16 *flashptr;

    flashptr = (UINT16 *) flash_get_memptr(sector);
    
    switch (flashFamily) {
    case FLASH_UNDEFINED:
        /* These commands should work for AMD and Intel flashes */
        switch (command) {
        case FLASH_RESET:
            flashptr[0] = 0xF0;
            flashptr[0] = 0xFF;
            break;
        case FLASH_READ_ID:
            flashptr[0x555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AA] = 0x55;       /* unlock 2 */
            flashptr[0x555] = 0x90;
            break;
        case FLASH_CFIQUERY:
            flashptr[0x55] = 0x98;
            break;
        default:
            break;
        }
        break;
#if SUPPORT_SST
    case FLASH_SST:
        switch (command) {
        case FLASH_RESET:
            flashptr[0] = 0xF0;
            break;
        case FLASH_READ_ID:
            flashptr[0x5555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AAA] = 0x55;       /* unlock 2 */
            flashptr[0x5555] = 0x90;
            break;
        case FLASH_CFIQUERY:
            flashptr[0x5555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AAA] = 0x55;       /* unlock 2 */
            flashptr[0x5555] = 0x98;
            break;
        case FLASH_PROG:
            flashptr[0x5555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AAA] = 0x55;       /* unlock 2 */
            flashptr[0x5555] = 0xA0;
            flashptr[offset/2] = data;
            break;
        case FLASH_SERASE:
            flashptr[0x5555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AAA] = 0x55;       /* unlock 2 */
            flashptr[0x5555] = 0x80;
            flashptr[0x5555] = 0xAA;
            flashptr[0x2AAA] = 0x55;
            flashptr[0] = 0x50;
            break;
        default:
            break;
        }
        break;
#endif
    case FLASH_AMD:
        switch (command) {
        case FLASH_RESET:
            flashptr[0] = 0xF0;
            break;
        case FLASH_READ_ID:
            flashptr[0x555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AA] = 0x55;       /* unlock 2 */
            flashptr[0x555] = 0x90;
            break;
        case FLASH_CFIQUERY:
            flashptr[0x55] = 0x98;
            break;
        case FLASH_UB:
            flashptr[0x555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AA] = 0x55;       /* unlock 2 */
            flashptr[0x555] = 0x20;
            break;
        case FLASH_PROG:
            flashptr[0] = 0xA0;
            flashptr[offset/2] = data;
            break;
        case FLASH_UBRESET:
            flashptr[0] = 0x90;
            flashptr[0] = 0x00;
            break;
        case FLASH_SERASE:
            flashptr[0x555] = 0xAA;       /* unlock 1 */
            flashptr[0x2AA] = 0x55;       /* unlock 2 */
            flashptr[0x555] = 0x80;
            flashptr[0x555] = 0xAA;
            flashptr[0x2AA] = 0x55;
            flashptr[0] = 0x30;
            break;
        default:
            break;
        }
        break;
    case FLASH_INTEL:
        switch (command) {
        case FLASH_RESET:
            flashptr[0] = 0xFF;
            break;
        case FLASH_READ_ID:
            flashptr[0] = 0x90;
            break;
        case FLASH_CFIQUERY:
            flashptr[0] = 0x98;
            break;
        case FLASH_PROG:
            flashptr[0] = 0x40;
            flashptr[offset/2] = data;
            break;
        case FLASH_SERASE:
            flashptr[0] = 0x60;
            flashptr[0] = 0xD0;
            flashptr[0] = 0x20;
            flashptr[0] = 0xD0;
            break;
        default:
            break;
        }
        break;
#if SUPPORT_INTEL_J3
    case FLASH_INTEL_J3:
        switch (command) {
        case FLASH_RESET:
            flashptr[0] = 0xFF;
            break;
        case FLASH_READ_ID:
            flashptr[0] = 0x90;
            break;
        case FLASH_CFIQUERY:
            flashptr[0] = 0x98;
            break;
        case FLASH_PROG:
            flashptr[0] = 0x40;
            flashptr[offset/2] = data;
            break;
        case FLASH_SERASE:
            flashptr[0] = 0x20;//Block Erase
            flashptr[0] = 0xD0;//Erase Confirm
            break;
        default:
            break;
        }
        break;
#endif
    default:
        break;
    }
}

/*********************************************************************/
/* flash_write extends the functionality of flash_program() by       */
/* providing an faster way to program multiple data words, without   */
/* needing the function overhead of looping algorithms which         */
/* program word by word.  This function utilizes fast pointers       */
/* to quickly loop through bulk data.                                */
/*********************************************************************/
static int flash_write(byte sector, int offset, byte *buf, int nbytes)
{
    UINT16 *src;
    src = (UINT16 *)buf;

    if ((nbytes | offset) & 1) {
        return -1;
    }

    flash_command(FLASH_UB, 0, 0, 0);
    while (nbytes > 0) {
        flash_command(FLASH_PROG, sector, offset, *src);
        if (flash_wait(sector, offset, *src) != STATUS_READY)
            break;
        offset +=2;
        nbytes -=2;
        src++;
    }
    flash_command(FLASH_UBRESET, 0, 0, 0);
    
    return (byte*)src - buf;
}

/*********************************************************************/
/* flash_wait utilizes the DQ6, DQ5, and DQ2 polling algorithms      */
/* described in the flash data book.  It can quickly ascertain the   */
/* operational status of the flash device, and return an             */
/* appropriate status code (defined in flash.h)                      */
/*********************************************************************/
static int flash_wait(byte sector, int offset, UINT16 data)
{
    volatile UINT16 *flashptr; /* flash window */
    UINT16 d1;
#if SUPPORT_SST
    UINT16 d2;
#endif

    flashptr = (UINT16 *) flash_get_memptr(sector);
#if SUPPORT_SST
    if (flashFamily == FLASH_AMD || flashFamily == FLASH_SST) {
#else
    if (flashFamily == FLASH_AMD) {
#endif
#if defined(_BCM96338_) || defined(CONFIG_BCM96338)
        do {
            d1 = flashptr[offset/2];
            if (d1 == data)
                return STATUS_READY;
        } while (!(d1 & 0x20));

        d1 = flashptr[offset/2];

        if (d1 != data) {
            flash_command(FLASH_RESET, 0, 0, 0);
            return STATUS_TIMEOUT;
        }
#else

#if SUPPORT_SST
       do {
            d1 = *flashptr;        /* read data */
            d2 = d1 ^ *flashptr;   /* read it again and see what toggled */
            if (d2 == 0)           /* no toggles, nothing's happening */
                return STATUS_READY;
        } while (!(d2 & 0x20));

        d1 = *flashptr;        /* read data */
        d2 = d1 ^ *flashptr;   /* read it again and see what toggled */

        if (d2 != 0) {
            flash_command(FLASH_RESET, 0, 0, 0);
            return STATUS_TIMEOUT;
        }
#else
        do {
            d1 = *flashptr;    /* read data */
            d1 ^= *flashptr;   /* read it again and see what toggled */
            if (d1 == 0)       /* no toggles, nothing's happening */
                return STATUS_READY;
        } while (!(d1 & 0x20));

        d1 = *flashptr;        /* read data */
        d1 ^= *flashptr;   /* read it again and see what toggled */

        if (d1 != 0) {
            flash_command(FLASH_RESET, 0, 0, 0);
            return STATUS_TIMEOUT;
        }
#endif // SUPPORT_SST

#endif //defined(_BCM96338_) || defined(CONFIG_BCM96338)

#if SUPPORT_INTEL_J3
    } else if ((flashFamily == FLASH_INTEL) || (flashFamily == FLASH_INTEL_J3)) {
#else
    } else if (flashFamily == FLASH_INTEL) {
#endif
        flashptr[0] = 0x70;
        /* Wait for completion */
        while(!(*flashptr & 0x80));
        if (*flashptr & 0x30) {
            flashptr[0] = 0x50;
            flash_command(FLASH_RESET, 0, 0, 0);
            return STATUS_TIMEOUT;
        }
        flashptr[0] = 0x50;
        flash_command(FLASH_RESET, 0, 0, 0);
    }
    
    return STATUS_READY;
}


/*********************************************************************/
/* flash_get_device_id() will perform an autoselect sequence on the  */
/* flash device, and return the device id of the component.          */
/* This function automatically resets to read mode.                  */
/*********************************************************************/
static UINT16 flash_get_device_id()
{
    volatile UINT16 *fwp; /* flash window */
    UINT16 answer;
    
    fwp = (UINT16 *)flash_get_memptr(0);
    
    flash_command(FLASH_READ_ID, 0, 0, 0);
    answer = *(fwp + 1);
    if (answer == ID_AM29LV320M) {
        answer = *(fwp + 0xe);
        answer = *(fwp + 0xf);
    }
    
    flash_command(FLASH_RESET, 0, 0, 0);
    return( (UINT16) answer );
}


#if SUPPORT_SST
static UINT16 SST_flash_get_device_id()
{
    volatile UINT16 *fwp; /* flash window */
    UINT16 answer;

    fwp = (UINT16 *)flash_get_memptr(0);

    fwp[0x5555] = 0xAA;       /* unlock 1 */
    fwp[0x2AAA] = 0x55;       /* unlock 2 */
    fwp[0x5555] = 0x90;

    answer = *(fwp + 1);

    flash_command(FLASH_RESET, 0, 0, 0);
    return( (UINT16) answer );
}
#endif

/*********************************************************************/
/* flash_get_cfi() is the main CFI workhorse function.  Due to it's  */
/* complexity and size it need only be called once upon              */
/* initializing the flash system.  Once it is called, all operations */
/* are performed by looking at the meminfo structure.                */
/* All possible care was made to make this algorithm as efficient as */
/* possible.  90% of all operations are memory reads, and all        */
/* calculations are done using bit-shifts when possible              */
/*********************************************************************/
static int flash_get_cfi(struct cfi_query *query, UINT16 *cfi_struct)
{
#if (SUPPORT_SST||SUPPORT_INTEL_J3)
    volatile UINT16 *fwp; /* flash window */
    int volts=0, milli=0, temp=0, i=0;
    int offset=0;

    flash_command(FLASH_CFIQUERY, 0, 0, 0);

    if (cfi_struct == 0)
        fwp = (UINT16 *)flash_get_memptr(0);
    else
        fwp = cfi_struct;

    /* Initial house-cleaning */

    for(i=0; i < 8; i++) {
        query->erase_block[i].sector_size = 0;
        query->erase_block[i].num_sectors = 0;
    }

    query->query_string[0] = fwp[0x10];
    query->query_string[1] = fwp[0x11];
    query->query_string[2] = fwp[0x12];
    query->query_string[3] = '\0';

    /* If not 'QRY', then we dont have a CFI enabled device in the
    socket */

    if( query->query_string[0] != 'Q' &&
        query->query_string[1] != 'R' &&
        query->query_string[2] != 'Y') {
        flash_command(FLASH_RESET, 0, 0, 0);
        return(-1);
    }

    query->oem_command_set       = fwp[0x13];
    query->primary_table_address = fwp[0x15]; /* Important one! */
    query->alt_command_set       = fwp[0x17];
    query->alt_table_address     = fwp[0x19];

    /* We will do some bit translation to give the following values
    numerical meaning in terms of C 'float' numbers */

    volts = ((fwp[0x1B] & 0xF0) >> 4);
    milli = (fwp[0x1B] & 0x0F);
    query->vcc_min = volts * 10 + milli;

    volts = ((fwp[0x1C] & 0xF0) >> 4);
    milli = (fwp[0x1C] & 0x0F);
    query->vcc_max = volts * 10 + milli;

    volts = ((fwp[0x1D] & 0xF0) >> 4);
    milli = (fwp[0x1D] & 0x0F);
    query->vpp_min = volts * 10 + milli;

    volts = ((fwp[0x1E] & 0xF0) >> 4);
    milli = (fwp[0x1E] & 0x0F);
    query->vpp_max = volts * 10 + milli;

    /* Let's not drag in the libm library to calculate powers
    for something as simple as 2^(power)
    Use a bit shift instead - it's faster */

    temp = fwp[0x1F];
    query->timeout_single_write = (1 << temp);

    temp = fwp[0x20];
    if (temp != 0x00)
        query->timeout_buffer_write = (1 << temp);
    else
        query->timeout_buffer_write = 0x00;

    temp = 0;
    temp = fwp[0x21];
    query->timeout_block_erase = (1 << temp);

    temp = fwp[0x22];
    if (temp != 0x00)
        query->timeout_chip_erase = (1 << temp);
    else
        query->timeout_chip_erase = 0x00;

    temp = fwp[0x23];
    query->max_timeout_single_write = (1 << temp) *
        query->timeout_single_write;

    temp = fwp[0x24];
    if (temp != 0x00)
        query->max_timeout_buffer_write = (1 << temp) *
        query->timeout_buffer_write;
    else
        query->max_timeout_buffer_write = 0x00;

    temp = fwp[0x25];
    query->max_timeout_block_erase = (1 << temp) *
        query->timeout_block_erase;

    temp = fwp[0x26];
    if (temp != 0x00)
        query->max_timeout_chip_erase = (1 << temp) *
        query->timeout_chip_erase;
    else
        query->max_timeout_chip_erase = 0x00;

    temp = fwp[0x27];
    query->device_size = (int) (((int)1) << temp);

    query->interface_description = fwp[0x28];

    temp = fwp[0x2A];
    if (temp != 0x00)
        query->max_multi_byte_write = (1 << temp);
    else
        query->max_multi_byte_write = 0;

    query->num_erase_blocks = fwp[0x2C];

    for(i=0; i < query->num_erase_blocks; i++) {
        query->erase_block[i].num_sectors = fwp[(0x2D+(4*i))];
        query->erase_block[i].num_sectors++;

        query->erase_block[i].sector_size = (int) 256 *
		( (int)256 * fwp[(0x30+(4*i))] + fwp[(0x2F+(4*i))] );
    }

    /* Store primary table offset in variable for clarity */
    offset = query->primary_table_address;

    query->primary_extended_query[0] = fwp[(offset)];
    query->primary_extended_query[1] = fwp[(offset + 1)];
    query->primary_extended_query[2] = fwp[(offset + 2)];
    query->primary_extended_query[3] = '\0';

    if( query->primary_extended_query[0] != 'P' &&
        query->primary_extended_query[1] != 'R' &&
        query->primary_extended_query[2] != 'I') {
        flash_command(FLASH_RESET, 0, 0, 0);
        return(2);
    }

    query->major_version = fwp[(offset + 3)];
    query->minor_version = fwp[(offset + 4)];

    query->sensitive_unlock      = (byte) (fwp[(offset+5)] & 0x0F);
    query->erase_suspend         = (byte) (fwp[(offset+6)] & 0x0F);
    query->sector_protect        = (byte) (fwp[(offset+7)] & 0x0F);
    query->sector_temp_unprotect = (byte) (fwp[(offset+8)] & 0x0F);
    query->protect_scheme        = (byte) (fwp[(offset+9)] & 0x0F);
    query->is_simultaneous       = (byte) (fwp[(offset+10)] & 0x0F);
    query->is_burst              = (byte) (fwp[(offset+11)] & 0x0F);
    query->is_page               = (byte) (fwp[(offset+12)] & 0x0F);

#else
    volatile UINT16 *fwp; /* flash window */
    int i=0;
    
    flash_command(FLASH_CFIQUERY, 0, 0, 0);
    
    if (cfi_struct == 0)
        fwp = (UINT16 *)flash_get_memptr(0);
    else
        fwp = cfi_struct;
    
    /* Initial house-cleaning */
    for(i=0; i < 8; i++) {
        query->erase_block[i].sector_size = 0;
        query->erase_block[i].num_sectors = 0;
    }
    
    /* If not 'QRY', then we dont have a CFI enabled device in the socket */
    if( fwp[0x10] != 'Q' &&
        fwp[0x11] != 'R' &&
        fwp[0x12] != 'Y') {
        flash_command(FLASH_RESET, 0, 0, 0);
        return(-1);
    }
    
    query->num_erase_blocks = fwp[0x2C];
    
    for(i=0; i < query->num_erase_blocks; i++) {
        query->erase_block[i].num_sectors = fwp[(0x2D+(4*i))];
        query->erase_block[i].num_sectors++;
        query->erase_block[i].sector_size = 256 * (256 * fwp[(0x30+(4*i))] + fwp[(0x2F+(4*i))]);
    }
#endif    
    flash_command(FLASH_RESET, 0, 0, 0);
    return(1);
}

#if SUPPORT_INTEL_J3
static void flashbuffered_Write(UINT32  address, TMPL_FDATA value )
{

   TMPL_FDATA_PTR  fptr;

   fptr = flashbuffered_GetFptr(address);

   *fptr = value;

}
#endif

#if SUPPORT_INTEL_J3
TMPL_FDATA_PTR  flashbuffered_GetFptr ( UINT32 address )
{
   return( (TMPL_FDATA_PTR)address );
}
#endif

#if SUPPORT_INTEL_J3
/****************************************************************************
 *
 * TMPL_ProgramFlashBuffered
 *
 * Description:
 *
 *    This procedure is called to program the flash device at the specified
 *    starting address contiguously with the specified buffer data.  See
 *    the flash device datasheet for specific details on the write to buffer
 *    command.
 ***************************************************************************/
static int FlashBuffered(byte sector, byte *buffer, int numbytes)
{
   TMPL_FDATA_PTR fptr;
   TMPL_FDATA     writedata;
   UINT16         numitems;
   UINT32         cmndaddress;
   UINT32         numwritebytes;
   UINT32         byteswritten;
   int            sect_size;

   unsigned int address = (unsigned int) flash_get_memptr(sector);
   sect_size = flash_get_sector_size(sector);
   cmndaddress = (UINT32)address;


   //printk("Entering FlashBuffered *** \n");

   if ( cmndaddress & 0x01 )
      cmndaddress --;

   /* if (start address is not TMPL_BUFFER_SIZE-byte aligned ) */
   if ( ( address % TMPL_BUFFER_SIZE ) != 0 )
   {
      /* if ( buffer size is > TMPL_BUFFER_SIZE ) or ( buffer crosses block boundary ) */
      if ( ( numbytes > TMPL_BUFFER_SIZE ) ||
         ( ( address & 0x1 ) && ( numbytes >= TMPL_BUFFER_SIZE ) ) ||
         ( ( address + numbytes -1 ) > ( address | TMPL_BLOCK_MASK) )
         ){
	    /* write partial buffer */
	    numwritebytes = ( TMPL_BUFFER_SIZE - ( address % TMPL_BUFFER_SIZE ) );
      }
	  else{
	    /* write all remaining bytes */
		numwritebytes = numbytes;
	  }

      byteswritten = numwritebytes;

	  flashbuffered_Write( cmndaddress, TMPL_WRITE_TO_BUFFER );

	  numitems = numwritebytes / sizeof(TMPL_FDATA);

	  if ( ( ( numwritebytes % sizeof(TMPL_FDATA) ) != 0 ) ||
		   ( ( numwritebytes > 0x01 ) && ( address & 0x01 ) ) )
	  {
	     numitems++;
	  }

	  flashbuffered_Write( cmndaddress, numitems-1 );

	  if ( numwritebytes > 0 ) /* while more data to write */
	  {
		while ( numwritebytes > 0 ) /* if more bytes still to write */
		{
		   if ( ( address & 0x1 ) != 0 ) /* if destination address is odd */
		   {
        		address--;
				writedata = (TMPL_FDATA) *buffer;
				writedata |= 0xff00;
				numwritebytes--;
				buffer++;
			}
			else /* destination address is even */
			{
		        /* grab first byte */
			    writedata = (TMPL_FDATA)( *buffer );
			    writedata = ( ( writedata << 8 ) & 0xff00 );
			    /* grab second byte */
			    writedata = writedata | ( (TMPL_FDATA) *(buffer+1) );
				if ( numwritebytes == 1 )
				{
			     writedata |= 0x00ff;
				 numwritebytes--;
				}
				else
				{
				 numwritebytes -= sizeof(TMPL_FDATA);
				 }

				 buffer += sizeof(TMPL_FDATA);
			}

			fptr = flashbuffered_GetFptr(address);
			*fptr = writedata;
			address += sizeof(TMPL_FDATA);
		 	}
		}

		flashbuffered_Write( cmndaddress, TMPL_CONFIRM );

        if(flash_wait(sector,0,0) != STATUS_READY)
             return -1;

     numbytes -= byteswritten;

   } /* end if  ( ( address % TMPL_BUFFER_SIZE )  != 0 ) ) */

      /* while bytes remain */
      while ( numbytes != 0 )
	  {
         /* if TMPL_BUFFER_SIZE bytes remain */
         if ( numbytes > TMPL_BUFFER_SIZE )
		 {
		    /* write full TMPL_BUFFER_SIZE-byte buffer */
		    numwritebytes = TMPL_BUFFER_SIZE;
		 }
		 /* else */
		 else
		 {
		    /* write partial buffer */
		    numwritebytes = numbytes;
         }
         /* end if */

         byteswritten = numwritebytes;

         cmndaddress = address;

		 flashbuffered_Write( cmndaddress, TMPL_WRITE_TO_BUFFER );

		 numitems = numwritebytes / sizeof(TMPL_FDATA);

		 if ( ( numwritebytes % sizeof(TMPL_FDATA) ) != 0 )
		 {
	        numitems++;
		 }

		 flashbuffered_Write( cmndaddress, numitems-1 );

		 if ( numwritebytes > 0 ) /* while more data to write */
		 {
		    while ( numwritebytes > 0 ) /* if more bytes still to write */
			{ /* address is known even at this point */
		       /* grab first byte */
			   writedata = (TMPL_FDATA)( *buffer );
			   writedata = ( ( writedata << 8 ) & 0xff00 );
			   /* grab second byte */
			   writedata = writedata | ( (TMPL_FDATA) *(buffer+1) );

			   if ( numwritebytes == 1 )
			   {
			      writedata |= 0x00ff;
				  numwritebytes--;
			   }
			   else
			   {
			      numwritebytes -= sizeof(TMPL_FDATA);
			   }

			   buffer += sizeof(TMPL_FDATA);

			   fptr = flashbuffered_GetFptr(address);
			   *fptr = writedata;
			   address += sizeof(TMPL_FDATA);
		    }
		 }

		 flashbuffered_Write( cmndaddress, TMPL_CONFIRM );

        if(flash_wait(sector,0,0) != STATUS_READY)
             return -1;

         numbytes -= byteswritten;

      } /* end while numbytes != 0 ) */

   //printk("Leaving FlashBuffered *** \n");

   /* return device to read array mode */
   flash_command(FLASH_RESET, 0, 0, 0);

   return( sect_size );
}
#endif


