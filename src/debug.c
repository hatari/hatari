/*
  Hatari

  Debug files

  For debugging we can send string to these functions which are output to the debugging files.
  Using this method it is easy to enable/disable the file output
*/

#include "main.h"

#ifdef DEBUG_TO_FILE
ofstream debug,debug2,debug3;
#endif  //DEBUG_TO_FILE

//-----------------------------------------------------------------------
/*
  Create debug files
*/
void Debug_OpenFiles(void)
{
#ifdef DEBUG_TO_FILE
  debug.open("debug.txt");
  debug2.open("debug2.txt");
  debug3.open("debug3.txt");
#endif  //DEBUG_TO_FILE
}

//-----------------------------------------------------------------------
/*
  Close debug files
*/
void Debug_CloseFiles(void)
{
#ifdef DEBUG_TO_FILE
  debug.close();
  debug2.close();
  debug3.close();
#endif  //DEBUG_TO_FILE
}

#ifdef DEBUG_TO_FILE

//-----------------------------------------------------------------------
/*
  Output string to debug file
*/
void Debug_File(char *format, ...)
{
  char szBuffer[1024];
  va_list argptr;

  va_start(argptr, format);
  vsprintf(szBuffer, format, argptr);
  va_end(argptr);

  debug << szBuffer;
}

//-----------------------------------------------------------------------
/*
  Output string to debug file 2 (Keyboard IKBD)
*/
void Debug_IKBD(char *format, ...)
{
  char szBuffer[1024];
  va_list argptr;

  va_start(argptr, format);
  vsprintf(szBuffer, format, argptr);
  va_end(argptr);

  debug2 << szBuffer;
}

//-----------------------------------------------------------------------
/*
  Output string to debug file 3 (Floppy Disc Controller)
*/
void Debug_FDC(char *format, ...)
{
  char szBuffer[1024];
  va_list argptr;

  va_start(argptr, format);
  vsprintf(szBuffer, format, argptr);
  va_end(argptr);

  debug3 << szBuffer;
}

#endif  //DEBUG_TO_FILE
