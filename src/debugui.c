/*
  Hatari

  debugui.c - this is the code for the mini-debugger, when the pause button is pressed,
  the emulator is (hopefully) halted and this little CLI can be used (in the terminal
  box) for debugging tasks like memory and register dumps

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

#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define MEMDUMP_ROWS   4       /* memdump, number of rows */
#define NON_PRINT_CHAR '.'     /* character to display for non-printables */
#define DISASM_INSTS   5       /* disasm - number of instructions */

BOOL bMemDump;         /* has memdump been called? */
unsigned long memdump_addr; /* memdump address */
unsigned long disasm_addr;  /* disasm address */

FILE *debugLogFile;
FILE *debug_stdout;

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

/* 
   Get a hex adress range, eg. "fa0000-fa0100" 
   returns -1 if not a range,
   -2 if a range, but not a valid one.
   0 if OK.
*/
BOOL getRange(char *str, unsigned long *lower, unsigned long *upper){
  BOOL fDash = FALSE;
  int i=0;

  while(str[i] != '\0') 
    {
      if(str[i] == '-') 
	{
	  str[i] = ' ';
	  fDash = TRUE;
	}
      i++;
    }
  if(fDash == FALSE) return(-1);  

  i = sscanf(str, "%lx%lx", lower, upper);
  if(i != 2)  return (-2);
  if(*lower > *upper) return(-3); 
  return(0);
}

/*-----------------------------------------------------------------------*/
/*
  Open a log file.
*/
void DebugUI_OpenLog(char *arg){
  debugLogFile = fopen(arg, "w");
  if(debugLogFile == NULL)
    fprintf(stderr, "Can't open file: %s\n", arg);
  debug_stdout = debugLogFile;  
}

/*-----------------------------------------------------------------------*/
/*
  Load a binary file to a memory address.
*/
void DebugUI_LoadBin(char *args){
  FILE *fp;
  unsigned char c;
  char dummy[100];
  char filename[200];
  unsigned long address;
  int i=0;

  if(sscanf(args, "%s%s%lx", dummy, filename, &address) != 3){
    fprintf(stderr, "Invalid arguments!\n");
    return;
  }
  address &= 0x00FFFFFF;
  if((fp = fopen(filename, "rb")) == NULL){
    fprintf(stderr,"Cannot open file!\n");
  }

  c = fgetc(fp);
  while(!feof(fp)){
    i++;
    STMemory_WriteByte(address++, c);    
    c = fgetc(fp);
  }
  fprintf(stderr,"  Read 0x%x bytes.\n", i);
  fclose(fp);
}

/*-----------------------------------------------------------------------*/
/*
  Dump memory from an address to a binary file.
*/
void DebugUI_SaveBin(char *args){
  FILE *fp;
  unsigned char c;
  char filename[200];
  char dummy[100];
  unsigned long address;
  unsigned long bytes;
  int i=0;

  if(sscanf(args, "%s%s%lx%lx", dummy, filename, &address, &bytes) != 4){
    fprintf(stderr, "  Invalid arguments!");
    return;
  }
  address &= 0x00FFFFFF;
  if((fp = fopen(filename, "wb")) == NULL){
    fprintf(stderr,"  Cannot open file!\n");
  }

  while(i < bytes){
    c = STMemory_ReadByte(address++);    
    fputc(c, fp);
    i++;
  }
  fclose(fp);
  fprintf(stderr, "  Wrote 0x%x bytes.\n", bytes); 
}

/*-----------------------------------------------------------------------*/
/*
  Do a register dump.
*/
void DebugUI_RegDump()
{
  int i;
  uaecptr nextpc;
  /* use the UAE function instead */
  m68k_dumpstate(debug_stdout, &nextpc);
}


/*-----------------------------------------------------------------------*/
/*
  Dissassemble - arg = starting address, or PC.
*/
void DebugUI_DisAsm(char *arg, BOOL cont)
{ 
  int i,j;
  unsigned long disasm_upper;
  uaecptr nextpc;
  BOOL isRange = FALSE;
  
  if(cont != TRUE){        
    j = getRange(arg, &disasm_addr, &disasm_upper);

    if( j == -1 ){ /* single address, not a range */
      if(!isHex(arg)) {
	fprintf(stderr,"Invalid address!\n");
	return;
      }
      i = sscanf(arg, "%lx", &disasm_addr);
      
      if(i == 0){
	fprintf(stderr,"Invalid address!\n");
	return;
      }
    } /* single address */
    else if(j == -2 || j == -3){
      fprintf(stderr,"Invalid range!\n");
      return;
    }
    else { /* range */
      isRange = TRUE;
      disasm_upper &= 0x00FFFFFF;
    }

  } else /* continue*/
    if(!disasm_addr) disasm_addr = m68k_getpc();

  disasm_addr &= 0x00FFFFFF;
  
  /* output a single block. */
  if( isRange == FALSE)
    {
      m68k_disasm(debug_stdout, (uaecptr)disasm_addr, &nextpc, DISASM_INSTS);
      disasm_addr = nextpc;
      return;
   }

  /* output a range */
  while(disasm_addr < disasm_upper)
    {
      m68k_disasm(debug_stdout, (uaecptr)disasm_addr, &nextpc, 1);
      disasm_addr = nextpc;
    }
  return;
}

