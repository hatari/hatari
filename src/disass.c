/*
  Hatari

  Disassemble 'OpCode' into text string for use with debugger

  This works in a similar way to the decoding of instructions, which while produces
  more complicated code is very handy for debugging in the early stages of development.
  (Anyhow, none of this is included in the final executable!)
  Note the 'rprintf' functions; these do a sprintf but the variables are in reverse order
  as I use function calls as parameters which 'C' puts them on stack in the opposite order.
  These functions are also used when writing out a history of the last 'x' instructions
  ran, which is used for debugging.
*/

#include "main.h"
//#include "debugger.h"
#include "decode.h"
#include "disass.h"
#include "m68000.h"
#include "misc.h"
#include "stMemory.h"

#ifdef USE_DEBUGGER

char szSizeString[4];             /* Size B,W or L */
char szParamString[256];          /* Data */
char szConditionString[256];      /* Condition code */
char szImmString[256];            /* Immediate */
char szEffAddrString[256];        /* Effective address */
char szUpperEffAddrString[256];   /* Effective address, upper bits in OpCode (for MOVE instruction) */
char szRegString[256];            /* MoveM register list */
char szOpString[256];             /* Final disassembly */
char szOpData[256];               /* Final disassembly */

unsigned short int OpCode;        /* Opcode of instruction */
unsigned long DisPC;              /* Disassembly Program Counter */

char *pszCC_Strings[] = {
  "T",   // 0000 (Not used in Bcc)
  "F",   // 0001 (Not used in Bcc)
  "HI",  // 0010
  "LS",  // 0011
  "CC",  // 0100
  "CS",  // 0101
  "NE",  // 0110
  "EQ",  // 0111
  "VC",  // 1000
  "VS",  // 1001
  "PL",  // 1010
  "MI",  // 1011
  "GE",  // 1100
  "LT",  // 1101
  "GT",  // 1110
  "LE"   // 1111
};

//-----------------------------------------------------------------------
/*
  As 'C' functions parameters are in 'reverse' order, have set of functions
  which pass parameters in correct order and swap in function.
  Uses 'C++' overloads to get correct types!
*/
void rprintf0(char *pSrcString)
{
  sprintf(szOpString,pSrcString);
}
//-----------------------------------------------------------------------
// %s
void rprintf1(char *pSrcString, char *pString1)
{
  sprintf(szOpString,pSrcString,pString1);
}
// %d
void rprintf1(char *pSrcString, int Var1)
{
  sprintf(szOpString,pSrcString,Var1);
}
//-----------------------------------------------------------------------
// %s %s
void rprintf2(char *pSrcString, char *pString1, char *pString2)
{
  sprintf(szOpString,pSrcString,pString2,pString1);
}
// %d %s
void rprintf2(char *pSrcString, char *pString1, int Var2)
{
  sprintf(szOpString,pSrcString,Var2,pString1);
}
// %s %d
void rprintf2(char *pSrcString, int Var1, char *pString2)
{
  sprintf(szOpString,pSrcString,pString2,Var1);
}
// %d %d
void rprintf2(char *pSrcString, int Var1, int Var2)
{
  sprintf(szOpString,pSrcString,Var2,Var1);
}
//-----------------------------------------------------------------------
// %s %s %s
void rprintf3(char *pSrcString, char *pString1, char *pString2, char *pString3)
{
  sprintf(szOpString,pSrcString,pString3,pString2,pString1);
}
// %s %s %d
void rprintf3(char *pSrcString, int Var1, char *pString2, char *pString3)
{
  sprintf(szOpString,pSrcString,pString3,pString2,Var1);
}
// %s %d %s
void rprintf3(char *pSrcString, char *pString1, int Var2, char *pString3)
{
  sprintf(szOpString,pSrcString,pString3,Var2,pString1);
}
// %s %d %d
void rprintf3(char *pSrcString, int Var1, int Var2, char *pString3)
{
  sprintf(szOpString,pSrcString,pString3,Var2,Var1);
}
//-----------------------------------------------------------------------
// %s %d %s %d
void rprintf4(char *pSrcString, int Var1, char *pString2, int Var3, char *pString4)
{
  sprintf(szOpString,pSrcString,pString4,Var3,pString2,Var1);
}
// %s %s %d %d
void rprintf4(char *pSrcString, int Var1, int Var2, char *pString3, char *pString4)
{
  sprintf(szOpString,pSrcString,pString4,pString3,Var2,Var1);
}


//-----------------------------------------------------------------------
/*
  Read byte,byte(odd address),word and long from memory
*/
unsigned char Disass_ReadByte(void)
{
  char szString[256];
  unsigned char Var;

  Var = *(unsigned char *)((unsigned long)STRam+(DisPC&0xffffff));
  DisPC += SIZE_BYTE;

  // Add to szOpString
  sprintf(szString,"%2.2X",Var);
  strcat(szOpData,szString);

  return(Var);
}
unsigned char Disass_ReadByte_OddAddr(void)
{
  char szString[256];
  unsigned char Var;

  Var = *(unsigned char *)((unsigned long)STRam+(DisPC&0xffffff)+1);
  DisPC += SIZE_WORD;

  // Add to szOpString
  sprintf(szString,"%4.4X",Var);
  strcat(szOpData,szString);

  return(Var);
}
unsigned short int Disass_ReadWord(void)
{
  char szString[256];
  unsigned short int Var;

  Var = *(unsigned short int *)((unsigned long)STRam+(DisPC&0xffffff));
  DisPC += SIZE_WORD;

  // Add to szOpString
  sprintf(szString,"%4.4X",STMemory_Swap68000Int(Var));
  strcat(szOpData,szString);

  return(STMemory_Swap68000Int(Var));
}
unsigned long Diass_ReadLong(void)
{
  char szString[256];
  unsigned long Var;

  Var = *(unsigned long *)((unsigned long)STRam+(DisPC&0xffffff));
  DisPC += SIZE_LONG;

  // Add to szOpString
  sprintf(szString,"%8.8X",STMemory_Swap68000Long(Var));
  strcat(szOpData,szString);

  return(STMemory_Swap68000Long(Var));
}

