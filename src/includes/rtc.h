/*
  Hatari - rtc.h

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Prototypes for the Mega-ST real time clock emulation.
*/

#ifndef HATARI_RTC_H
#define HATARI_RTC_H

void Rtc_SecondsUnits_ReadByte(void);
void Rtc_SecondsTens_ReadByte(void);
void Rtc_MinutesUnits_ReadByte(void);
void Rtc_MinutesUnits_WriteByte(void);
void Rtc_MinutesTens_ReadByte(void);
void Rtc_MinutesTens_WriteByte(void);
void Rtc_HoursUnits_ReadByte(void);
void Rtc_HoursTens_ReadByte(void);
void Rtc_Weekday_ReadByte(void);
void Rtc_DayUnits_ReadByte(void);
void Rtc_DayTens_ReadByte(void);
void Rtc_MonthUnits_ReadByte(void);
void Rtc_MonthTens_ReadByte(void);
void Rtc_YearUnits_ReadByte(void);
void Rtc_YearTens_ReadByte(void);
void Rtc_ClockMod_ReadByte(void);
void Rtc_ClockMod_WriteByte(void);

#endif
