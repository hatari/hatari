/*
  Hatari

  Load TOS image file into ST memory, fix/setup for emulator

  The Atari ST TOS needs to be patched to help with emulation. Eg, it references the MMU chip
  to set memory size. This is patched to the sizes we need without the complicated emulation
  of hardware which is not needed(as yet). We also patch DMA devices and Hard Drives.
  NOTE: TOS versions 1.06 and 1.62 were not designed for use on a real STfm. These were for the
  STe machine ONLY. They access the DMA/Microwire addresses on boot-up which(correctly) cause a
  bus-error on Hatari as they would in a real STfm. If a user tries to select any of these images
  we bring up an error.
*/

#include <SDL_types.h>

#include "main.h"
#include "cart.h"
#include "debug.h"
#include "decode.h"
#include "dialog.h"
#include "errlog.h"
#include "file.h"
#include "floppy.h"
#include "gemdos.h"
#include "hdc.h"
#include "m68000.h"
#include "memAlloc.h"
#include "memorySnapShot.h"
#include "stMemory.h"
#include "tos.h"
#include "vdi.h"


/* Settings for differnt memory sizes */
static MEMORY_INFO MemoryInfo[] = {
  { 0x80000,0x0000,0x00080000 },     /* MEMORYSIZE_512 */
  { 0x100000,0x0101,0x00100000 },    /* MEMORYSIZE_1024 */
  { 0x200000,0x0001,0x00200000 },    /* MEMORYSIZE_2MB */
  { 0x400000,0x1010,0x00400000 }     /* MEMORYSIZE_4MB */
};

/* Bit masks of connected drives(we support up to C,D,E,F,G,H) */
unsigned int ConnectedDriveMaskList[] = {
  0x03,  /* DRIVELIST_NONE  A,B         */
  0x07,  /* DRIVELIST_C    A,B,C       */
  0x0F,  /* DRIVELIST_CD    A,B,C,D     */
  0x1F,  /* DRIVELIST_CDE  A,B,C,D,E   */
  0x3F,  /* DRIVELIST_CDEF  A,B,C,D,E,F */
  0x7F,  /* DRIVELIST_CDEFG  A,B,C,D,E,F,G */
  0xFF,  /* DRIVELIST_CDEFGH  A,B,C,D,E,F,G,H */
};

unsigned short int TOSVersion;          /* eg, 0x0100, 0x0102 */
unsigned long TOSAddress, TOSSize;      /* Address in ST memory and size of TOS image */
unsigned int ConnectedDriveMask=0x03;   /* Bit mask of connected drives, eg 0x7 is A,B,C */

/* Possible TOS file extensions to scan for */
char *pszTOSNameExts[] = {
  ".img",
  ".rom",
  ".tos",
  NULL
};

/* Taken from Decode.asm - Thothy */
unsigned long STRamEnd;                 /* End of ST Ram, above this address is no-mans-land and hardware vectors */
unsigned long STRamEnd_BusErr;          /* as above, but start of BUS error exception */



/*-----------------------------------------------------------------------*/
/*
  Save/Restore snapshot of local variables('MemorySnapShot_Store' handles type)
*/
void TOS_MemorySnapShot_Capture(BOOL bSave)
{
  /* Save/Restore details */
  MemorySnapShot_Store(&TOSVersion,sizeof(TOSVersion));
  MemorySnapShot_Store(&TOSAddress,sizeof(TOSAddress));
  MemorySnapShot_Store(&TOSSize,sizeof(TOSSize));
  MemorySnapShot_Store(&ConnectedDriveMask,sizeof(ConnectedDriveMask));
}


