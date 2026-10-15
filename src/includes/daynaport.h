/*
  Hatari - daynaport.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/
#ifndef HATARI_DAYNAPORT_H
#define HATARI_DAYNAPORT_H

#include "hdc.h"

extern bool DaynaPort_Init(SCSI_DEV *dev, const char *ifname);
extern void DaynaPort_UnInit(void);
extern void DaynaPort_EmulateCommand(SCSI_CTRLR *ctr);
extern void DaynaPort_DataOut(SCSI_CTRLR *ctr);

#endif