/*-----------------------------------------------------------------------*/
/*
  Set a register: 
*/
void DebugUI_RegSet(char *arg){
  int i;
  BOOL s = FALSE;
  char reg[4];
  long value;

  for(i=0;i<4;i++) reg[i] = 0;
  i=0;
  while(arg[i] != '\0'){
    if(arg[i] == '=')
      { 
      arg[i] = ' ';
      s = TRUE;
      }
    i++;
  }

  if( s == FALSE ){
    fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
    return;
  }

  if(sscanf(arg, "%s%lx", reg, &value) == 2) s = TRUE; else s = FALSE;
  if( s == FALSE ){
    fprintf(stderr,"\tError, usage: r or r xx=yyyy\n\tWhere: xx=A0-A7, D0-D7, PC or SR and yyyy is a hex value.\n");
    return;
  }

  for(i=0;i<4;i++) reg[i] = toupper(reg[i]);

  /* FIXME: update conditional flags for the UAE core. 
     set SR
  if(reg[0] == 'S' && reg[1] == 'R')
    SR = value;
*/
  /* set PC */
  if(reg[0] == 'P' && reg[1] == 'C')
    m68k_setpc( value );

  /* Data regs */
  else if(reg[0] == 'D') switch( reg[1] ){
  case '0':
    Regs[REG_D0] = value;
    break;
  case '1':
    Regs[REG_D1] = value;
    break;
  case '2':
    Regs[REG_D2] = value;
    break;
  case '3':
    Regs[REG_D3] = value;
    break;
  case '4':
    Regs[REG_D4] = value;
    break;
  case '5':
    Regs[REG_D5] = value;
    break;
  case '6':
    Regs[REG_D6] = value;
    break;
  case '7':
    Regs[REG_D7] = value;
    break;

  default:
    fprintf(stderr,"\tBad data register, valid values are 0-7\n");
    break;
  }

  /* Address regs */
  else if(reg[0] == 'A') switch( reg[1] ){
  case '0':
    Regs[REG_A0] = value;
    break;
  case '1':
    Regs[REG_A1] = value;
    break;
  case '2':
    Regs[REG_A2] = value;
    break;
  case '3':
    Regs[REG_A3] = value;
    break;
  case '4':
    Regs[REG_A4] = value;
    break;
  case '5':
    Regs[REG_A5] = value;
    break;
  case '6':
    Regs[REG_A6] = value;
    break;
  case '7':
    Regs[REG_A7] = value;
    break;

  default:
    fprintf(stderr,"\tBad address register, valid values are 0-7\n");
    break;
  }

  else fprintf(stderr, "\t Bad register!\n");
  
}

/*-----------------------------------------------------------------------*/
/*
  Do a memory dump, args = starting address.
*/
void DebugUI_MemDump(char *arg, BOOL cont)
{ 
  int i,j;
  char c;
  BOOL isRange = FALSE;
  unsigned long memdump_upper;

  

  if(cont != TRUE){        
    j = getRange(arg, &memdump_addr, &memdump_upper);

    if( j == -1 ){ /* single address, not a range */
      if(!isHex(arg)) {
	bMemDump = FALSE;
	fprintf(stderr,"Invalid address!\n");
	return;
      }
      i = sscanf(arg, "%lx", &memdump_addr);
      
      if(i == 0){
	bMemDump = FALSE;
	fprintf(stderr,"Invalid address!\n");
	return;
      }
    } /* single address */
    else if(j == -2 || j == -3){
      fprintf(stderr,"Invalid range!\n");
      return;
    }
    else { /* range */
      isRange = TRUE;
      memdump_upper &= 0x00FFFFFF;
    }
  } /* continue */

  memdump_addr &= 0x00FFFFFF;
  bMemDump = TRUE;
  if(isRange != TRUE)
    {
      for(j=0;j<MEMDUMP_ROWS;j++){
	fprintf(debug_stdout, "%6.6X: ", memdump_addr); /* print address */
	for(i=0;i<MEMDUMP_COLS;i++)               /* print hex data */
	  fprintf(debug_stdout, "%2.2x ",STMemory_ReadByte(memdump_addr++));
	fprintf(debug_stdout, "  ");                    /* print ASCII data */
	for(i=0;i<MEMDUMP_COLS;i++){
	  c = STMemory_ReadByte(memdump_addr-MEMDUMP_COLS+i);
	  if(!isprint(c)) c = NON_PRINT_CHAR;             /* non-printable as dots */
	  fprintf(debug_stdout,"%c", c);
	}
	fprintf(debug_stdout, "\n", memdump_addr);   /* newline */
      }
      return;
    } /* not a range */

  while(memdump_addr < memdump_upper)
    {
      fprintf(debug_stdout, "%6.6X: ", memdump_addr); /* print address */
      for(i=0;i<MEMDUMP_COLS;i++)               /* print hex data */
	fprintf(debug_stdout, "%2.2x ",STMemory_ReadByte(memdump_addr++));
      fprintf(debug_stdout, "  ");                    /* print ASCII data */
      for(i=0;i<MEMDUMP_COLS;i++){
	c = STMemory_ReadByte(memdump_addr-MEMDUMP_COLS+i);
	if(!isprint(c)) c = NON_PRINT_CHAR;             /* non-printable as dots */
	fprintf(debug_stdout,"%c", c);
      }
      fprintf(debug_stdout, "\n", memdump_addr);   /* newline */
    } /* while */
} /* end of memdump */

