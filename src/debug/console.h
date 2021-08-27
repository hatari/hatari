/*
 * Hatari - profile.h
 * 
 * This file is distributed under the GNU General Public License, version 2
 * or at your option any later version. Read the file gpl.txt for details.
 */

#ifndef HATARI_CONSOLE_H
#define HATARI_CONSOLE_H

extern int ConOutDevices;

extern bool Console_SetDevice(int dev);
extern void Console_SetTrace(bool enable);
extern void Console_Check(void);

#endif
