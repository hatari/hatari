/*
  Hatari

  debugui.c - this is the code for the mini-debugger, when the pause button is pressed,
  the emulator is (hopefully) halted and this little CLI can be used (in the terminal
  box) for debugging tasks like memory and register dumps
  
  (Personal note: Writing this reminded me how much I dislike writing syntax parsers,
  especially with languages with weak string-handling, like C. Also,
  please have oversight with any ugly code: This was written at 4 a.m. /Sven )
*/

#include <ctype.h>

#include "main.h"
#include "configuration.h"
#include "decode.h"
#include "gemdos.h"
#include "intercept.h"
#include "reset.h"
#include "m68000.h"
#include "memorySnapShot.h"
#include "screen.h"
#include "sound.h"
#include "timer.h"
#include "tos.h"
#include "video.h"

#include "uae-cpu/hatari-glue.h"


#define DEBUG_QUIT     0
#define DEBUG_CMD      1

#define MEMDUMP_ROWS   4
#define MEMDUMP_COLS   16

BOOL bMemDump;         /* has memdump been called? */
unsigned long memdump_adr; /* memdump adress */

/* convert string to lowercase */
void string_tolower(char *str)
{
  int i=0;
  while(str[i] != '\0'){
    if(isupper(str[i])) str[i] = tolower(str[i]);
    i++;
  }
}

/* truncate string at first unprintable char (e.g. newline) */
void string_trunc(char *str){
  int i=0;
  while(str[i] != '\0'){
    if(!isprint(str[i])) str[i] = '\0';
    i++;
  }
}

/* check if string is valid hex number. */
BOOL isHex(char *str)
{
  int i=0;
  while(str[i] != '\0' && str[i] != ' '){
    if(!isxdigit(str[i]))return(FALSE);
    i++;
  }
  return(TRUE);
}

/*-----------------------------------------------------------------------*/
/*
  Do a register dump.
*/
void DebugUI_RegDump()
{
  int i;

  fprintf(stderr, "D0 = $%8.8lx\tA0 = $%8.8lx\n", Regs[REG_D0], Regs[REG_A0]);
  fprintf(stderr, "D1 = $%8.8lx\tA1 = $%8.8lx\n", Regs[REG_D1], Regs[REG_A1]);
  fprintf(stderr, "D2 = $%8.8lx\tA2 = $%8.8lx\n", Regs[REG_D2], Regs[REG_A2]);
  fprintf(stderr, "D3 = $%8.8lx\tA3 = $%8.8lx\n", Regs[REG_D3], Regs[REG_A3]);
  fprintf(stderr, "D4 = $%8.8lx\tA4 = $%8.8lx\n", Regs[REG_D4], Regs[REG_A4]);
  fprintf(stderr, "D5 = $%8.8lx\tA5 = $%8.8lx\n", Regs[REG_D5], Regs[REG_A5]);
  fprintf(stderr, "D6 = $%8.8lx\tA6 = $%8.8lx\n", Regs[REG_D6], Regs[REG_A6]);
  fprintf(stderr, "D7 = $%8.8lx\tA7 = $%8.8lx\n", Regs[REG_D7], Regs[REG_A7]);
  fprintf(stderr, "PC = $%8.8lx\tSR = %%", m68k_getpc());
  /* Rather obfuscated way of printing SR in binary */
  for(i=0;i<8;i++)fprintf(stderr, "%i", (SR & (1 << (15-i)))?1:0);
  fprintf(stderr," "); /* space between bytes */
  for(i=8;i<16;i++)fprintf(stderr, "%i", (SR & (1 << (15-i)))?1:0);
  fprintf(stderr,"\n");
}


/*-----------------------------------------------------------------------*/
/*
  Do a memory dump, args = starting adress.
*/
void DebugUI_MemDump(char *arg, BOOL cont)
{ 
  int i,j;
  
  if(cont != TRUE){    
    if(!isHex(arg)) {
      bMemDump = FALSE;
      fprintf(stderr,"Invalid adress!\n");
      return;
    }
    i = sscanf(arg, "%lx", &memdump_adr);
    
    if(i == 0){
      bMemDump = FALSE;
      fprintf(stderr,"Invalid adress!\n");
      return;
    }
  }

  memdump_adr &= 0x00FFFFFF;
  bMemDump = TRUE;

  fprintf(stderr, "%6.6X: ", memdump_adr);
  for(j=0;j<MEMDUMP_ROWS-1;j++){
    for(i=0;i<MEMDUMP_COLS;i++)
      fprintf(stderr, "%2.2x ",STMemory_ReadByte(memdump_adr++));
  fprintf(stderr, "\n%6.6X: ", memdump_adr);
  }
  for(i=0;i<MEMDUMP_COLS;i++)
    fprintf(stderr, "%2.2x ",STMemory_ReadByte(memdump_adr++));
  fprintf(stderr,"\n"); 
}

