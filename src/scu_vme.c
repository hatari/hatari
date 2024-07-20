/*
  Hatari - scu_vme.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  SCU (System Control Unit) interrupt handling, used only in MegaSTE and TT

  References :
    - Atari TT030 Hardware reference Manual - June 1990
    - Atari Profibuch ST-STE-TT, chapter 9 (german edition) - 1991


  TODO: Non-cacheable TT VME card address mapping (word based data transfer):
  - FE000000-FEFEFFFF VMEbus A24:D16
  - FEFF0000-FEFFFFFF VMEbus A16:D16

  TODO: more limited MegaSTE VME card address mapping:
  - 00A00000-00DEFFFF VMEbus A24:D16
  - 00DF0000-00DFFFFF VMEbus A16:D16

  SCU IRQ info from TT HW reference:
  - SCU generated IRQ1 is detected only by the MPU not the VMEbus
  - SCU generated IRQ1 and IRQ3 are hardwired to the corresponding
    priorities and are always auto vectored
  - only interrupts 5 and 6 have external IACK pins and are capable
    of generating vectored interrupts on the motherboard (and also
    cause VME IRQ5 and IRQ6 respectively)
  - VMEbus SYSFAIL generates a system (motherboard) IRQ7 to the MPU,
    but does not not generate an IRQ7 to the VMEbus. The only other
    source of an IRQ7 is a VMEbus card

  TODO :
   - SCU generated IRQ3 is ignored because it should send a level 3 IRQ
     on the VME bus, but we don't emulate any VME board at the moment


  June 2024 : Nicolas Pomarède, add all the required SCU logic to handle interrupts on MegaSTE / TT
              (code was "empty" before, only displaying traces)

*/
const char vme_fileid[] = "Hatari scu_vme.c";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "scu_vme.h"
#include "m68000.h"
#include "hatari-glue.h"
#include "memorySnapShot.h"



/* Possible interrupt level for SysIntMask at $FF8E01 */
#define	SCU_SYS_INT_LEVEL_VME_SYSFAIL		7
#define	SCU_SYS_INT_LEVEL_MFP			6
#define	SCU_SYS_INT_LEVEL_SCC			5
#define	SCU_SYS_INT_LEVEL_VSYNC			4
#define	SCU_SYS_INT_LEVEL_UNUSED_3		3
#define	SCU_SYS_INT_LEVEL_HSYNC			2
#define	SCU_SYS_INT_LEVEL_SOFT_INT		1
#define	SCU_SYS_INT_LEVEL_UNUSED_0		0



typedef struct {
	uint8_t		SysIntMask;			/* FF8E01 */
	uint8_t		SysIntState;			/* FF8E03 */
	uint8_t		SysInterrupter;			/* FF8E05 */

	uint8_t		VmeIntMask;			/* FF8E0D */
	uint8_t		VmeIntState;			/* FF8E0F */
	uint8_t		VmeInterrupter;			/* FF8E07 */

	uint8_t		GPR1;				/* FF8E09 */
	uint8_t		GPR2;				/* FF8E0B */
} SCU_REGS;


static SCU_REGS		SCU;






/**
 * SCU trace logging for read / write accesses
 */
static void SCU_TraceRead ( const char *info )
{
	LOG_TRACE_VAR int addr = IoAccessCurrentAddress;
	LOG_TRACE(TRACE_SCU, "scu read %s %x=0x%x pc=%x\n", info , addr, IoMem[addr], M68000_GetPC());
}

static void SCU_TraceWrite ( const char *info )
{
	LOG_TRACE_VAR int addr = IoAccessCurrentAddress;
	LOG_TRACE(TRACE_SCU, "scu write %s %x=0x%x pc=%x\n", info , addr, IoMem[addr], M68000_GetPC());
}


/**
 * Return 'true' if SCU is enabled (MegaSTE or TT), else 'false'
 */
bool	SCU_IsEnabled ( void )
{
	return ( Config_IsMachineTT() || Config_IsMachineMegaSTE() );
}


/**
 * Reset SCU/VME registers and interrupts
 */
void	SCU_Reset ( bool bCold )
{
	if ( !SCU_IsEnabled() )
		return;

	/* All the SCU regs are cleared on reset */
	SCU.SysIntMask = 0x00;					/* TOS will set 0x14 : hsync and vsync */
	SCU.SysIntState = 0x00;
	SCU.SysInterrupter = 0x00;
	SCU.VmeIntMask = 0x00;					/* TOS will set 0x60 : MFP and SCC */
	SCU.VmeIntState = 0x00;
	SCU.VmeInterrupter = 0x00;

	/* GPR1 and GPR2 are cleared only on cold boot, they keep their content on warm boot */
	if ( bCold )
	{
		SCU.GPR1 = 0x00;
		SCU.GPR2 = 0x00;
	}

	/* TODO: ...but TOS v2 / v3 crash on MegaSTE / TT
	 * unless gen reg 1 has this value, why?
	 */
	SCU.GPR1 = 0x01;

	/* Update CPU interrupts (=clear all) */
	SCU_UpdatePendingInts_CPU();
}



