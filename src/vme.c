/*
  Hatari - vme.c

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
*/
const char vme_fileid[] = "Hatari vme.c";

#include "main.h"
#include "configuration.h"
#include "ioMem.h"
#include "log.h"
#include "vme.h"
#include "m68000.h"
#include "memorySnapShot.h"



#define IOTAB_OFFSET 0xff8000


typedef struct {
	bool		Enabled;			/* 1 for MegaSTE/TT if SCU/VME is enabled, else 0 */

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
 * SCU trace logging
 */
#ifdef ENABLE_TRACING
static void SCU_Trace(const char *access, const char *info)
{
	int addr = IoAccessCurrentAddress;
	LOG_TRACE(TRACE_VME, "VME: SCU %s (0x%x): 0x%02x pc %x, %s\n", access, addr, IoMem[addr], M68000_GetPC(), info);
}
#else
# define SCU_Trace(a,b)
#endif

/**
 * Generic SCU reg read access function
 */
static void SCU_TraceRead(void)
{
	SCU_Trace("read ", "");
}




void	SCU_SetEnabled ( bool on_off )
{
	SCU.Enabled = on_off;
}



/**
 * Reset SCU/VME registers and interrupts
 */
void	SCU_Reset ( bool bCold )
{
	int addr;

	if ( !SCU.Enabled )
		return;

	/* docs say that all SCU regs are cleared on reset... */
	for (addr = 0xff8e01; addr <= 0xff8e0f; addr += 2)
		IoMem[addr] = 0;
	/* TODO: ...but TOS v2 / v3 crash on MegaSTE / TT
	 * unless gen reg 1 has this value, why?
	 */
	IoMem[0xff8e09] = 0x1;

	/* All the SCU regs are cleared on reset */
	SCU.SysIntMask = 0x00;
	SCU.SysIntState = 0x00;
	SCU.SysInterrupter = 0x00;
	SCU.VmeIntMask = 0x00;
	SCU.VmeIntState = 0x00;
	SCU.VmeInterrupter = 0x00;
	SCU.GPR1 = 0x00;
	SCU.GPR2 = 0x00;

	/* TODO: ...but TOS v2 / v3 crash on MegaSTE / TT
	 * unless gen reg 1 has this value, why?
	 */
	SCU.GPR1 = 0x01;

	/* TODO: clear all SCU interrupts */
}




/*-----------------------------------------------------------------------*/
/**
 * Save/Restore snapshot of SCU/VME variables.
 */
void	SCU_MemorySnapShot_Capture ( bool bSave )
{
	/* Save/Restore details */
	MemorySnapShot_Store(&SCU.Enabled, sizeof(SCU.Enabled));
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
 * 0xff8e01 - masks interrupts generated on the system (board)
 *
 * Bits 1-7 -> IRQ 0-6, Bit 0 unused
 *
 * IRQ5 & IRQ6 can be serviced either by 68030 or VMEbus master,
 * so they cannot be masked independently by VME & system masks.
 */
static void SCU_SysIntMask_WriteByte(void)
{
	SCU_Trace("write", "(system interrupt mask)");
	/* TODO: implement interrupt masking */
}
/**TT030_HW_Ref_Jun-1990.pdf
 * 0xff8e03 - system interrupt status before they are masked with above
 */
static void SCU_SysIntState_ReadByte(void)
{
	SCU_Trace("read ", "(system interrupt state)");
	/* TODO: provide non-masked interrupt status */
}
static void SCU_SysIntState_WriteByte(void)
{
	SCU_Trace("write", "(system interrupt state - READ ONLY)");
}

/**
 * 0xff8e05 - SCU system interrupter
 *
 * Bit 0 controls VME IRQ1 setting/clearing
 */
static void SCU_SysInterrupter_WriteByte(void)
{
	if (IoMem[0xff8e05] & 0x1)
	{
		SCU_Trace("write", "(system interrupter, IRQ1 set)");
		/* TODO: generate auto vectored level 1 interrupt (IRQ1),
		 * interrupt CPU immediately unless masked off
		 */
	}
	else
	{
		SCU_Trace("write", "(system interrupter, IRQ1 clear)");
		/* TODO: clear VMEbus IRQ1 */
	}
}
/**
 * 0xff8e07 - SCU VME interrupter
 *
 * Bit 0 controls VME IRQ3 setting/clearing
 */
static void SCU_VmeInterrupter_WriteByte(void)
{
	if (IoMem[0xff8e07] & 0x1)
	{
		SCU_Trace("write", "(VME interrupter, IRQ3 set)");
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
		SCU_Trace("write", "(VME interrupter, IRQ3 clear)");
		/* TODO: clear VMEbus IRQ3 */
	}
}

/**
 * 0xff8e09 - SCU general purpose reg 1
 */
static void SCU_GenReg1_WriteByte(void)
{
	SCU_Trace("write", "(general reg 1)");
}
/**
 * 0xff8e0b - SCU general purpose reg 2
 */
static void SCU_GenReg2_WriteByte(void)
{
	SCU_Trace("write", "(general reg 2)");
}

/**
 * 0xff8e0d - masks interrupts generated by VMEbus sources
 *
 * Bits 1-7 -> IRQ 0-6, Bit 0 unused
 */
static void SCU_VmeIntMask_WriteByte(void)
{
	SCU_Trace("write", "(VME interrupt mask)");
	/* TODO: implement interrupt masking */
}
/**
 * 0xff8e0f - VME interrupt status before they are masked with above
 */
static void SCU_VmeIntState_ReadByte(void)
{
	SCU_Trace("read ", "(VME interrupt state)");
	/* TODO: provide non-masked interrupt status */
}
static void SCU_VmeIntState_WriteByte(void)
{
	SCU_Trace("write", "(VME interrupt state - READ ONLY)");
}

/**
 * Allow SCU/VME register access and set up tracing
 */
static void SCUSetupTracing(void (**reads)(void), void (**writes)(void))
{
	reads[0xff8e01 - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e03 - IOTAB_OFFSET] = SCU_SysIntState_ReadByte;
	reads[0xff8e05 - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e07 - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e09 - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e0b - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e0d - IOTAB_OFFSET] = SCU_TraceRead;
	reads[0xff8e0f - IOTAB_OFFSET] = SCU_VmeIntState_ReadByte;

	writes[0xff8e01 - IOTAB_OFFSET] = SCU_SysIntMask_WriteByte;      /* SCU system interrupt mask */
	writes[0xff8e03 - IOTAB_OFFSET] = SCU_SysIntState_WriteByte;     /* SCU system interrupt state (RO) */
	writes[0xff8e05 - IOTAB_OFFSET] = SCU_SysInterrupter_WriteByte;  /* SCU system interrupter */
	writes[0xff8e07 - IOTAB_OFFSET] = SCU_VmeInterrupter_WriteByte;  /* SCU VME interrupter */
	writes[0xff8e09 - IOTAB_OFFSET] = SCU_GenReg1_WriteByte;         /* SCU general purpose 1 */
	writes[0xff8e0b - IOTAB_OFFSET] = SCU_GenReg2_WriteByte;         /* SCU general purpose 2 */
	writes[0xff8e0d - IOTAB_OFFSET] = SCU_VmeIntMask_WriteByte;      /* SCU VME interrupt mask */
	writes[0xff8e0f - IOTAB_OFFSET] = SCU_VmeIntState_WriteByte;     /* SCU VME interrupt state (RO) */
}

/**
 * Show SCU/VME register values
 */
void SCU_Info(FILE *fp, uint32_t arg)
{
	if (!(Config_IsMachineTT() || Config_IsMachineMegaSTE()))
	{
		fprintf(fp, "No MegaSTE/TT -> no SCU/VME\n\n");
		return;
	}
	static const char *modes[] = { "none", "dummy" };
	fprintf(fp, "SCU/VME registers ('%s' access mode):\n", modes[ConfigureParams.System.nVMEType]);
	fprintf(fp, "$FF8E01.b : system interrupt mask  : 0x%02x\n",      SCU.SysIntMask);
	fprintf(fp, "$FF8E03.b : system interrupt state : 0x%02x (RO)\n", SCU.SysIntState);
	fprintf(fp, "$FF8E05.b : system interrupter     : 0x%02x\n",      SCU.SysInterrupter);
	fprintf(fp, "$FF8E07.b : VME interrupter        : 0x%02x\n",      SCU.VmeInterrupter);
	fprintf(fp, "$FF8E09.b : general register 1     : 0x%02x\n",      SCU.GPR1);
	fprintf(fp, "$FF8E0B.b : general register 2     : 0x%02x\n",      SCU.GPR2);
	fprintf(fp, "$FF8E0D.b : VME interrupt mask     : 0x%02x\n",      SCU.VmeIntMask);
	fprintf(fp, "$FF8E0F.b : VME interrupt state    : 0x%02x (RO)\n", SCU.VmeIntState);
}

/**
 * Set SCU/VME register accessors based on Hatari configuration
 * VME type setting
 */
void SCU_SetAccess(void (**readtab)(void), void (**writetab)(void))
{
	SCU_SetEnabled ( true );

	/* Allow SCU reg access and support tracing in "dummy" mode */
	if (ConfigureParams.System.nVMEType == VME_TYPE_DUMMY)
		SCUSetupTracing(readtab, writetab);
}

