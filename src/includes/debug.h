/*
  Hatari
*/

#undef DEBUG_TO_FILE

/*#define DEBUG_OUTPUT_IKBD*/
/*#define DEBUG_OUTPUT_FDC*/

extern void Debug_OpenFiles(void);
extern void Debug_CloseFiles(void);

#ifdef DEBUG_TO_FILE
 extern ofstream debug,debug2,debug3;

 extern void Debug_File(char *format, ...);
 #ifdef DEBUG_OUTPUT_IKBD
  extern void Debug_IKBD(char *format, ...);
 #else  /* DEBUG_OUTPUT_IKBD */
  #define  Debug_IKBD(f,...)
 #endif  /* DEBUG_OUTPUT_IKBD */
 #ifdef DEBUG_OUTPUT_FDC
  extern void Debug_FDC(char *format, ...);
 #else  /* DEBUG_OUTPUT_FDC */
  #define  Debug_FDC(f,...)
 #endif  /* DEBUG_OUTPUT_FDC */

#endif  /* DEBUG_TO_FILE */
