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

#define MEMDUMP_COLS   16      /* memdump, number of bytes per row */
#define MEMDUMP_ROWS   4       /* memdump, number of rows */
#define DISASM_INSTS   5       /* disasm - number of instructions */

BOOL bMemDump;         /* has memdump been called? */
unsigned long memdump_addr; /* memdump address */
unsigned long disasm_addr;  /* disasm address */

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
  Dissassemble - arg = starting address, or PC.
*/
void DebugUI_DisAsm(char *arg, BOOL cont)
{ 
  int i;
  uaecptr nextpc;
  
  if(cont != TRUE){    
    if(!isHex(arg)) {
      fprintf(stderr,"Invalid address!\n");
      return;
    }
    i = sscanf(arg, "%lx", &disasm_addr);
    
    if(i == 0){
      fprintf(stderr,"Invalid address!\n");
      return;
    }
  } else 
    if(!disasm_addr) disasm_addr = m68k_getpc();

  disasm_addr &= 0x00FFFFFF;

  m68k_disasm(stderr, (uaecptr)disasm_addr, &nextpc, 5);
  disasm_addr = nextpc;
}

/*-----------------------------------------------------------------------*/
/*
  Do a memory dump, args = starting address.
*/
void DebugUI_MemDump(char *arg, BOOL cont)
{ 
  int i,j;
  
  if(cont != TRUE){    
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
  }

  memdump_addr &= 0x00FFFFFF;
  bMemDump = TRUE;

  fprintf(stderr, "%6.6X: ", memdump_addr);
  for(j=0;j<MEMDUMP_ROWS-1;j++){
    for(i=0;i<MEMDUMP_COLS;i++)
      fprintf(stderr, "%2.2x ",STMemory_ReadByte(memdump_addr++));
  fprintf(stderr, "\n%6.6X: ", memdump_addr);
  }
  for(i=0;i<MEMDUMP_COLS;i++)
    fprintf(stderr, "%2.2x ",STMemory_ReadByte(memdump_addr++));
  fprintf(stderr,"\n"); 
}

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
  fprintf(stderr,"---- debug mode commands ----\n");
  fprintf(stderr," d [address]- disassemble from PC, or given address. \n");
  fprintf(stderr," r - dump register values \n");
  fprintf(stderr," m [address] - dump memory at address, \n\tm alone continues from previous address.\n");
  fprintf(stderr," w address bytes - write bytes to a memory address, bytes are space separated. \n");
  fprintf(stderr," l filename address - load a file into memory starting at address. \n");
  fprintf(stderr," s filename address length - dump length bytes from memory to a file. \n");
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

  case 'w':
    if(i < 2){  /* no arg? */
      fprintf(stderr,"  Usage: w address bytes\n");
      return(DEBUG_CMD);
    }
    DebugUI_MemWrite(arg, temp);
    break;

  case 'r':
    DebugUI_RegDump();
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
  bMemDump = FALSE;
  disasm_addr = 0;
  fprintf(stderr,"\nYou have entered debug mode. Type q to quit, h for help. \n------------------------------\n");
  while(DebugUI_Getcommand() != DEBUG_QUIT);
  fprintf(stderr,"Returning to emulation...\n------------------------------\n\n");
}