/**
 * Functions used to process the IRQ that should go to the CPU and change IPL
 *
 * On MegaSTE / TT all IRQ are connected to the SCU, which uses 2 masks to forward or not
 * the IRQ to the CPU through the IPL signal (on STF/STE/Falcon, changes in IRQ will update IPL directly)
 *
 * - SysIntMask is used to mask IRQ level 1 - 7 coming from the motherboard (hsync, vsync, ...)
 * - VmeIntMask is used to mask IRQ level 1 - 7 coming from the VME Bus
 *
 * NOTE : MFP (level 6) and SCC (level 5) are in fact hardwired to the VME bus, even if MFP and SCC
 * are on the motherboard. So, instead of using SysIntMask to control level 5 and 6 IRQ, we must
 * use VmeIntMask bits 5 and 6.
 * This is why on boot TOS will set :
 *  - SysIntMask = 0x14 to enable hsync and vsync IRQ
 *  - VmeIntMask = 0x60 to enable MFP and SCC IRQ
 */
void 	SCU_UpdatePendingInts_CPU ( void )
{
	pendingInterrupts = ( ( SCU.SysIntState & SCU.SysIntMask ) & 0x9f )		/* keep bits 0-7, except 5 and 6 */
			  | ( ( SCU.VmeIntState & SCU.VmeIntMask ) & 0x60 );		/* keep only bits 5 and 6 */
//fprintf ( stderr , "scu update int state=%x mask=%x vme state=%x mask=%x : out=%x\n" , SCU.SysIntState, SCU.SysIntMask, SCU.VmeIntState , SCU.VmeIntMask , pendingInterrupts );
}

void	SCU_SetIRQ_CPU ( int IntNr )
{
	if ( ( IntNr == 6 ) || ( IntNr == 5 ) )			/* MFP level 6 and SCC level 5 */
		SCU.VmeIntState |= ( 1 << IntNr );
	else
		SCU.SysIntState |= ( 1 << IntNr );

	SCU_UpdatePendingInts_CPU();
}

void	SCU_ClearIRQ_CPU ( int IntNr )
{
	if ( ( IntNr == 6 ) || ( IntNr == 5 ) )			/* MFP level 6 and SCC level 5 */
		SCU.VmeIntState &= ~( 1 << IntNr );
	else
		SCU.SysIntState &= ~( 1 << IntNr );

	SCU_UpdatePendingInts_CPU();
}




/**
 * 0xff8e01 - system interrupt mask
 *
 * Bits 1-7 -> IRQ 1-7, Bit 0 unused
 *
 * IRQ5 & IRQ6 can be serviced either by 68030 or VMEbus master,
 * so they cannot be masked independently by VME & system masks,
 * they will be masked using VmeIntMask and not SysIntMask.
 */
void SCU_SysIntMask_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.SysIntMask;
	SCU_TraceRead ( "sys_int mask" );

	/* Accessing sys int mask resets all pending interrupt requests */
	SCU.SysIntState = 0;
	M68000_Update_intlev ();
}

void SCU_SysIntMask_WriteByte ( void )
{
	SCU_TraceWrite ( "sys_int mask" );
	SCU.SysIntMask = IoMem[IoAccessCurrentAddress];
//SCU.SysIntMask = 0;

	/* Accessing sys int mask resets all pending interrupt requests */
	SCU.SysIntState = 0;
	M68000_Update_intlev ();
}



/**
 * 0xff8e03 - system interrupt status (pending bits before they are masked with SysIntMask above)
 */
void SCU_SysIntState_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.SysIntState;
	SCU_TraceRead ( "sys_int state" );
}

void SCU_SysIntState_WriteByte ( void )
{
	SCU_TraceWrite ( "sys_int state (read only)" );
}



/**
 * 0xff8e05 - SCU system interrupter
 *
 * If bit 0 is set, a level 1 interrupt request to the CPU is triggered (if level 1 int is enabled in SysIntMask)
 * If bit 0 is clear, the level 1 interrupt request is removed
 * Other bits are not used
 */
void SCU_SysInterrupter_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.SysInterrupter;
	SCU_TraceRead ( "sys interrupter" );
}

void SCU_SysInterrupter_WriteByte ( void )
{
	SCU.SysInterrupter = IoMem[IoAccessCurrentAddress];

	if ( SCU.SysInterrupter & 0x1 )
	{
		SCU_TraceWrite ( "sys interrupter, set IRQ1" );
		SCU.SysIntState |= SCU_SYS_INT_LEVEL_SOFT_INT;
	}
	else
	{
		SCU_TraceWrite ( "sys interrupter, clear IRQ1" );
		SCU.SysIntState &= ~SCU_SYS_INT_LEVEL_SOFT_INT;
	}

	/* Update CPU's intlev depending on IRQ1 status */
	M68000_Update_intlev ();
}



/**
 * 0xff8e07 - SCU VME interrupter
 *
 * Bit 0 controls VME IRQ3 setting/clearing
 *
 * NOTE : not implemented at the moment as Hatari doesn't emulate the VME bus itself
 */