/*-----------------------------------------------------------------------*/
/*
  Do a memory write, arg = starting address, followed by bytes.
*/
void DebugUI_MemWrite(char *addr_str, char *arg)
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
  while(isxdigit(arg[i]) && j < 14) /* get address */
    temp[j++] = arg[i++];
  temp[j] = '\0';
  j = sscanf(temp, "%x", &write_addr);
  
  /* if next char is not valid, or it's not a valid address */
  if((arg[i] != '\0' && arg[i] != ' ') || (j == 0)){
    fprintf(stderr, "Bad address!\n");
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
  fprintf(stderr,"---- debug mode commands ----\n"
                 " d [address]- disassemble from PC, or given address. \n"
                 " r [REG=value] - dump register values/ set register to value \n"
                 " m [address] - dump memory at address, \n\tm alone continues from previous address.\n"
                 " w address bytes - write bytes to a memory address, bytes are space separated. \n"  
	         " f [filename] - open log file, no argument closes the log file\n"
	         "   Output of reg & mem dumps and disassembly will be written to the log\n"
                 " l filename address - load a file into memory starting at address. \n"  
                 " s filename address length - dump length bytes from memory to a file. \n\n"  
                 " q - return to emulation\n\n"
	         " Adresses may be given as a range e.g. fc0000-fc0100\nAll values in hexadecimal.\n"
                 "-----------------------------\n"  
                 "\n");
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

  i = sscanf(temp, "%s%s", command, arg);
  string_tolower(command);

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

  case 'd':
    if(i < 2)  /* no arg? */
      DebugUI_DisAsm(arg, TRUE);     /* No arg - disassemble at PC */
    else DebugUI_DisAsm(arg, FALSE); /* disasm at address. */
    break;

  case 'm':
    if(i < 2){  /* no arg? */
      if(bMemDump == FALSE){
	fprintf(stderr,"  Usage: m address\n");
	return(DEBUG_CMD);
      }
      DebugUI_MemDump(arg, TRUE);     /* No arg - continue memdump */
    } else DebugUI_MemDump(arg, FALSE); /* new memdump */
    break;

  case 'f':
    if(i < 2){  /* no arg? */
      if(debugLogFile == NULL)
	fprintf(stderr, "No log file open.\n");
      else {
	fclose(debugLogFile);
	debug_stdout = stderr;  
	fprintf(stderr, "Log closed.\n");
      } 
    }
    else DebugUI_OpenLog(arg);
    break;

  case 'w':
    if(i < 2){  /* no arg? */
      fprintf(stderr,"  Usage: w address bytes\n");
      return(DEBUG_CMD);
    }
    DebugUI_MemWrite(arg, temp);
    break;

  case 'r':
    if(i < 2){  /* no arg - dump regs */
      DebugUI_RegDump();
      return(DEBUG_CMD);
    }
    DebugUI_RegSet(arg);
    break;

  case 'l':
    if(i < 2){  /* no arg? */
      fprintf(stderr,"  Usage: l filename address\n");
      return(DEBUG_CMD);
    }
    DebugUI_LoadBin(temp);
    break;

  case 's':
    if(i < 2){  /* no arg? */
      fprintf(stderr,"  Usage: s filename address bytes\n");
      return(DEBUG_CMD);
    }
    DebugUI_SaveBin(temp);
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
  debugLogFile = NULL;
  debug_stdout = stderr;  /* output to screen, until log file opened */

  bMemDump = FALSE;
  disasm_addr = 0;
  fprintf(stderr,"\nYou have entered debug mode. Type q to quit, h for help. \n------------------------------\n");
  while(DebugUI_Getcommand() != DEBUG_QUIT);
  if(debugLogFile != NULL) fclose(debugLogFile);
  fprintf(stderr,"Returning to emulation...\n------------------------------\n\n");
}








