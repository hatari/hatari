/*
  Hatari

  Debug Timer functions
*/

#include "main.h"

#define LARGE_INTEGER long long  /* ???? */
#define LONGLONG long long       /* ???? */

#define QueryPerformanceFrequency(x)  /* FIXME: What the !$*? is this? */
#define QueryPerformanceCounter(x)    /* Dito */


LARGE_INTEGER StartCount,EndCount,Frequency;


/*-----------------------------------------------------------------------*/
/*
  Initialise debug timer
*/
void Timer_Init(void)
{
#ifdef FIND_PERFORMANCE
  /* Find frequency to calculate 'milli-second' results */
  QueryPerformanceFrequency(&Frequency);
#endif
}


/*-----------------------------------------------------------------------*/
/*
  Start timer
*/
void Timer_Start()
{
  /* Start timer */
  QueryPerformanceCounter(&StartCount);
}


/*-----------------------------------------------------------------------*/
/*
  Stop timer, return as 'milli-second' count(float)
*/
float Timer_Stop()
{
  LONGLONG a,b;

  /* End timer */
  QueryPerformanceCounter(&EndCount);

  /* Find time and frequency */
  a = EndCount /*.QuadPart*/ - StartCount /*.QuadPart*/;  /* FIXME */
  b = Frequency /*.QuadPart*/ ;

  return( ((float)a/(float)b) * 1000.0f );  /* as 'ms' */
}