/*-----------------------------------------------------------------------*/
/*
  Load TOS Rom image file into ST memory space and fix image so can emulate correctly
  Pre TOS 1.06 are loaded at 0xFC0000 with later ones at 0xE00000
  If we cannot find the TOS image, or we detect an error we default to the built-in
  TOS 1.00 image. This works great for new users who do not understand the idea of a TOS
  Rom and are confused when presented with a 'select TOS image' dialog.
*/
void TOS_LoadImage(void)
{
  void *pTOSFile = NULL;
  BOOL bTOSImageLoaded = FALSE;

  /* Load TOS image into memory so we can check it's vesion */
  TOSVersion = 0;
  pTOSFile = File_Read(ConfigureParams.TOSGEM.szTOSImageFileName,NULL,NULL,pszTOSNameExts);

  if (pTOSFile)
  {
    bTOSImageLoaded = TRUE;
    /* Now, look at start of image to find Version number and address */
    TOSVersion = STMemory_Swap68000Int( *(Uint16 *)((Uint32)pTOSFile+2) );
    TOSAddress = STMemory_Swap68000Long( *(Uint32 *)((Uint32)pTOSFile+8) );

    TOSSize = File_Length(ConfigureParams.TOSGEM.szTOSImageFileName);
    if( TOSSize<=0 )  bTOSImageLoaded = FALSE;

    /* TOSes 1.06 and 1.62 are for the STe ONLY and so don't run on a real STfm. */
    /* They access illegal memory addresses which don't exist on a real machine and cause the OS */
    /* to lock up. So, if user selects one of these, show an error */
    if( TOSVersion==0x0106 || TOSVersion==0x0162 )
    {
      Main_Message("TOS versions 1.06 and 1.62 are NOT valid STfm images.\n\n"
                   "These were only designed for use on the STe range of machines.\n", PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
      bTOSImageLoaded = FALSE;
    }

    /* Copy loaded image into ST memory, if found valid one*/
    if (bTOSImageLoaded)
      memcpy((void *)((unsigned long)STRam+TOSAddress),pTOSFile,TOSSize);
  }

  /* Are we allowed VDI under this TOS? */
  if ( (TOSVersion<0x0104) && (bUseVDIRes) )
  {
    /* Warn user (exit if need to) */
    Main_Message("To use GEM Extended resolutions, you must select TOS 1.04 or higher.",PROG_NAME /*,MB_OK|MB_ICONINFORMATION*/);
    /* And select non VDI */
    bUseVDIRes = ConfigureParams.TOSGEM.bUseExtGEMResolutions = FALSE;
  }

  /* Did we load a TOS image correctly? */
  if (!bTOSImageLoaded)
  {
    fprintf(stderr, "Error: No tos.img loaded!\n");
    exit(-1);
  }

  /* Fix TOS image, modify code for emulation */
  TOS_FixRom();

  /* Set connected devices, memory configuration */
  TOS_SetDefaultMemoryConfig();

  /* and free loaded image */
  Memory_Free(pTOSFile);
}


/*-----------------------------------------------------------------------*/
/*
  Patch TOS to skip some TOS setup code which we don't support/need.
  As TOS Roms need to be modified we can only run images which are entered here.

  So, how do we find these addresses when we have no commented source code?
  Easy,
    Hdv_init: Scan start of TOS for table of move.l <addr>,$46A(a5), around 0x224 bytes in
      and look at the first entry - that's the hdv_init address.
    Hdv_boot: Scan start of TOS for table of move.l <addr>,$47A(a5), and look for 5th entry
      - that's the hdv_boot address. The function starts with link,movem,jsr.
    Boot from DMA bus: again scan at start of rom for tst.w $482, boot call will be just above it.
    Set connected drives: search for 'clr.w' and '$4c2' to find, may use (a5) in which case op-code
     is only 4 bytes and also note this is only do on TOS's after 1.00
*/
void TOS_FixRom(void)
{
  switch(TOSVersion)
  {
    /*
      TOS 1.00 settings
    */
    case 0x0100:
      /* hdv_init, initialize drives */
      STMemory_WriteWord(0xFC0D60,RTS_OPCODE);    /* RTS */

      /* FC1384  JSR $FC0AF8  hdv_boot, load boot sector */
      STMemory_WriteWord(0xFC1384,NOP_OPCODE);    /* NOP */
      STMemory_WriteWord(0xFC1384+2,NOP_OPCODE);  /* NOP */
      STMemory_WriteWord(0xFC1384+4,NOP_OPCODE);  /* NOP */

      /* FC03d6  JSR $FC04A8  Boot from DMA bus */
      /* This doesn't need to be patched - but we do
         need to force set condrv ($4c2), because the
         ACSI driver (if any) will reset it, this is 
         done after the DMA bus boot (when the driver loads), 
         replacing the RTS with our own
         routine which sets condrv and then RTSes. */
      if(ACSI_EMU_ON || GEMDOS_EMU_ON) 
      {
        STMemory_WriteWord(0xFC04d4, CONDRV_OPCODE);
      }
      else 
      {
        STMemory_WriteWord(0xFC03D6,NOP_OPCODE);    /* NOP */
        STMemory_WriteWord(0xFC03D6+2,NOP_OPCODE);  /* NOP */ 
      }

      /* Timer D(MFP init 0xFC21B4), set value before call Set Timer routine */
      STMemory_WriteWord(0xFC21F6,TIMERD_OPCODE);

      /* Modify assembler loaded into cartridge area */
      Cart_WriteHdvAddress(0x167A);
      break;

    /*
      TOS 1.02 settings
    */
    case 0x0102:
      /* hdv_init, initialize drives */
      if( STMemory_ReadLong(0xFC0F44)==0x4e56fff0L )  /* Check if we can patch this TOS */
        STMemory_WriteWord(0xFC0F44, RTS_OPCODE);     /* RTS */

      /* FC1568  JSR $FC0C2E  hdv_boot, load boot sector */
      if( STMemory_ReadLong(0xFC1568)==0x4eb900fcL )
      {
        STMemory_WriteWord(0xFC1568, NOP_OPCODE);     /* NOP */
        STMemory_WriteWord(0xFC1568+2, NOP_OPCODE);   /* NOP */
        STMemory_WriteWord(0xFC1568+4, NOP_OPCODE);   /* NOP */
      }

      /* see comments above -Sven */
      if(ACSI_EMU_ON || GEMDOS_EMU_ON)
      {
        STMemory_WriteWord(0xFC0584, CONDRV_OPCODE);
      }
      else 
      {
        /* FC0302  CLR.L $4C2  Set connected drives */
        if( STMemory_ReadLong(0xFC0302)==0x42b90000L )
        {
          STMemory_WriteWord(0xFC0302, NOP_OPCODE);
          STMemory_WriteWord(0xFC0302+2, NOP_OPCODE);   /* NOP */
          STMemory_WriteWord(0xFC0302+4, NOP_OPCODE);   /* NOP */
        }
	  
        /* FC0472  BSR.W $FC0558  Boot from DMA bus */
        if( STMemory_ReadLong(0xFC0472)==0x610000e4L )
        {
          STMemory_WriteWord(0xFC0472, NOP_OPCODE);     /* NOP */
          STMemory_WriteWord(0xFC0472+2, NOP_OPCODE);   /* NOP */
        }
      }

      /* Timer D (MFP init 0xFC2408) */
      if( STMemory_ReadLong(0xFC2450)==0x74026100 )
        STMemory_WriteWord(0xFC2450, TIMERD_OPCODE);

      /* Modify assembler loaded into cartridge area */
      Cart_WriteHdvAddress(0x16DA);
      break;

    /*
      TOS 1.04 settings
    */
    case 0x0104:
      /* hdv_init, initialize drives */
      STMemory_WriteWord(0xFC16BA,RTS_OPCODE);      /* RTS */

      /* FC1CCE  JSR $FC0BD8  hdv_boot, load boot sector */
      STMemory_WriteWord(0xFC1CCE,NOP_OPCODE);      /* NOP */
      STMemory_WriteWord(0xFC1CCE + 2,NOP_OPCODE);  /* NOP */
      STMemory_WriteWord(0xFC1CCE + 4,NOP_OPCODE);  /* NOP */

      /* FC0466  BSR.W $FC054C  Boot from DMA bus */
      /* see comment above -Sven */
      if(ACSI_EMU_ON || GEMDOS_EMU_ON)
      {
        STMemory_WriteWord(0xFC0576, CONDRV_OPCODE);
      }
      else 
      {
        /* FC02E6  CLR.L $4C2(A5)  Set connected drives */
        STMemory_WriteWord(0xFC02E6,NOP_OPCODE);
        STMemory_WriteWord(0xFC02E6+2,NOP_OPCODE);    /* NOP */
	  
        STMemory_WriteWord(0xFC0466,NOP_OPCODE);    /* NOP */
        STMemory_WriteWord(0xFC0466+2,NOP_OPCODE);  /* NOP */ 
      }

      /* Timer D(MFP init 0xFC34FC) */
      STMemory_WriteWord(0xFC3544,TIMERD_OPCODE);

      /* Modify assembler loaded into cartridge area */
      Cart_WriteHdvAddress(0x181C);
      break;

    /*
      TOS 1.06 settings
    */
//    case 0x0106:
//      // hdv_init, initialize drives
//      STMemory_WriteWord(0xE01892,RTS_OPCODE);  //RTS
//
//      // E01EA6  JSR $E00D74      hdv_boot, load boot sector
//      STMemory_WriteWord(0xE01EA6,NOP_OPCODE);  //NOP
//      STMemory_WriteWord(0xE01EA6+2,NOP_OPCODE);  //NOP
//      STMemory_WriteWord(0xE01EA6+4,NOP_OPCODE);  //NOP
//
//      // E00576  BSR.W $E0065C    Boot from DMA bus
//      if (bUseVDIRes) {
//        STMemory_WriteWord(0xE00576,0xa000);  //Init Line-A
//        STMemory_WriteWord(0xE00576+2,0xa0ff);  //Trap Line-A(to get structure)
//      }
//      else {
//        STMemory_WriteWord(0xE00576,NOP_OPCODE);  //NOP
//        STMemory_WriteWord(0xE00576+2,NOP_OPCODE);  //NOP
//      }
//
//      // E002DC  CLR.L $4C2(A5)    Set connected drives
//      STMemory_WriteWord(0xE002DC,CONDRV_OPCODE);
//      STMemory_WriteWord(0xE002DC+2,NOP_OPCODE);  //NOP
//
//      // Timer D(MFP init 0xE036BC)
//      STMemory_WriteWord(0xE03704,TIMERD_OPCODE);
//
//      // Modify assembler loaded into cartridge area
//      Cart_WriteHdvAddress(0x185C);
//      break;

    /*
      TOS 1.62 settings
    */
//    case 0x0162:
//      // hdv_init, initialize drives
//      STMemory_WriteWord(0xE01892,RTS_OPCODE);  //RTS
//
//      // E01EA6  JSR $E00D74      hdv_boot, load boot sector
//      STMemory_WriteWord(0xE01EA6,NOP_OPCODE);  //NOP
//      STMemory_WriteWord(0xE01EA6+2,NOP_OPCODE);  //NOP
//      STMemory_WriteWord(0xE01EA6+4,NOP_OPCODE);  //NOP
//
//      // E00576  BSR.W $E0065C    Boot from DMA bus
//      if (bUseVDIRes) {
//        STMemory_WriteWord(0xE00576,0xa000);  //Init Line-A
//        STMemory_WriteWord(0xE00576+2,0xa0ff);  //Trap Line-A(to get structure)
//      }
//      else {
//        STMemory_WriteWord(0xE00576,NOP_OPCODE);  //NOP
//        STMemory_WriteWord(0xE00576+2,NOP_OPCODE);  //NOP
//      }
//
//      // E002DC  CLR.L $4C2(A5)    Set connected drives
//      STMemory_WriteWord(0xE002DC,CONDRV_OPCODE);
//      STMemory_WriteWord(0xE002DC+2,NOP_OPCODE);  //NOP
//
//      // Timer D(MFP init 0xE036BC)
//      STMemory_WriteWord(0xE03704,TIMERD_OPCODE);
//
//      // Modify assembler loaded into cartridge area
//      Cart_WriteHdvAddress(0x185C);
//      break;

    /*
      TOS 2.05 settings
    */
    case 0x0205:
      /* hdv_init, initialize drives */
      STMemory_WriteWord(0xE0468C,RTS_OPCODE);    /* RTS */

      /* E04CA0  JSR $E00E8E      hdv_boot, load boot sector */
      //      STMemory_WriteWord(0xE04CA0,NOP_OPCODE);    /* NOP */
      //STMemory_WriteWord(0xE04CA0+2,NOP_OPCODE);  /* NOP */
      //STMemory_WriteWord(0xE04CA0+4,NOP_OPCODE);  /* NOP */

      /* E006AE  BSR.W $E00794    Boot from DMA bus */
      /* The 2.0x DMA bus boot routine uses two RTSes - patch both */
      if(ACSI_EMU_ON || GEMDOS_EMU_ON) 
      {
        /* no bootable DMA devices */
        STMemory_WriteWord(0xE0081A, CONDRV_OPCODE);
        /* used if we have DMA devices */
        STMemory_WriteWord(0xE00842, CONDRV_OPCODE);
      }
      else
      {
        /* E002FC  CLR.L $4C2      Set connected drives */
        STMemory_WriteWord(0xE002FC,CONDRV_OPCODE);
        STMemory_WriteWord(0xE002FC+2,NOP_OPCODE);  /* NOP */
	
        /* E006AE  BSR.W $E00794    Boot from DMA bus */
        STMemory_WriteWord(0xE006AE,NOP_OPCODE);      /* NOP */
        STMemory_WriteWord(0xE006AE + 2,NOP_OPCODE);  /* NOP */
	    }

      /* Timer D(MFP init 0xE01928) */
      STMemory_WriteWord(0xE01972,TIMERD_OPCODE);

      /* Modify assembler loaded into cartridge area */
      Cart_WriteHdvAddress(0x1410);
      break;

    /*
      TOS 2.06 settings
    */
    case 0x0206:
      /* E007FA  MOVE.L  #$1FFFE,D7  Run checksums on 2xROMs (skip) */
      /* Checksum is total of TOS ROM image, but get incorrect results */
      /* as we've changed bytes in the ROM! So, just skip anyway! */
      if( STMemory_ReadLong(0xE007FA)==0x2E3C0001L )  /* Test if we can patch it */
      {
        STMemory_WriteWord(0xE007FA, BRAW_OPCODE);    /* BRA.W  $E00894 */
        STMemory_WriteWord(0xE007FA+2, 0x98);
      }
      else
      {
        fprintf(stderr, "TOS ROM won't be patched!\n");
        break;    /* Can't patch checksum (is it EmuTOS?) -> skip patches */
      }

      /* hdv_init, initialize drives */
      STMemory_WriteWord(0xE0518E,RTS_OPCODE);    /* RTS */

      /* E05944  JSR  $E011DC    hdv_boot, load boot sector */
      STMemory_WriteWord(0xE05944,NOP_OPCODE);    /* NOP */
      STMemory_WriteWord(0xE05944+2,NOP_OPCODE);  /* NOP */
      STMemory_WriteWord(0xE05944+4,NOP_OPCODE);  /* NOP */

      /* E00898  BSR.W  $E0097A    Boot from DMA bus */
      if(ACSI_EMU_ON || GEMDOS_EMU_ON)
      {
        /* no bootable DMA devices */
        STMemory_WriteWord(0xE00B3E, CONDRV_OPCODE);
        /* used if we have DMA devices */
        STMemory_WriteWord(0xE00B66, CONDRV_OPCODE);
      }
      else
      {
        /* E00362  CLR.L  $4C2    Set connected drives */
        STMemory_WriteWord(0xE00362,NOP_OPCODE);
        STMemory_WriteWord(0xE00362+2,NOP_OPCODE);  /* NOP */

        /* E00898  BSR.W  $E0097A    Boot from DMA bus */
        STMemory_WriteWord(0xE00898,NOP_OPCODE);    /* NOP */
        STMemory_WriteWord(0xE00898+2,NOP_OPCODE);  /* NOP */
      }
      
      /* Timer D(MFP init 0xE02206) */
      STMemory_WriteWord(0xE02250,TIMERD_OPCODE);

      /* Modify assembler loaded into cartridge area */
      Cart_WriteHdvAddress(0x1644);
      break;
  }
}


/*-----------------------------------------------------------------------*/
/*
  Set default memory configuration, connected floppies and memory size
*/
void TOS_SetDefaultMemoryConfig(void)
{
  /* As TOS checks hardware for memory size + connected devices on boot-up */
  /* we set these values ourselves and fill in the magic numbers so TOS */
  /* skips these tests which would crash the emulator as the reference the MMU */

  /* Fill in magic numbers, so TOS does not try to reference MMU */
  STMemory_WriteLong(0x420,0x752019f3);        /* memvalid - configuration is valid */
  STMemory_WriteLong(0x43a,0x237698aa);        /* another magic # */
  STMemory_WriteLong(0x51a,0x5555aaaa);        /* and another */

  /* Set memory size, adjust for extra VDI screens if needed */
  if (bUseVDIRes)
  {
    /* This is enough for 1024x768x16colour (0x60000) */
    STMemory_WriteLong(0x436,MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x60000);  /* mem top - upper end of user memory (before 32k screen) */
    STMemory_WriteLong(0x42e,MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x58000);  /* phys top */
  }
  else
  {
    STMemory_WriteLong(0x436,MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop-0x8000);   /* mem top - upper end of user memory(before 32k screen) */
    STMemory_WriteLong(0x42e,MemoryInfo[ConfigureParams.Memory.nMemorySize].PhysTop);          /* phys top */
  }
  STMemory_WriteLong(0x424,MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryConfig);       /* 512k configure 0x00=128k 0x01=512k 0x10=2Mb 11=reserved eg 0x1010 = 4Mb */
  STMemory_WriteLong(0xff8000,MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryConfig);

  /* Set memory range, and start of BUS error */
  STRamEnd = MemoryInfo[ConfigureParams.Memory.nMemorySize].MemoryEnd;  /* Set end of RAM */
  STRamEnd_BusErr = 0x00420000;    /* 4Mb */      /* Between RAM end and this is void space (0's), after is a BUS error */

  /* Set TOS floppies */
  STMemory_WriteWord(0x446,nBootDrive);           /* Boot up on A(0) or C(2) */
  STMemory_WriteWord(0x4a6,0x2);                  /* Connected floppies A,B (0 or 2) */

  ConnectedDriveMask = ConnectedDriveMaskList[ConfigureParams.HardDisc.nDriveList];

  STMemory_WriteLong(0x4c2,ConnectedDriveMask);   /* Drives A,B and C - NOTE some TOS images overwrite value, see 'TOS_ConnectedDrive_OpCode' */

  /* Mirror ROM boot vectors */
  STMemory_WriteLong(0x00,STMemory_ReadLong(TOSAddress) );
  STMemory_WriteLong(0x04,STMemory_ReadLong(TOSAddress+4) );
}