//-----------------------------------------------------------------------
/*
  Convert unsigned values to strings
*/
char *Disass_ByteToString(unsigned char DataB)
{
  if (DataB<=9)
    sprintf(szParamString,"%d",DataB);
  else
    sprintf(szParamString,"$%X",DataB);
  
  return(szParamString);
}
char *Disass_WordToString(unsigned short int DataW)
{
  if (DataW<=9)
    sprintf(szParamString,"%d",DataW);
  else
    sprintf(szParamString,"$%X",DataW);
  
  return(szParamString);
}
char *Disass_LongToString(unsigned long DataL)
{
  if (DataL<=9)
    sprintf(szParamString,"%d",DataL);
  else
    sprintf(szParamString,"$%X",DataL);
  
  return(szParamString);
}

//-----------------------------------------------------------------------
/*
  Convert signed values to strings, pass NULL as String to write to 'szParamString' (default)
*/
char *Disass_SignedByteToString(char DataB,char *pString=NULL)
{
  if (pString==NULL)
    pString = szParamString;

  if (abs(DataB)<=9) {
    if (DataB>=0)
      sprintf(pString,"%d",DataB);
    else
      sprintf(pString,"-%d",-DataB);
  }
  else {
    if (DataB>=0)
      sprintf(pString,"$%X",DataB);
    else
      sprintf(pString,"$%X",(unsigned char)DataB);
  }

  return(pString);
}
char *Disass_SignedWordToString(short int DataW,char *pString=NULL)
{
  if (pString==NULL)
    pString = szParamString;

  if (abs(DataW)<=9) {
    if (DataW>=0)
      sprintf(pString,"%d",DataW);
    else
      sprintf(pString,"-%d",-DataW);
  }
  else {
    if (DataW>=0)
      sprintf(pString,"$%X",DataW);
    else
      sprintf(pString,"$%X",(unsigned short int)DataW);
  }

  return(pString);
}
char *Disass_SignedLongToString(long DataL,char *pString=NULL)
{
  if (pString==NULL)
    pString = szParamString;

  if (abs(DataL)<=9) {
    if (DataL>=0)
      sprintf(pString,"%d",DataL);
    else
      sprintf(pString,"-%d",-DataL);
  }
  else {
    if (DataL>=0)
      sprintf(pString,"$%X",DataL);
    else
      sprintf(pString,"$%X",(unsigned long)DataL);
  }

  return(pString);
}

//-----------------------------------------------------------------------
/*
  Create 'Effective Address' string from OpCode and size, 'asserts' on error
*/
char *Disass_CalcEffAddr(int Size,unsigned short int Mode,unsigned short int Reg, char *pString)
{
  unsigned long OffsetL;
  unsigned short int OffsetW,EReg,OffsetSize;
  unsigned char OffsetB;

  // Clear string, allows easy error check when complete
  strcpy(pString,"");

  switch(Mode) {
    case BIN3(0,0,0):  // Dn
      sprintf(pString,"D%d",Reg);
      break;
    case BIN3(0,0,1):  // An
      sprintf(pString,"A%d",Reg);
      break;

    case BIN3(0,1,0):  // (An)
      sprintf(pString,"(A%d)",Reg);
      break;
    case BIN3(0,1,1):  // (An)+
      sprintf(pString,"(A%d)+",Reg);
      break;
    case BIN3(1,0,0):  // -(An)
      sprintf(pString,"-(A%d)",Reg);
      break;

    case BIN3(1,0,1):  // (d16,An)
      OffsetW = Disass_ReadWord();
      sprintf(pString,"%s(A%d)",Disass_SignedWordToString(OffsetW),Reg);
      break;

    case BIN3(1,1,0):  // (d8,An,Xn)
      OffsetW = Disass_ReadWord();
      EReg = OffsetW>>12;        // d0-d7,a0-a7
      OffsetSize = (OffsetW>>11)&0x1;  // 0-Sign extended word, 1-long
      if (EReg<REG_A0) {
        if (OffsetSize==0)
          sprintf(pString,"%s(A%d,D%d.W)",Disass_SignedByteToString((char)OffsetW),Reg,EReg);
        else
          sprintf(pString,"%s(A%d,D%d.L)",Disass_SignedByteToString((char)OffsetW),Reg,EReg);
      }
      else {
        if (OffsetSize==0)
          sprintf(pString,"%s(A%d,A%d.W)",Disass_SignedByteToString((char)OffsetW),Reg,EReg-REG_A0);
        else
          sprintf(pString,"%s(A%d,A%d.L)",Disass_SignedByteToString((char)OffsetW),Reg,EReg-REG_A0);
      }
      break;

    case BIN3(1,1,1):  // 0x111
      switch(Reg) {
        case BIN3(0,0,0):  // xxx.W
          OffsetW = Disass_ReadWord();
          sprintf(pString,"%s",Disass_WordToString(OffsetW));
          break;
        case BIN3(0,0,1):  // xxx.L
          OffsetL = Diass_ReadLong();
          sprintf(pString,"%s",Disass_LongToString(OffsetL));
          break;
        case BIN3(0,1,0):  // (d16,PC)
          OffsetW = Disass_ReadWord();
          sprintf(pString,"%s(PC)",Disass_LongToString((long)DisPC+(short int)OffsetW-SIZE_WORD));
          break;
        case BIN3(0,1,1):  // (d8,PC,Xn)
          OffsetW = Disass_ReadWord();
          EReg = OffsetW>>12;        // d0-d7,a0-a7
          OffsetSize = (OffsetW>>11)&0x1;  // 0-Sign extended word, 1-long
          if (EReg<REG_A0) {
            if (OffsetSize==0)
              sprintf(pString,"%s(PC,D%d.W)",Disass_LongToString((long)DisPC+(char)OffsetW-SIZE_WORD),EReg);
            else
              sprintf(pString,"%s(PC,D%d.L)",Disass_LongToString((long)DisPC+(char)OffsetW-SIZE_WORD),EReg);
          }
          else {
            if (OffsetSize==0)
              sprintf(pString,"%s(PC,A%d.W)",Disass_LongToString((long)DisPC+(char)OffsetW-SIZE_WORD),EReg-REG_A0);
            else
              sprintf(pString,"%s(PC,A%d.L)",Disass_LongToString((long)DisPC+(char)OffsetW-SIZE_WORD),EReg-REG_A0);
          }
          break;
        case BIN3(1,0,0):  // 0x100  // # data
          switch(Size) {
            case SIZE_BYTE:
              OffsetB = Disass_ReadByte_OddAddr();
              sprintf(pString,"#%s",Disass_SignedByteToString(OffsetB));
              break;
            case SIZE_WORD:
              OffsetW = Disass_ReadWord();
              sprintf(pString,"#%s",Disass_SignedWordToString(OffsetW));
              break;
            case SIZE_LONG:
              OffsetL = Diass_ReadLong();
              sprintf(pString,"#%s",Disass_SignedLongToString(OffsetL));
              break;
          }
          break;
      }
      break;
  }

  // Check for errors!
  if (strlen(pString)==0) {
    assert(0);
  }

  return(pString);
}

