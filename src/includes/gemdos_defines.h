/*
  Hatari - gemdos_defines.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_GEMDOS_DEFINES_H
#define HATARI_GEMDOS_DEFINES_H

/*
  GEMDOS error codes, See 'The Atari Compendium' D.3
*/
#define GEMDOS_EOK      0    // OK
#define GEMDOS_ERROR   -1    // Generic error
#define GEMDOS_EDRVNR  -2    // Drive not ready
#define GEMDOS_EUNCMD  -3    // Unknown command
#define GEMDOS_E_CRC   -4    // CRC error
#define GEMDOS_EBADRQ  -5    // Bad request
#define GEMDOS_E_SEEK  -6    // Seek error
#define GEMDOS_EMEDIA  -7    // Unknown media
#define GEMDOS_ESECNF  -8    // Sector not found
#define GEMDOS_EPAPER  -9    // Out of paper
#define GEMDOS_EWRITF  -10   // Write fault
#define GEMDOS_EREADF  -11   // Read fault
#define GEMDOS_EWRPRO  -13   // Device is write protected
#define GEMDOS_E_CHNG  -14   // Media change detected
#define GEMDOS_EUNDEV  -15   // Unknown device
#define GEMDOS_EINVFN  -32   // Invalid function
#define GEMDOS_EFILNF  -33   // File not found
#define GEMDOS_EPTHNF  -34   // Path not found
#define GEMDOS_ENHNDL  -35   // No more handles
#define GEMDOS_EACCDN  -36   // Access denied
#define GEMDOS_EIHNDL  -37   // Invalid handle
#define GEMDOS_ENSMEM  -39   // Insufficient memory
#define GEMDOS_EIMBA   -40   // Invalid memory block address
#define GEMDOS_EDRIVE  -46   // Invalid drive specification
#define GEMDOS_ENSAME  -48   // Cross device rename
#define GEMDOS_ENMFIL  -49   // No more files
#define GEMDOS_ELOCKED -58   // Record is already locked
#define GEMDOS_ENSLOCK -59   // Invalid lock removal request
#define GEMDOS_ERANGE  -64   // Range error
#define GEMDOS_EINTRN  -65   // Internal error
#define GEMDOS_EPLFMT  -66   // Invalid program load format
#define GEMDOS_EGSBF   -67   // Memory block growth failure
#define GEMDOS_ELOOP   -80   // Too many symbolic links
#define GEMDOS_EMOUNT  -200  // Mount point crossed (indicator)

/*
  GemDOS file attributes
*/
#define GEMDOS_FILE_ATTRIB_READONLY      0x01
#define GEMDOS_FILE_ATTRIB_HIDDEN        0x02
#define GEMDOS_FILE_ATTRIB_SYSTEM_FILE   0x04
#define GEMDOS_FILE_ATTRIB_VOLUME_LABEL  0x08
#define GEMDOS_FILE_ATTRIB_SUBDIRECTORY  0x10
/* file was written and closed correctly (used automatically on gemdos >=0.15) */
#define GEMDOS_FILE_ATTRIB_WRITECLOSE    0x20

#endif /* HATARI_GEMDOS_DEFINES_H */