void SCU_VmeInterrupter_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.SysInterrupter;
	SCU_TraceRead ( "vme interrupter" );
}

void SCU_VmeInterrupter_WriteByte ( void )
{
	if (IoMem[0xff8e07] & 0x1)
	{
		SCU_TraceWrite ( "vme interrupter, set IRQ3 (ignored)" );
		/* TODO: generate VMEbus level 3 interrupt (IRQ3),
		 * interrupt CPU immediately unless masked off
		 *
		 * System responds to interrupt acknowledge cycle
		 * with the status ID of 0xFF
		 *
		 * Status word supplied by the card during acknowledge
		 * cycle is used as 030 interrupt vector.
		 */
	}
	else
	{
		SCU_TraceWrite ( "vme interrupter, clear IRQ3 (ignored)" );
		/* TODO: clear VMEbus IRQ3 */
	}
}



/**
 * 0xff8e09 - SCU general purpose reg 1
 */
void SCU_GPR1_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.GPR1;
	SCU_TraceRead ( "gpr1" );
}

void SCU_GPR1_WriteByte ( void )
{
	SCU_TraceWrite ( "gpr1" );
	SCU.GPR1 = IoMem[IoAccessCurrentAddress];
}



/**
 * 0xff8e0b - SCU general purpose reg 2
 */
void SCU_GPR2_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.GPR2;
	SCU_TraceRead ( "gpr2" );
}

void SCU_GPR2_WriteByte ( void )
{
	SCU_TraceWrite ( "gpr2" );
	SCU.GPR2 = IoMem[IoAccessCurrentAddress];
}



/**
 * 0xff8e0d - masks interrupts generated by VMEbus sources
 *
 * Bits 1-7 -> IRQ 1-7, Bit 0 unused
 */
void SCU_VmeIntMask_Readyte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.VmeIntMask;
	SCU_TraceRead ( "vme_int mask" );

	/* Accessing vme int mask resets all pending interrupt requests */
	SCU.VmeIntState = 0;
	M68000_Update_intlev ();
}

void SCU_VmeIntMask_WriteByte ( void )
{
	SCU_TraceWrite ( "vme_int mask" );
	SCU.VmeIntMask = IoMem[IoAccessCurrentAddress];

	/* Accessing vme int mask resets all pending interrupt requests */
	SCU.VmeIntState = 0;
	M68000_Update_intlev ();
}



/**
 * 0xff8e0f - VME interrupt status (pending bits before they are masked with VmeIntMask above)
 */
void SCU_VmeIntState_ReadByte ( void )
{
	IoMem[IoAccessCurrentAddress] = SCU.VmeIntState;
	SCU_TraceRead ( "vme_int state" );
}

void SCU_VmeIntState_WriteByte ( void )
{
	SCU_TraceWrite ( "vme_int state (read only)" );
}



/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of SCU/VME variables.
 */
void	SCU_MemorySnapShot_Capture ( bool bSave )
{
	/* Save/Restore details */
	MemorySnapShot_Store(&SCU.SysIntMask, sizeof(SCU.SysIntMask));
	MemorySnapShot_Store(&SCU.SysIntState, sizeof(SCU.SysIntState));
	MemorySnapShot_Store(&SCU.SysInterrupter, sizeof(SCU.SysInterrupter));
	MemorySnapShot_Store(&SCU.VmeIntMask, sizeof(SCU.VmeIntMask));
	MemorySnapShot_Store(&SCU.VmeIntState, sizeof(SCU.VmeIntState));
	MemorySnapShot_Store(&SCU.VmeInterrupter, sizeof(SCU.VmeInterrupter));
	MemorySnapShot_Store(&SCU.GPR1, sizeof(SCU.GPR1));
	MemorySnapShot_Store(&SCU.GPR2, sizeof(SCU.GPR2));
}



/**
 * Show SCU/VME register values
 */
void SCU_Info ( FILE *fp, uint32_t arg )
{
	if (!(Config_IsMachineTT() || Config_IsMachineMegaSTE()))
	{
		fprintf(fp, "No MegaSTE/TT -> no SCU/VME\n\n");
		return;
	}
	fprintf(fp, "$FF8E01.b : system interrupt mask  : 0x%02x\n",      SCU.SysIntMask);
	fprintf(fp, "$FF8E03.b : system interrupt state : 0x%02x (RO)\n", SCU.SysIntState);
	fprintf(fp, "$FF8E05.b : system interrupter     : 0x%02x\n",      SCU.SysInterrupter);
	fprintf(fp, "$FF8E07.b : VME interrupter        : 0x%02x\n",      SCU.VmeInterrupter);
	fprintf(fp, "$FF8E09.b : general register 1     : 0x%02x\n",      SCU.GPR1);
	fprintf(fp, "$FF8E0B.b : general register 2     : 0x%02x\n",      SCU.GPR2);
	fprintf(fp, "$FF8E0D.b : VME interrupt mask     : 0x%02x\n",      SCU.VmeIntMask);
	fprintf(fp, "$FF8E0F.b : VME interrupt state    : 0x%02x (RO)\n", SCU.VmeIntState);
}