//-----------------------------------------------------------------------
/*
  Use these two functions to find Effective Address, use 'upper' version for MOVE instruction
*/
char *Disass_FindEffAddr(int Size)
{
  unsigned short int Mode,Reg;

  // Find effective address Mode and Register
  Mode = (OpCode>>3)&0x7;
  Reg = OpCode&0x7;

  return(Disass_CalcEffAddr(Size,Mode,Reg,szEffAddrString));
}
char *Disass_FindUpperEffAddr(int Size)
{
  unsigned short int Mode,Reg;

  // Find effective address Mode and Register in upper bits in OpCode
  Mode = (OpCode>>6)&0x7;
  Reg = (OpCode>>9)&0x7;

  return(Disass_CalcEffAddr(Size,Mode,Reg,szUpperEffAddrString));
}

//-----------------------------------------------------------------------
/*
  Find size .B,.W,.L
  Do as string and as SIZE_xxxx
*/
char *Disass_FindSize_00_01_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x3;
  switch(Size) {
    case BIN2(0,0):    strcpy(szSizeString,"B"); break;
    case BIN2(0,1):    strcpy(szSizeString,"W"); break;
    case BIN2(1,0):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_00_01_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x3;
  switch(Size) {
    case BIN2(0,0):    return(SIZE_BYTE);
    case BIN2(0,1):    return(SIZE_WORD);
    case BIN2(1,0):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_000_001_010(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,0,0):    strcpy(szSizeString,"B"); break;
    case BIN3(0,0,1):    strcpy(szSizeString,"W"); break;
    case BIN3(0,1,0):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_000_001_010(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,0,0):    return(SIZE_BYTE);
    case BIN3(0,0,1):    return(SIZE_WORD);
    case BIN3(0,1,0):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_011_111(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,1,1):    strcpy(szSizeString,"W"); break;
    case BIN3(1,1,1):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_011_111(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,1,1):    return(SIZE_WORD);
    case BIN3(1,1,1):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_100_101_110(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(1,0,0):    strcpy(szSizeString,"B"); break;
    case BIN3(1,0,1):    strcpy(szSizeString,"W"); break;
    case BIN3(1,1,0):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_100_101_110(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(1,0,0):    return(SIZE_BYTE);
    case BIN3(1,0,1):    return(SIZE_WORD);
    case BIN3(1,1,0):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_010_011(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,1,0):    strcpy(szSizeString,"W"); break;
    case BIN3(0,1,1):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_010_011(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(0,1,0):    return(SIZE_WORD);
    case BIN3(0,1,1):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_110_111(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(1,1,0):  strcpy(szSizeString,"W"); break;
    case BIN3(1,1,1):  strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}

//-----------------------------------------------------------------------
char *Disass_FindSize_100_101(void)
{
  unsigned short int Size;

  Size = (OpCode>>6)&0x7;
  switch(Size) {
    case BIN3(1,0,0):  strcpy(szSizeString,"W"); break;
    case BIN3(1,0,1):  strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}

//-----------------------------------------------------------------------
/*
  NOTE This is ONLY to be used with MOVE instruction, as Size bits are 12,13
*/
char *Disass_FindSize_01_11_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>12)&0x3;
  switch(Size) {
    case BIN2(0,1):    strcpy(szSizeString,"B"); break;
    case BIN2(1,1):    strcpy(szSizeString,"W"); break;
    case BIN2(1,0):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_01_11_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>12)&0x3;
  switch(Size) {
    case BIN2(0,1):    return(SIZE_BYTE);
    case BIN2(1,1):    return(SIZE_WORD);
    case BIN2(1,0):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
/*
  NOTE This is ONLY to be used with MOVE instruction, as Size bits are 12,13
*/
char *Disass_FindSize_11_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>12)&0x3;
  switch(Size) {
    case BIN2(1,1):    strcpy(szSizeString,"W"); break;
    case BIN2(1,0):    strcpy(szSizeString,"L"); break;
  }

  return(szSizeString);
}
int Disass_EffAddrSize_11_10(void)
{
  unsigned short int Size;

  Size = (OpCode>>12)&0x3;
  switch(Size) {
    case BIN2(1,1):    return(SIZE_WORD);
    case BIN2(1,0):    return(SIZE_LONG);
  }

  // Error
  assert(0);
  return(0);
}

//-----------------------------------------------------------------------
/*
  Find size of MOVEM instruction operands
*/
char *Disass_FindSize_MoveM(void)
{
  if ((OpCode&BIN7(1,0,0,0,0,0,0))==0)
    sprintf(szSizeString,"W");
  else
    sprintf(szSizeString,"L");

  return(szSizeString);
}
int Disass_EffAddrSize_MoveM(void)
{
  if ((OpCode&BIN7(1,0,0,0,0,0,0))==0)
    return(SIZE_WORD);
  else
    return(SIZE_LONG);
}

//-----------------------------------------------------------------------
/*
  Find if access BYTE or LONG according to 'Dn' or other effective address (see BTST etc...)
*/
char *Disass_FindSize_WordOrLong(void)
{
  if (((OpCode>>3)&0x7)==BIN3(0,0,0))  // Dn
    sprintf(szSizeString,"L");
  else
    sprintf(szSizeString,"B");

  return(szSizeString);
}
int Disass_EffAddrSize_WordOrLong(void)
{
  if (((OpCode>>3)&0x7)==BIN3(0,0,0))  // Dn
    return(SIZE_LONG);
  else
    return(SIZE_BYTE);
}

//-----------------------------------------------------------------------
/*
  Read Immediate data after OpCode
*/
char *Disass_ReadImmediate(int Size)
{
  // Clear string, allows easy error check when complete
  strcpy(szImmString,"");

  switch(Size) {
    case SIZE_BYTE:
      Disass_SignedByteToString(Disass_ReadByte_OddAddr(),szImmString);
      break;
    case SIZE_WORD:
      Disass_SignedWordToString(Disass_ReadWord(),szImmString);
      break;
    case SIZE_LONG:
      Disass_SignedLongToString(Diass_ReadLong(),szImmString);
      break;
  }

  // Check for errors!
  if (strlen(szImmString)==0) {
    assert(0);
  }

  return(szImmString);
}

//-----------------------------------------------------------------------
/*
  Find conditon codes using in Bcc,Scc,DBcc etc...
*/
char *Disass_FindCondition(void)
{
  strcpy(szConditionString,pszCC_Strings[(OpCode>>8)&0xf]);
  
  return(szConditionString);
}

//-----------------------------------------------------------------------
/*
  Find register index using bits 9,10,11
*/
int Disass_FindRegister(void)
{
  return((OpCode>>9)&0x7);
}

// And using bits 0,1,2
int Disass_FindRegisterLower(void)
{
  return(OpCode&0x7);
}

//-----------------------------------------------------------------------
/*
  Find shift count 1,2,3,4...8 from data 0,1,2,3...7
*/
#define Disass_FindQuickData  Disass_FindShiftCount  // 'Quick' data ie ADDQ uses same process
int Disass_FindShiftCount(void)
{
  unsigned short int Count;

  Count = (OpCode>>9)&0x7;
  if (Count==0)
    Count = 8;

  return(Count);
}

//-----------------------------------------------------------------------
/*
  Return TRUE is displacement is a signed 8-bit number
  Used in BRA,BSR,Bcc etc...
*/
BOOL Disass_ShortDisplacement(void)
{
  if ((OpCode&0xff)==0)
    return(FALSE);    // 16-bit offset
  else
    return(TRUE);    // 8-bit offset
}

//-----------------------------------------------------------------------
/*
  Convert register mask for MOVEM into string, eg D0123/A167 (D0-D3/A1/A6-A7)
*/
char *Disass_FindMoveMRegisters(unsigned short int MaskW)
{
  char szString[256];
  int i;

  // Set 'Dx-Dy/Ax-Ay' string
  strcpy(szRegString,"");
  // Is pre-decrement? -(An)
  if (((OpCode>>3)&0x7)==BIN3(1,0,0)) {  // D0..D7/A0..A7
    if (MaskW&0xff00) {  // Any 'D' registers?
      strcat(szRegString,"D");
      for(i=0; i<8; i++) {
        if (MaskW&(0x8000>>i)) {
          sprintf(szString,"%d",i);
          strcat(szRegString,szString);
        }
      }
    }
    if (MaskW&0x00ff) {  // Any 'A' registers?
      // Separate by '/'
      if (strlen(szRegString)>0)
        strcat(szRegString,"/");

      strcat(szRegString,"A");
      for(i=0; i<8; i++) {
        if (MaskW&(0x0080>>i)) {
          sprintf(szString,"%d",i);
          strcat(szRegString,szString);
        }
      }
    }
  }
  else {              // A7..A0/D7..D0
    if (MaskW&0x00ff) {  // Any 'D' registers?
      strcat(szRegString,"D");
      for(i=0; i<8; i++) {
        if (MaskW&(0x0001<<i)) {
          sprintf(szString,"%d",i);
          strcat(szRegString,szString);
        }
      }
    }
    if (MaskW&0xff00) {  // Any 'A' registers?
      // Separate by '/'
      if (strlen(szRegString)>0)
        strcat(szRegString,"/");

      strcat(szRegString,"A");
      for(i=0; i<8; i++) {
        if (MaskW&(0x0100<<i)) {
          sprintf(szString,"%d",i);
          strcat(szRegString,szString);
        }
      }
    }
  }

  return(szRegString);
}



//-----------------------------------------------------------------------
/*
  List of disassembly instruction functions
*/

//-----------------------------------------------------------------------
// ABCD Dy,Dx
void Disass_ABCD_Dy_Dx(void)
{
  rprintf2( "ABCD.B\tD%d,D%d",Disass_FindRegister(),Disass_FindRegisterLower() );
}

// ABCD -(Ay),-(Ax)
void Disass_ABCD_Ay_Ax(void)
{
  rprintf2( "ABCD.B\t-(A%d),-(A%d)",Disass_FindRegister(),Disass_FindRegisterLower() );
}

//-----------------------------------------------------------------------
// ADD <ea>,Dn
void Disass_ADD_ea_Dn(void)
{
  rprintf3( "ADD.%s\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_000_001_010()),Disass_FindSize_000_001_010() );
}

// ADD Dn,<ea>
void Disass_ADD_Dn_ea(void)
{
  rprintf3( "ADD.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_100_101_110()),Disass_FindRegister(),Disass_FindSize_100_101_110() );
}

//-----------------------------------------------------------------------
// ADDA <ea>,An
void Disass_ADDA(void)
{
  rprintf3( "ADDA.%s\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_011_111()),Disass_FindSize_011_111() );
}

//-----------------------------------------------------------------------
// ADDI #<data>,<ea>
void Disass_ADDI(void)
{
  rprintf3( "ADDI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// ADDQ #<data>,<ea>
void Disass_ADDQ(void)
{
  rprintf3( "ADDQ.%s\t#%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindQuickData(),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// ADDX Dy,Dx
void Disass_ADDX_Dy_Dx(void)
{
  rprintf3( "ADDX.%s\tD%d,D%d",Disass_FindRegister(),Disass_FindRegisterLower(),Disass_FindSize_00_01_10() );
}

// ADDX -(Ay),-(Ax)
void Disass_ADDX_Ay_Ax(void)
{
  rprintf3( "ADDX.%s\t-(A%d),-(A%d)",Disass_FindRegister(),Disass_FindRegisterLower(),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// AND <ea>,Dn
void Disass_AND_ea_Dn(void)
{
  rprintf3( "AND.%s\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_000_001_010()),Disass_FindSize_000_001_010() );
}

// AND Dn,<ea>
void Disass_AND_Dn_ea(void)
{
  rprintf3( "AND.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_100_101_110()),Disass_FindRegister(),Disass_FindSize_100_101_110() );
}

//-----------------------------------------------------------------------
// ANDI #<data>,<ea>
void Disass_ANDI(void)
{
  rprintf3( "ANDI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// AND to CCR #<data>,CCR
void Disass_ANDI_to_CCR(void)
{
  rprintf1( "ANDI.B\t#%s,CCR",Disass_ReadImmediate(SIZE_BYTE) );
}

//-----------------------------------------------------------------------
// AND to SR #<data>,SR
void Disass_ANDI_to_SR(void)
{
  rprintf1( "ANDI.W\t#%s,SR",Disass_ReadImmediate(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// ASL Dx,Dy
void Disass_ASL_Dx_Dy(void)
{
  rprintf3( "ASL.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ASL #<data>,Dy
void Disass_ASL_data_Dy(void)
{
  rprintf3( "ASL.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ASL <ea>
void Disass_ASL(void)
{
  rprintf1( "ASL.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// ASR Dx,Dy
void Disass_ASR_Dx_Dy(void)
{
  rprintf3( "ASR.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ASR #<data>,Dy
void Disass_ASR_data_Dy(void)
{
  rprintf3( "ASR.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ASR <ea>
void Disass_ASR(void)
{
  rprintf1( "ASR.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// Bcc <label>
void Disass_Bcc(void)
{
  short int OffsetW;

  if (Disass_ShortDisplacement())
    rprintf2( "B%s.S\t$%X",DisPC+(char)OpCode,Disass_FindCondition() );
  else {
    OffsetW = Disass_ReadWord();
    rprintf2( "B%s.W\t$%X",(DisPC-SIZE_WORD)+OffsetW,Disass_FindCondition() );
  }
}

//-----------------------------------------------------------------------
// BCHG Dn,<ea>
void Disass_BCHG(void)
{
  rprintf3( "BCHG.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_FindRegister(),Disass_FindSize_WordOrLong() );
}

// BCHG #<data>,<ea>
void Disass_BCHG_imm(void)
{
  rprintf3( "BCHG.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_ReadImmediate(SIZE_BYTE),Disass_FindSize_WordOrLong() );
}

//-----------------------------------------------------------------------
// BCLR Dn,<ea>
void Disass_BCLR(void)
{
  rprintf3( "BCLR.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_FindRegister(),Disass_FindSize_WordOrLong() );
}

// BCLR #<data>,<ea>
void Disass_BCLR_imm(void)
{
  rprintf3( "BCLR.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_ReadImmediate(SIZE_BYTE),Disass_FindSize_WordOrLong() );
}

//-----------------------------------------------------------------------
// BRA <label>
void Disass_BRA(void)
{
  short int OffsetW;

  if (Disass_ShortDisplacement())
    rprintf1( "BRA.S\t$%X",DisPC+(char)OpCode );
  else {
    OffsetW = Disass_ReadWord();
    rprintf1( "BRA.W\t$%X",(DisPC-SIZE_WORD)+OffsetW );
  }
}

//-----------------------------------------------------------------------
// BSET Dn,<ea>
void Disass_BSET(void)
{
  rprintf3( "BSET.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_FindRegister(),Disass_FindSize_WordOrLong() );
}

// BSET #<data>,<ea>
void Disass_BSET_imm(void)
{
  rprintf3( "BSET.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_ReadImmediate(SIZE_BYTE),Disass_FindSize_WordOrLong() );
}

//-----------------------------------------------------------------------
// BSR <label>
void Disass_BSR(void)
{
  short int OffsetW;

  if (Disass_ShortDisplacement())
    rprintf1( "BSR.S\t$%X",DisPC+(char)OpCode );
  else {
    OffsetW = Disass_ReadWord();
    rprintf1( "BSR.W\t$%X",(DisPC-SIZE_WORD)+OffsetW );
  }
}

//-----------------------------------------------------------------------
// BTST Dn,<ea>
void Disass_BTST(void)
{
  rprintf3( "BTST.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_FindRegister(),Disass_FindSize_WordOrLong() );
}

// BTST #<data>,<ea>
void Disass_BTST_imm(void)
{
  rprintf3( "BTST.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_WordOrLong()),Disass_ReadImmediate(SIZE_BYTE),Disass_FindSize_WordOrLong() );
}

//-----------------------------------------------------------------------
// CHK <ea>,Dn
void Disass_CHK(void)
{
  rprintf2( "CHK.W\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// CLR <ea>
void Disass_CLR(void)
{
  rprintf2( "CLR.%s\t%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// CMP <ea>,Dn
void Disass_CMP(void)
{
  rprintf3( "CMP.%s\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_000_001_010()),Disass_FindSize_000_001_010() );
}

//-----------------------------------------------------------------------
// CMPA <ea>,An
void Disass_CMPA(void)
{
  rprintf3( "CMPA.%s\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_011_111()),Disass_FindSize_011_111() );
}

//-----------------------------------------------------------------------
// CMPI #<data>,<ea>
void Disass_CMPI(void)
{
  rprintf3( "CMPI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// CMPM (Ay)+,(Ax)+
void Disass_CMPM(void)
{
  rprintf3( "CMPM.%s\t(A%d)+,(A%d)+",Disass_FindRegister(),Disass_FindRegisterLower(),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// DBcc Dn,<label>
void Disass_DBcc(void)
{
  short int OffsetW;

  OffsetW = Disass_ReadWord();
  rprintf3( "DB%s.W\tD%d,$%X",(DisPC-SIZE_WORD)+OffsetW,Disass_FindRegisterLower(),Disass_FindCondition() );
}

//-----------------------------------------------------------------------
// DIVS <ea>,Dn
void Disass_DIVS(void)
{
  rprintf2( "DIVS.W\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// DIVU <ea>,Dn
void Disass_DIVU(void)
{
  rprintf2( "DIVU.W\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// EOR Dn,<ea>
void Disass_EOR(void)
{
  rprintf3( "EOR.%s\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_100_101_110()),Disass_FindSize_100_101_110() );
}

//-----------------------------------------------------------------------
// EORI #<data>,Dn
void Disass_EORI(void)
{
  rprintf3( "EORI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// EOR to CCR #<data>,CCR
void Disass_EORI_to_CCR(void)
{
  rprintf1( "EORI.B\t#%s,CCR",Disass_ReadImmediate(SIZE_BYTE) );
}

//-----------------------------------------------------------------------
// EOR to SR #<data>,SR
void Disass_EORI_to_SR(void)
{
  rprintf1( "EORI.W\t#%s,SR",Disass_ReadImmediate(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// EXG Dx,Dy
void Disass_EXG_Dx_Dy(void)
{
  rprintf2( "EXG.L\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister() );
}

// EXG Ax,Ay
void Disass_EXG_Ax_Ay(void)
{
  rprintf2( "EXG.L\tA%d,A%d",Disass_FindRegisterLower(),Disass_FindRegister() );
}

// EXG Dx,Ay
void Disass_EXG_Dx_Ay(void)
{
  rprintf2( "EXG.L\tD%d,A%d",Disass_FindRegisterLower(),Disass_FindRegister() );
}

//-----------------------------------------------------------------------
// EXT Dn
void Disass_EXT(void)
{
  rprintf2( "EXT.%s\tD%d",Disass_FindRegisterLower(),Disass_FindSize_010_011() );
}

//-----------------------------------------------------------------------
// ILLEGAL
void Disass_ILLEGAL(void)
{
  rprintf0( "ILLEGAL" );
}

//-----------------------------------------------------------------------
// JMP <ea>
void Disass_JMP(void)
{
  rprintf1( "JMP\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// JSR <ea>
void Disass_JSR(void)
{
  rprintf1( "JSR\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// LEA <ea>,An
void Disass_LEA(void)
{
  rprintf2( "LEA\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_LONG) );
}

//-----------------------------------------------------------------------
// LINK An,#<displacement>
void Disass_LINK(void)
{
  unsigned short int OffsetW;

  OffsetW = Disass_ReadWord();
  rprintf2( "LINK\tA%d,#%s",Disass_WordToString(OffsetW),Disass_FindRegisterLower() );
}

//-----------------------------------------------------------------------
// LSL Dx,Dy
void Disass_LSL_Dx_Dy(void)
{
  rprintf3( "LSL.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// LSL #<data>,Dy
void Disass_LSL_data_Dy(void)
{
  rprintf3( "LSL.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// LSL <ea>
void Disass_LSL(void)
{
  rprintf1( "LSL.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// LSR Dx,Dy
void Disass_LSR_Dx_Dy(void)
{
  rprintf3( "LSR.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// LSR #<data>,Dy
void Disass_LSR_data_Dy(void)
{
  rprintf3( "LSR.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// LSR <ea>
void Disass_LSR(void)
{
  rprintf1( "LSR.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// MOVE <ea>,<ea>
void Disass_MOVE(void)
{
  rprintf3( "MOVE.%s\t%s,%s",Disass_FindUpperEffAddr(Disass_EffAddrSize_01_11_10()),Disass_FindEffAddr(Disass_EffAddrSize_01_11_10()),Disass_FindSize_01_11_10() );
}

//-----------------------------------------------------------------------
// MOVEA <ea>,An
void Disass_MOVEA(void)
{
  rprintf3( "MOVEA.%s\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_11_10()),Disass_FindSize_11_10() );
}

//-----------------------------------------------------------------------
// MOVE CCR,<ea>
void Disass_MOVE_from_CCR(void)
{
  rprintf1( "MOVE.W\tCCR,%s",Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// MOVE <ea>,CCR
void Disass_MOVE_to_CCR(void)
{
  rprintf1( "MOVE.W\t%s,CCR",Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// MOVE SR,<ea>
void Disass_MOVE_from_SR(void)
{
  rprintf1( "MOVE.W\tSR,%s",Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// MOVE <ea>,SR
void Disass_MOVE_to_SR(void)
{
  rprintf1( "MOVE.W\t%s,SR",Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// MOVE USP,An
void Disass_MOVE_USP_An(void)
{
  rprintf1( "MOVE.L\tUSP,A%d",Disass_FindRegisterLower() );
}

// MOVE An,USP
void Disass_MOVE_An_USP(void)
{
  rprintf1( "MOVE.L\tA%d,USP",Disass_FindRegisterLower() );
}

//-----------------------------------------------------------------------
// MOVEM regs,<ea>
void Disass_MOVEM_regs_ea(void)
{
  unsigned short int MaskW;

  MaskW = Disass_ReadWord();
  rprintf3( "MOVEM.%s\t%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_MoveM()),Disass_FindMoveMRegisters(MaskW),Disass_FindSize_MoveM() );
}

// MOVEM <ea>,regs
void Disass_MOVEM_ea_regs(void)
{
  unsigned short int MaskW;

  MaskW = Disass_ReadWord();
  rprintf3( "MOVEM.%s\t%s,%s",Disass_FindMoveMRegisters(MaskW),Disass_FindEffAddr(Disass_EffAddrSize_MoveM()),Disass_FindSize_MoveM() );
}

//-----------------------------------------------------------------------
// MOVEP Dx,(d,Ay)
void Disass_MOVEP_Dn_An(void)
{
  rprintf4( "MOVEP.%s\tD%d,%s(A%d)",Disass_FindRegisterLower(),Disass_ReadImmediate(SIZE_WORD),Disass_FindRegister(),Disass_FindSize_110_111() );
}

// MOVEP (d,Ay),Dx
void Disass_MOVEP_An_Dn(void)
{
  rprintf4( "MOVEP.%s\t%s(A%d),D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_ReadImmediate(SIZE_WORD),Disass_FindSize_100_101() );
}

//-----------------------------------------------------------------------
// MOVEQ #<data>,Dn
void Disass_MOVEQ(void)
{
  rprintf2( "MOVEQ.L\t#%d,D%d",Disass_FindRegister(),(char)OpCode );
}

//-----------------------------------------------------------------------
// MULS <ea>,Dn
void Disass_MULS(void)
{
  rprintf2( "MULS.W\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// MULU <ea>,Dn
void Disass_MULU(void)
{
  rprintf2( "MULU.W\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// NBCD <ea>
void Disass_NBCD(void)
{
  rprintf1( "NBCD.B\t%s",Disass_FindEffAddr(SIZE_BYTE) );
}

//-----------------------------------------------------------------------
// NEG <ea>
void Disass_NEG(void)
{
  rprintf2( "NEG.%s\t%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// NEGX <ea>
void Disass_NEGX(void)
{
  rprintf2( "NEGX.%s\t%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// NOP
void Disass_NOP(void)
{
  rprintf0( "NOP" );
}

//-----------------------------------------------------------------------
// NOT <ea>
void Disass_NOT(void)
{
  rprintf2( "NOT.%s\t%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// OR <ea>,Dn
void Disass_OR_ea_Dn(void)
{
  rprintf3( "OR.%s\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_000_001_010()),Disass_FindSize_000_001_010() );
}

// OR Dn,<ea>
void Disass_OR_Dn_ea(void)
{
  rprintf3( "OR.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_100_101_110()),Disass_FindRegister(),Disass_FindSize_100_101_110() );
}

//-----------------------------------------------------------------------
// ORI #<data>,<ea>
void Disass_ORI(void)
{
  rprintf3( "ORI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// OR to CCR #<data>,CCR
void Disass_ORI_to_CCR(void)
{
  rprintf1( "ORI.B\t#%s,CCR",Disass_ReadImmediate(SIZE_BYTE) );
}

//-----------------------------------------------------------------------
// OR to SR #<data>,SR
void Disass_ORI_to_SR(void)
{
  rprintf1( "ORI.W\t#%s,SR",Disass_ReadImmediate(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// PEA <ea>
void Disass_PEA(void)
{
  rprintf1( "PEA.L\t%s",Disass_FindEffAddr(SIZE_LONG) );
}

//-----------------------------------------------------------------------
// RESET
void Disass_RESET(void)
{
  rprintf0( "RESET" );
}

//-----------------------------------------------------------------------
// ROL Dx,Dy
void Disass_ROL_Dx_Dy(void)
{
  rprintf3( "ROL.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ROL #<data>,Dy
void Disass_ROL_data_Dy(void)
{
  rprintf3( "ROL.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ROL <ea>
void Disass_ROL(void)
{
  rprintf1( "ROL.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// ROR Dx,Dy
void Disass_ROR_Dx_Dy(void)
{
  rprintf3( "ROR.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ROR #<data>,Dy
void Disass_ROR_data_Dy(void)
{
  rprintf3( "ROR.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ROR <ea>
void Disass_ROR(void)
{
  rprintf1( "ROR.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// ROXL Dx,Dy
void Disass_ROXL_Dx_Dy(void)
{
  rprintf3( "ROXL.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ROXL #<data>,Dy
void Disass_ROXL_data_Dy(void)
{
  rprintf3( "ROXL.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ROXL <ea>
void Disass_ROXL(void)
{
  rprintf1( "ROXL.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// ROXR Dx,Dy
void Disass_ROXR_Dx_Dy(void)
{
  rprintf3( "ROXR.%s\tD%d,D%d",Disass_FindRegisterLower(),Disass_FindRegister(),Disass_FindSize_00_01_10() );
}

// ROXR #<data>,Dy
void Disass_ROXR_data_Dy(void)
{
  rprintf3( "ROXR.%s\t#%s,D%d",Disass_FindRegisterLower(),Disass_WordToString(Disass_FindShiftCount()),Disass_FindSize_00_01_10() );
}

// ROXR <ea>
void Disass_ROXR(void)
{
  rprintf1( "ROXR.W\t%s",Disass_FindEffAddr(NULL) );
}

//-----------------------------------------------------------------------
// RTE
void Disass_RTE(void)
{
  rprintf0( "RTE" );
}

//-----------------------------------------------------------------------
// RTR
void Disass_RTR(void)
{
  rprintf0( "RTR" );
}

//-----------------------------------------------------------------------
// RTS
void Disass_RTS(void)
{
  rprintf0( "RTS" );
}

//-----------------------------------------------------------------------
// SBCD Dy,Dx
void Disass_SBCD_Dy_Dx(void)
{
  rprintf2( "SBCD.B\tD%d,D%d",Disass_FindRegister(),Disass_FindRegisterLower() );
}

// SBCD -(Ay),-(Ax)
void Disass_SBCD_Ay_Ax(void)
{
  rprintf2( "SBCD.B\t-(A%d),-(A%d)",Disass_FindRegister(),Disass_FindRegisterLower() );
}

//-----------------------------------------------------------------------
// Scc <ea>
void Disass_Scc(void)
{
  rprintf2( "S%s.B\t%s",Disass_FindEffAddr(SIZE_BYTE),Disass_FindCondition() );
}

//-----------------------------------------------------------------------
// STOP #<data>
void Disass_STOP(void)
{
  rprintf1( "STOP\t#%s",Disass_ReadImmediate(SIZE_WORD) );
}

//-----------------------------------------------------------------------
// SUB <ea>,Dn
void Disass_SUB_ea_Dn(void)
{
  rprintf3( "SUB.%s\t%s,D%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_000_001_010()),Disass_FindSize_000_001_010() );
}

// SUB Dn,<ea>
void Disass_SUB_Dn_ea(void)
{
  rprintf3( "SUB.%s\tD%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_100_101_110()),Disass_FindRegister(),Disass_FindSize_100_101_110() );
}

//-----------------------------------------------------------------------
// SUBA <ea>,An
void Disass_SUBA(void)
{
  rprintf3( "SUBA.%s\t%s,A%d",Disass_FindRegister(),Disass_FindEffAddr(Disass_EffAddrSize_011_111()),Disass_FindSize_011_111() );
}

//-----------------------------------------------------------------------
// SUBI #<data>,<ea>
void Disass_SUBI(void)
{
  rprintf3( "SUBI.%s\t#%s,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_ReadImmediate(Disass_EffAddrSize_00_01_10()),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// SUBQ #<data>,<ea>
void Disass_SUBQ(void)
{
  rprintf3( "SUBQ.%s\t#%d,%s",Disass_FindEffAddr(Disass_EffAddrSize_00_01_10()),Disass_FindQuickData(),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// SUBX Dy,Dx
void Disass_SUBX_Dy_Dx(void)
{
  rprintf3( "SUBX.%s\tD%d,D%d",Disass_FindRegister(),Disass_FindRegisterLower(),Disass_FindSize_00_01_10() );
}

// SUBX -(Ay),-(Ax)
void Disass_SUBX_Ay_Ax(void)
{
  rprintf3( "SUBX.%s\t-(A%d),-(A%d)",Disass_FindRegister(),Disass_FindRegisterLower(),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// SWAP Dn
void Disass_SWAP(void)
{
  rprintf1( "SWAP.W\tD%d",Disass_FindRegisterLower() );
}

//-----------------------------------------------------------------------
// TAS <ea>
void Disass_TAS(void)
{
  rprintf1( "TAS.B\t%s",Disass_FindEffAddr(SIZE_BYTE) );
}

//-----------------------------------------------------------------------
// TRAP #<vector>
void Disass_TRAP(void)
{
  rprintf1( "TRAP\t#%d",OpCode&0xf );
}

//-----------------------------------------------------------------------
// TRAPV
void Disass_TRAPV(void)
{
  rprintf0( "TRAPV" );
}

//-----------------------------------------------------------------------
// TST <ea>
void Disass_TST(void)
{
  rprintf2( "TST.%s\t%s",Disass_FindEffAddr(SIZE_BYTE),Disass_FindSize_00_01_10() );
}

//-----------------------------------------------------------------------
// UNLK An
void Disass_UNLK(void)
{
  rprintf1( "UNLK\tA%d",Disass_FindRegisterLower() );
}



//-----------------------------------------------------------------------
/*
  Disassemble from 'DisPC' program counter to 'szOpData', update DisPC
  Return TRUE if instruction was valid
*/
BOOL Disass_DiassembleLine(void)
{
  void *pFunc;
  int OpCodeIndex;

  // Clear OpCode data
  strcpy(szOpData,"");

  // Read 'Opcode'
  OpCode = Disass_ReadWord();

  // Look up disassembly function
  OpCodeIndex = STMemory_Swap68000Int(OpCode)*SIZEOF_DECODE;
  pFunc = (void *)DecodeTable[OpCodeIndex+(DECODE_DISASS/sizeof(long))];  
  if (pFunc) {
    CALL_VAR(pFunc);                // Disassemble
    return(TRUE);
  }
  else {
    strcpy(szOpString,"----");            // Not an instruction
    return(FALSE);
  }
}

#endif  //USE_DEBUGGER