/*-----------------------------------------------------------------------*/
/*
  Do a memory write, arg = starting adress, followed by bytes.
*/
void DebugUI_MemWrite(char *adr_str, char *arg)
{
  int i, j, numBytes;
  long write_addr;
  unsigned char bytes[300]; /* store bytes */
  char temp[15];
  int d;

  numBytes = 0;
  i = 0;

  string_trunc(arg);
  while(arg[i] == ' ')i++; /* skip spaces */
  while(arg[i] != ' ')i++; /* skip command */
  while(arg[i] == ' ')i++; /* skip spaces */

  j = 0;
  while(isxdigit(arg[i]) && j < 14) /* get adress */
    temp[j++] = arg[i++];
  temp[j] = '\0';
  j = sscanf(temp, "%x", &write_addr);
  
  /* if next char is not valid, or it's not a valid adress */
  if((arg[i] != '\0' && arg[i] != ' ') || (j == 0)){
    fprintf(stderr, "Bad adress!\n");
    return;
  }
      
  write_addr &= 0x00FFFFFF;
  
  while(arg[i] == ' ')i++; /* skip spaces */

  /* get bytes data */  
  while(arg[i] != '\0'){
    j = 0;
    while(isxdigit(arg[i]) && j < 14) /* get byte */
      temp[j++] = arg[i++];
    temp[j] = '\0';

    /* if next char is not a null or a space - it's not valid. */
    if(arg[i] != '\0' && arg[i] != ' '){
	fprintf(stderr, "Bad byte argument: %c\n", arg[i]);
	return;
    }
      
    if(temp[0] != '\0')
      if(sscanf(temp,"%x", &d) != 1){
	fprintf(stderr, "Bad byte argument!\n");
	return;
      }	 

    bytes[numBytes] = (d&0x0FF);
    numBytes++;
    while(arg[i] == ' ')i++; /* skip any spaces */
  }

  /* write the data */
  for(i=0;i<numBytes;i++)
    STMemory_WriteByte(write_addr + i, bytes[i]);
}

/*-----------------------------------------------------------------------*/
/*
  Help!
*/
void DebugUI_Help()
{
  fprintf(stderr,"---- debug mode commands ----\n");
  fprintf(stderr," r - dump register values \n");
  fprintf(stderr," m [address] - dump memory at adress, \n\tm alone continues from previous adress.\n");
  fprintf(stderr," w adress bytes - write bytes to memory adress, bytes are space separated. \n");
  fprintf(stderr," q - return to emulation\n");
  fprintf(stderr,"-----------------------------\n");
  fprintf(stderr,"\n");
}

/*-----------------------------------------------------------------------*/
/*
  Get a UI command, return it.
*/
int DebugUI_Getcommand()
{  
  char temp[255];
  char command[255], arg[255];
  int i;
  fprintf(stderr,"> ");
  temp[0] = '\0';
  fgets(temp, 255, stdin);

  string_tolower(temp);
  i = sscanf(temp, "%s%s", command, arg);

  if(i == 0){
    fprintf(stderr,"  Unknown command.\n");
    return(DEBUG_CMD);
  }

  switch(command[0]){
  case 'q':
    return(DEBUG_QUIT);
    break;

  case 'h':
  case '?':
    DebugUI_Help(); /* get help */
    return(DEBUG_CMD);
    break;

  case 'm':
    if(i < 2){  /* no arg? */
      if(bMemDump == FALSE){
	fprintf(stderr,"Usage: m adress\n");
	return(DEBUG_CMD);
      }
      DebugUI_MemDump(arg, TRUE);     /* No arg - continue memdump */
    } else DebugUI_MemDump(arg, FALSE); /* new memdump */
    break;

  case 'w':
    if(i < 2){  /* no arg? */
      fprintf(stderr,"Usage: w adress bytes\n");
      return(DEBUG_CMD);
    }
    DebugUI_MemWrite(arg, temp);
    break;

  case 'r':
    DebugUI_RegDump();
    break;

  default:
    fprintf(stderr,"  Unknown command: '%s'\n", command);
    break;
  }

  return(DEBUG_CMD);
}


/*-----------------------------------------------------------------------*/
/*
  Debug UI
*/
void DebugUI()
{
  bMemDump = FALSE;

  fprintf(stderr,"\nYou have entered debug mode. Type q to quit, h for help. \n------------------------------\n");
  while(DebugUI_Getcommand() != DEBUG_QUIT);
  fprintf(stderr,"Returning to emulation...\n------------------------------\n\n");
}



