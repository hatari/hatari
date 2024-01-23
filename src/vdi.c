/*
  Hatari - vdi.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  VDI (Virtual Device Interface) (Trap #2)

  To get higher resolutions on the Desktop, we intercept the VDI/Line-A calls
  and set elements in their structures to the higher width/height/cel/planes.
  We need to intercept the initial Line-A call (which we force into the TOS on
  boot-up) and also the init calls to the VDI.
*/
const char VDI_fileid[] = "Hatari vdi.c";

#include "main.h"
#include "configuration.h"
#include "file.h"
#include "gemdos.h"
#include "inffile.h"
#include "m68000.h"
#include "options.h"
#include "screen.h"
#include "stMemory.h"
#include "tos.h"
#include "vars.h"
#include "vdi.h"
#include "video.h"

/* #undef ENABLE_TRACING */
#define DEBUG 0

uint32_t VDI_OldPC;                  /* When call Trap#2, store off PC */

bool bVdiAesIntercept = false;     /* Set to true to trace VDI & AES calls */
bool bUseVDIRes = false;           /* Set to true (if want VDI), or false (ie for games) */
/* defaults */
int VDIRes = ST_LOW_RES;           /* used in screen.c */
int VDIWidth = 640;                /* 640x480, 800x600 or 1024x768 */
int VDIHeight = 480;
int VDIPlanes = 4;

static uint32_t LineABase;           /* Line-A structure */
static uint32_t FontBase;            /* Font base, used for 16-pixel high font */

/* Last VDI opcode, vectors & their contents (for "info vdi") */
static struct {
	uint32_t Control;
	uint32_t Intin;
	uint32_t Ptsin;
	uint32_t Intout;
	uint32_t Ptsout;
	/* TODO: add arrays for storing above vector contents */
	uint16_t OpCode;
} VDI;

/* Last AES opcode, vectors & their contents (for "info aes") */
static struct {
	uint32_t Control;
	uint32_t Global;
	uint32_t Intin;
	uint32_t Intout;
	uint32_t Addrin;
	uint32_t Addrout;
	/* TODO: add arrays for storing above vector contents */
	uint16_t OpCode;
} AES;


/*-----------------------------------------------------------------------*/
/**
 * Called to reset VDI variables on reset.
 */
void VDI_Reset(void)
{
	/* no VDI calls in progress */
	VDI_OldPC = 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Limit width and height to VDI screen size in bytes, retaining their ratio.
 * Return true if limiting was done.
 */
bool VDI_ByteLimit(int *width, int *height, int planes)
{
	double ratio;
	int size;

	size = (*width)*(*height)*planes/8;
	if (size <= MAX_VDI_BYTES)
		return false;

	ratio = sqrt(MAX_VDI_BYTES) / sqrt(size);
	*width = (*width) * ratio;
	*height = (*height) * ratio;
	if (*width < MIN_VDI_WIDTH || *height < MIN_VDI_HEIGHT)
	{
		*width = MIN_VDI_WIDTH;
		*height = MIN_VDI_HEIGHT;
		Log_Printf(LOG_WARN, "Bad VDI screen ratio / too small size -> use smallest valid size.\n");
	}
	else
		Log_Printf(LOG_WARN, "VDI screen size limited to <= %dKB\n", MAX_VDI_BYTES/1024);
	return true;
}

/*-----------------------------------------------------------------------*/
/**
 * Set Width/Height/BitDepth according to passed GEMCOLOR_2/4/16.
 * Align size when necessary.
 */
void VDI_SetResolution(int GEMColor, int WidthRequest, int HeightRequest)
{
	int w = WidthRequest;
	int h = HeightRequest;

	/* Color depth */
	switch (GEMColor)
	{
	 case GEMCOLOR_2:
		VDIRes = ST_HIGH_RES;
		VDIPlanes = 1;
		break;
	 case GEMCOLOR_4:
		VDIRes = ST_MEDIUM_RES;
		VDIPlanes = 2;
		break;
	 case GEMCOLOR_16:
		VDIRes = ST_LOW_RES;
		VDIPlanes = 4;
		break;
	default:
		fprintf(stderr, "Invalid VDI planes mode request: %d!\n", GEMColor);
		exit(1);
	}
#if DEBUG
	printf("%s v0x%04x, RAM=%dkB\n", bIsEmuTOS ? "EmuTOS" : "TOS", TosVersion,  ConfigureParams.Memory.STRamSize_KB);
#endif
	/* Make sure VDI screen size is acceptable and aligned to TOS requirements */

	w = Opt_ValueAlignMinMax(w, VDI_ALIGN_WIDTH, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
	h = Opt_ValueAlignMinMax(h, VDI_ALIGN_HEIGHT, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);

	/* screen size in bytes needs to be below limit */
	if (VDI_ByteLimit(&w, &h, VDIPlanes))
	{
		/* align again */
		w = Opt_ValueAlignMinMax(w, VDI_ALIGN_WIDTH, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
		h = Opt_ValueAlignMinMax(h, VDI_ALIGN_HEIGHT, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);
	}
	if (w != WidthRequest || h != HeightRequest)
	{
		Log_Printf(LOG_WARN, "VDI screen: request = %dx%d@%d, result = %dx%d@%d\n",
		       WidthRequest, HeightRequest, VDIPlanes, w, h, VDIPlanes);
	}
	else
	{
		Log_Printf(LOG_DEBUG, "VDI screen: %dx%d@%d\n",
			   w, h, VDIPlanes);
	}
	VDIWidth = w;
	VDIHeight = h;
	if (bUseVDIRes)
	{
		/* INF file overriding so that (re-)boot uses correct bit-depth */
		INF_SetVdiMode(VDIRes);
	}
}


/*-----------------------------------------------------------------------*/

/* AES opcode -> function name mapping */
static const char* AESName_10[] = {
	"appl_init",		/* (0x0A) */
	"appl_read",		/* (0x0B) */
	"appl_write",		/* (0x0C) */
	"appl_find",		/* (0x0D) */
	"appl_tplay",		/* (0x0E) */
	"appl_trecord",		/* (0x0F) */
	NULL,			/* (0x10) */
	NULL,			/* (0x11) */
	"appl_search",		/* (0x12) */
	"appl_exit",		/* (0x13) */
	"evnt_keybd",		/* (0x14) */
	"evnt_button",		/* (0x15) */
	"evnt_mesag",		/* (0x16) */
	"evnt_mesag",		/* (0x17) */
	"evnt_timer",		/* (0x18) */
	"evnt_multi",		/* (0x19) */
	"evnt_dclick",		/* (0x1A) */
	NULL,			/* (0x1b) */
	NULL,			/* (0x1c) */
	NULL,			/* (0x1d) */
	"menu_bar",		/* (0x1E) */
	"menu_icheck",		/* (0x1F) */
	"menu_ienable",		/* (0x20) */
	"menu_tnormal",		/* (0x21) */
	"menu_text",		/* (0x22) */
	"menu_register",	/* (0x23) */
	"menu_popup",		/* (0x24) */
	"menu_attach",		/* (0x25) */
	"menu_istart",		/* (0x26) */
	"menu_settings",	/* (0x27) */
	"objc_add",		/* (0x28) */
	"objc_delete",		/* (0x29) */
	"objc_draw",		/* (0x2A) */
	"objc_find",		/* (0x2B) */
	"objc_offset",		/* (0x2C) */
	"objc_order",		/* (0x2D) */
	"objc_edit",		/* (0x2E) */
	"objc_change",		/* (0x2F) */
	"objc_sysvar",		/* (0x30) */
	NULL,			/* (0x31) */
	"form_do",		/* (0x32) */
	"form_dial",		/* (0x33) */
	"form_alert",		/* (0x34) */
	"form_error",		/* (0x35) */
	"form_center",		/* (0x36) */
	"form_keybd",		/* (0x37) */
	"form_button",		/* (0x38) */
	NULL,			/* (0x39) */
	NULL,			/* (0x3a) */
	NULL,			/* (0x3b) */
	NULL,			/* (0x3c) */
	NULL,			/* (0x3d) */
	NULL,			/* (0x3e) */
	NULL,			/* (0x3f) */
	NULL,			/* (0x40) */
	NULL,			/* (0x41) */
	NULL,			/* (0x42) */
	NULL,			/* (0x43) */
	NULL,			/* (0x44) */
	NULL,			/* (0x45) */
	"graf_rubberbox",	/* (0x46) */
	"graf_dragbox",		/* (0x47) */
	"graf_movebox",		/* (0x48) */
	"graf_growbox",		/* (0x49) */
	"graf_shrinkbox",	/* (0x4A) */
	"graf_watchbox",	/* (0x4B) */
	"graf_slidebox",	/* (0x4C) */
	"graf_handle",		/* (0x4D) */
	"graf_mouse",		/* (0x4E) */
	"graf_mkstate",		/* (0x4F) */
	"scrp_read",		/* (0x50) */
	"scrp_write",		/* (0x51) */
	NULL,			/* (0x52) */
	NULL,			/* (0x53) */
	NULL,			/* (0x54) */
	NULL,			/* (0x55) */
	NULL,			/* (0x56) */
	NULL,			/* (0x57) */
	NULL,			/* (0x58) */
	NULL,			/* (0x59) */
	"fsel_input",		/* (0x5A) */
	"fsel_exinput",		/* (0x5B) */
	NULL,			/* (0x5c) */
	NULL,			/* (0x5d) */
	NULL,			/* (0x5e) */
	NULL,			/* (0x5f) */
	NULL,			/* (0x60) */
	NULL,			/* (0x61) */
	NULL,			/* (0x62) */
	NULL,			/* (0x63) */
	"wind_create",		/* (0x64) */
	"wind_open",		/* (0x65) */
	"wind_close",		/* (0x66) */
	"wind_delete",		/* (0x67) */
	"wind_get",		/* (0x68) */
	"wind_set",		/* (0x69) */
	"wind_find",		/* (0x6A) */
	"wind_update",		/* (0x6B) */
	"wind_calc",		/* (0x6C) */
	"wind_new",		/* (0x6D) */
	"rsrc_load",		/* (0x6E) */
	"rsrc_free",		/* (0x6F) */
	"rsrc_gaddr",		/* (0x70) */
	"rsrc_saddr",		/* (0x71) */
	"rsrc_obfix",		/* (0x72) */
	"rsrc_rcfix",		/* (0x73) */
	NULL,			/* (0x74) */
	NULL,			/* (0x75) */
	NULL,			/* (0x76) */
	NULL,			/* (0x77) */
	"shel_read",		/* (0x78) */
	"shel_write",		/* (0x79) */
	"shel_get",		/* (0x7A) */
	"shel_put",		/* (0x7B) */
	"shel_find",		/* (0x7C) */
	"shel_envrn",		/* (0x7D) */
	NULL,			/* (0x7e) */
	NULL,			/* (0x7f) */
	NULL,			/* (0x80) */
	NULL,			/* (0x81) */
	"appl_getinfo"		/* (0x82) */
};

/**
 * Map AES call opcode to an AES function name
 */
static const char* AES_Opcode2Name(uint16_t opcode)
{
	int code = opcode - 10;
	if (code >= 0 && code < ARRAY_SIZE(AESName_10) && AESName_10[code])
		return AESName_10[code];
	else
		return "???";
}

#if ENABLE_TRACING
/**
 * Output AES call info, including some of args
 */
static void AES_OpcodeInfo(FILE *fp, uint16_t opcode)
{
	/* AES opcodes which have string args */
	static const struct {
		int code;	/* AES opcode */
		int count;	/* number of char * args _first_ in addrin[] */
	} strings[] = {
		{ 0x0D, 1 },	/* appl_find() */
		{ 0x12, 1 },	/* appl_search() */
		{ 0x23, 1 },	/* menu_register() */
		{ 0x34, 1 },	/* form_alert() */
		{ 0x51, 1 },	/* scrp_write() */
		{ 0x5A, 2 },	/* fsel_input() */
		{ 0x5B, 3 },	/* fsel_exinput() */
		{ 0x6E, 1 },	/* rsrc_load() */
		{ 0x7C, 1 }	/* shell_find() */
	};
	int code = opcode - 10;
	if (code >= 0 && code < ARRAY_SIZE(AESName_10) && AESName_10[code])
	{
		bool first = true;
		int i, items;

		fprintf(fp, "%s(", AESName_10[code]);

		items = 0;
		/* there are so few of these that linear search is fine */
		for (i = 0; i < ARRAY_SIZE(strings); i++)
		{
			/* something that can be shown? */
			if (strings[i].code == opcode)
			{
				items = strings[i].count;
				break;
			}
		}
		/* addrin array size in longs enough for items? */
		if (items > 0 && items <= STMemory_ReadWord(AES.Control+SIZE_WORD*3))
		{
			const char *str;
			fputs("addrin: ", fp);
			for (i = 0; i < items; i++)
			{
				if (first)
					first = false;
				else
					fputs(", ", fp);
				str = (const char *)STMemory_STAddrToPointer(STMemory_ReadLong(AES.Addrin+SIZE_LONG*i));
				fprintf(fp, "\"%s\"", str);
			}
		}
		/* intin array size in words */
		items = STMemory_ReadWord(AES.Control+SIZE_WORD*1);
		if (items > 0)
		{
			if (!first)
			{
				fputs(", ", fp);
				first = true;
			}
			fputs("intin: ", fp);
			for (i = 0; i < items; i++)
			{
				if (first)
					first = false;
				else
					fputs(",", fp);
				fprintf(fp, "0x%x", STMemory_ReadWord(AES.Intin+SIZE_WORD*i));
			}
		}
		fputs(")\n", fp);
	}
	else
		fputs("???\n", fp);
}
#endif

/**
 * Verify given VDI table pointer and store variables from
 * it for later use. Return true for success
 */
static bool AES_StoreVars(uint32_t TablePtr)
{
	if (!STMemory_CheckAreaType(TablePtr, 24, ABFLAG_RAM))
	{
		Log_Printf(LOG_WARN, "AES param store failed due to invalid parameter block address 0x%x+%i\n", TablePtr, 24);
		return false;
	}
	/* store values for debugger "info aes" command */
	AES.Control = STMemory_ReadLong(TablePtr);
	AES.Global  = STMemory_ReadLong(TablePtr+4);
	AES.Intin   = STMemory_ReadLong(TablePtr+8);
	AES.Intout  = STMemory_ReadLong(TablePtr+12);
	AES.Addrin  = STMemory_ReadLong(TablePtr+16);
	AES.Addrout = STMemory_ReadLong(TablePtr+20);
	/* TODO: copy/convert also above array contents to AES struct */
	AES.OpCode  = STMemory_ReadWord(AES.Control);
	return true;
}

/**
 * If opcodes argument is set, show AES opcode/function name table,
 * otherwise AES vectors information.
 */
void AES_Info(FILE *fp, uint32_t bShowOpcodes)
{
	uint16_t opcode;
	
	if (bShowOpcodes)
	{
		for (opcode = 10; opcode < 0x86; opcode++)
		{
			fprintf(fp, "%02x %-16s", opcode, AES_Opcode2Name(opcode));
			if ((opcode-9) % 4 == 0) fputs("\n", fp);
		}
		return;
	}
	opcode = Vars_GetAesOpcode();
	if (opcode != INVALID_OPCODE)
	{
		/* we're on AES trap -> store new values */
		if (!AES_StoreVars(Regs[REG_D1]))
			return;
	}
	else
	{
#if !ENABLE_TRACING
		fputs("Hatari build with ENABLE_TRACING required to retain AES call info!\n", fp);
		return;
#else
		if (!bVdiAesIntercept)
		{
			fputs("VDI/AES interception isn't enabled!\n", fp);
			return;
		}
		if (!AES.Control)
		{
			fputs("No traced AES calls -> no AES call info!\n", fp);
			return;
		}
		opcode = STMemory_ReadWord(AES.Control);
		if (opcode != AES.OpCode)
		{
			fputs("AES parameter block contents changed since last call!\n", fp);
			return;
		}
#endif
	}
	/* TODO: replace use of STMemory calls with getting the data
	 * from already converted AES.* array members
	 */
	fputs("Latest AES Parameter block:\n", fp);
#if ENABLE_TRACING
	fprintf(fp, "- Opcode:  0x%02hX ", opcode);
	AES_OpcodeInfo(fp, opcode);
#else
	fprintf(fp, "- Opcode:  0x%02hX (%s)\n",
		opcode, AES_Opcode2Name(opcode));
#endif
	fprintf(fp, "- Control: 0x%08x\n", AES.Control);
	fprintf(fp, "- Global:  0x%08x, %d bytes\n",
		AES.Global, 2+2+2+4+4+4+4+4+4);
	fprintf(fp, "- Intin:   0x%08x, %d words\n",
		AES.Intin, STMemory_ReadWord(AES.Control+2*1));
	fprintf(fp, "- Intout:  0x%08x, %d words\n",
		AES.Intout, STMemory_ReadWord(AES.Control+2*2));
	fprintf(fp, "- Addrin:  0x%08x, %d longs\n",
		AES.Addrin, STMemory_ReadWord(AES.Control+2*3));
	fprintf(fp, "- Addrout: 0x%08x, %d longs\n",
		AES.Addrout, STMemory_ReadWord(AES.Control+2*4));
	fflush(fp);
}


/*-----------------------------------------------------------------------*/

/**
 * Map VDI call opcode/sub-opcode to a VDI function name
 */
static const char* VDI_Opcode2Name(uint16_t opcode, uint16_t subcode, uint16_t nintin, const char **extra_info)
{
	unsigned int i;

	static const char* names_0[] = {
		"???",
		"v_opnwk",
		"v_clswk",
		"v_clrwk",
		"v_updwk",
		"",		/* 5: lots of sub opcodes */
		"v_pline",
		"v_pmarker",
		"v_gtext",
		"v_fillarea",	/* sub-opcode 13: v_bez_fill with GDOS */
		"v_cellarray",
		"",		/* 11: lots of sub opcodes */
		"vst_height",
		"vst_rotation",
		"vs_color",
		"vsl_type",
		"vsl_width",
		"vsl_color",
		"vsm_type",
		"vsm_height",
		"vsm_color",
		"vst_font",
		"vst_color",
		"vsf_interior",
		"vsf_style",
		"vsf_color",
		"vq_color",
		"vq_cellarray",
		"vrq/sm_locator",
		"vrq/sm_valuator",
		"vrq/sm_choice",
		"vrq/sm_string",
		"vswr_mode",
		"vsin_mode",
		"???", /* 34 */
		"vql_attributes",
		"vqm_attributes",
		"vqf_attributes",
		"vqt_attributes",
		"vst_alignment"
	};
	static const char* names_100[] = {
		"v_opnvwk",
		"v_clsvwk",
		"vq_extnd",
		"v_contourfill",
		"vsf_perimeter",
		"v_get_pixel",
		"vst_effects",
		"vst_point",
		"vsl_ends",
		"vro_cpyfm",
		"vr_trnfm",
		"vsc_form",
		"vsf_udpat",
		"vsl_udsty",
		"vr_recfl",
		"vqin_mode",
		"vqt_extent",
		"vqt_width",
		"vex_timv",
		"vst_load_fonts",
		"vst_unload_fonts",
		"vrt_cpyfm",
		"v_show_c",
		"v_hide_c",
		"vq_mouse",
		"vex_butv",
		"vex_motv",
		"vex_curv",
		"vq_key_s",
		"vs_clip",
		"vqt_name",
		"vqt_fontinfo"
		/* 139-169: no known opcodes
		 * 170-255: NVDI/Speedo GDOS opcodes
		 */
	};
	static const char* names_opcode5[] = {
		"<no subcode>",
		"vq_chcells",
		"v_exit_cur",
		"v_enter_cur",
		"v_curup",
		"v_curdown",
		"v_curright",
		"v_curleft",
		"v_curhome",
		"v_eeos",
		"v_eeol",
		"vs_curaddress",
		"v_curtext",
		"v_rvon",
		"v_rvoff",
		"vq_curaddress",
		"vq_tabstatus",
		"v_hardcopy",
		"v_dspcur",
		"v_rmcur",
		"v_form_adv",
		"v_output_window",
		"v_clear_disp_list",
		"v_bit_image",
		"vq_scan",
		"v_alpha_text"
	};
	static const char* names_opcode11[] = {
		"<no subcode>",
		"v_bar",
		"v_arc",
		"v_pieslice",
		"v_circle",
		"v_ellipse",
		"v_ellarc",
		"v_ellpie",
		"v_rbox",
		"v_rfbox",
		"v_justified",
		"???",
		"v_bez_on/off",
	};
	static struct {
		unsigned short opcode;
		unsigned short subcode;
		unsigned short nintin;
		const char *name;
		const char *extra_info;
	} const names_other[] = {
		{ 5, 98, 0xffff, "v_meta_extents", "GDOS" },
		{ 5, 99, 0xffff, "v_write_meta", "GDOS" },
		{ 5, 100, 0xffff, "vm_filename", "GDOS" },
		{ 5, 101, 0xffff, "v_offset", "GDOS" },
		{ 5, 102, 0xffff, "v_fontinit", "GDOS" },
		{ 100, 1, 13, "v_opnbm", "EdDI" },
		{ 100, 2, 6, "v_resize_bm", "EdDI" },
		{ 100, 3, 4, "v_open_bm", "EdDI" },
		{ 100, 0xffff, 0xffff, "v_opnvwk", NULL },
		{ 132, 0xffff, 0xffff, "vqt_justified", "PC/GEM" },
		{ 133, 0xffff, 0xffff, "vs_grayoverride", "GEM/3" },
		{ 134, 0xffff, 1, "v_pat_rotate", "GEM/3" },
		{ 134, 0xffff, 0xffff, "vex_wheelv", "Milan" },
		{ 138, 0xffff, 0xffff, "v_setrgb", "NVDI" },
		{ 170, 0, 0xffff, "vr_transfer_bits" },
		{ 171, 0, 0xffff, "vr_clip_rects_by_dst", "NVDI" }, /* NVDI 5.02 */
		{ 171, 1, 0xffff, "vr_clip_rects_by_src", "NVDI" }, /* NVDI 5.02 */
		{ 171, 2, 0xffff, "vr_clip_rects32_by_dst", "NVDI" }, /* NVDI 5.02 */
		{ 171, 3, 0xffff, "vr_clip_rects32_by_src", "NVDI" }, /* NVDI 5.02 */
		{ 180, 0, 0xffff, "v_create_driver_info", "NVDI" }, /* NVDI 5.00 */
		{ 181, 0, 0xffff, "v_delete_driver_info", "NVDI" }, /* NVDI 5.00 */
		{ 182, 0, 0xffff, "v_read_default_settings", "NVDI" }, /* NVDI 5.00 */
		{ 182, 1, 0xffff, "v_write_default_settings", "NVDI" }, /* NVDI 5.00 */
		{ 190, 0, 0xffff, "vqt_char_index", "GDOS" }, /* NVDI 4.00 */
		{ 200, 0, 0xffff, "vst_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 200, 1, 0xffff, "vsf_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 200, 2, 0xffff, "vsl_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 200, 3, 0xffff, "vsm_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 200, 4, 0xffff, "vsr_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 201, 0, 0xffff, "vst_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 201, 1, 0xffff, "vsf_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 201, 2, 0xffff, "vsl_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 201, 3, 0xffff, "vsm_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 201, 4, 0xffff, "vsr_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 202, 0, 0xffff, "vqt_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 202, 1, 0xffff, "vqf_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 202, 2, 0xffff, "vql_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 202, 3, 0xffff, "vqm_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 202, 4, 0xffff, "vqr_fg_color", "GDOS" }, /* NVDI 5.00 */
		{ 203, 0, 0xffff, "vqt_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 203, 1, 0xffff, "vqf_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 203, 2, 0xffff, "vql_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 203, 3, 0xffff, "vqm_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 203, 4, 0xffff, "vqr_bg_color", "GDOS" }, /* NVDI 5.00 */
		{ 204, 0, 0xffff, "v_color2value", "NVDI" }, /* NVDI 5.00 */
		{ 204, 1, 0xffff, "v_value2color", "NVDI" }, /* NVDI 5.00 */
		{ 204, 2, 0xffff, "v_color2nearest", "NVDI" }, /* NVDI 5.00 */
		{ 204, 3, 0xffff, "vq_px_format", "NVDI" }, /* NVDI 5.00 */
		{ 205, 0, 0xffff, "vs_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 205, 1, 0xffff, "vs_ctab_entry", "NVDI" }, /* NVDI 5.00 */
		{ 205, 2, 0xffff, "vs_dflt_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 206, 0, 0xffff, "vq_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 206, 1, 0xffff, "vq_ctab_entry", "NVDI" }, /* NVDI 5.00 */
		{ 206, 2, 0xffff, "vq_ctab_id", "NVDI" }, /* NVDI 5.00 */
		{ 206, 3, 0xffff, "v_ctab_idx2vdi", "NVDI" }, /* NVDI 5.00 */
		{ 206, 4, 0xffff, "v_ctab_vdi2idx", "NVDI" }, /* NVDI 5.00 */
		{ 206, 5, 0xffff, "v_ctab_idx2value", "NVDI" }, /* NVDI 5.00 */
		{ 206, 6, 0xffff, "v_get_ctab_id", "NVDI" }, /* NVDI 5.00 */
		{ 206, 7, 0xffff, "vq_dflt_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 206, 8, 0xffff, "v_create_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 206, 9, 0xffff, "v_delete_ctab", "NVDI" }, /* NVDI 5.00 */
		{ 207, 0, 0xffff, "vs_hilite_color", "NVDI" }, /* NVDI 5.00 */
		{ 207, 1, 0xffff, "vs_min_color", "NVDI" }, /* NVDI 5.00 */
		{ 207, 2, 0xffff, "vs_max_color", "NVDI" }, /* NVDI 5.00 */
		{ 207, 3, 0xffff, "vs_weight_color", "NVDI" }, /* NVDI 5.00 */
		{ 208, 0, 0xffff, "v_create_itab", "NVDI" }, /* NVDI 5.00 */
		{ 208, 1, 0xffff, "v_delete_itab", "NVDI" }, /* NVDI 5.00 */
		{ 209, 0, 0xffff, "vq_hilite_color", "NVDI" }, /* NVDI 5.00 */
		{ 209, 1, 0xffff, "vq_min_color", "NVDI" }, /* NVDI 5.00 */
		{ 209, 2, 0xffff, "vq_max_color", "NVDI" }, /* NVDI 5.00 */
		{ 209, 3, 0xffff, "vq_weight_color", "NVDI" }, /* NVDI 5.00 */
		{ 224, 100, 0xffff, "vs_backmap", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 224, 101, 0xffff, "vs_outmode", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 224, 105, 0xffff, "vs_use_fonts", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 225, 0, 0xffff, "vqt_drv_avail", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 1, 0xffff, "v_set_cachedir", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 2, 0xffff, "v_get_cachedir", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 3, 0xffff, "v_def_cachedir", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 4, 0xffff, "v_clr_cachedir", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 5, 0xffff, "v_delete_cache", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 226, 6, 0xffff, "v_save_cache", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 229, 0, 0xffff, "vqt_xfntinfo", "GDOS" }, /* NVDI 3.02 */
		{ 230, 0, 0xffff, "vst_name", "GDOS" }, /* NVDI 3.02 */
		{ 230, 100, 0xffff, "vqt_name_and_id", "GDOS" }, /* NVDI 3.02 */
		{ 231, 0, 0xffff, "vst_width", "GDOS" }, /* NVDI 3.00 */
		{ 232, 0, 0xffff, "vqt_fontheader", "GDOS" }, /* NVDI 3.00 */
		{ 233, 0, 0xffff, "v_mono_ftext", "Speedo" }, /* SpeedoGDOS 5.1 */
		{ 234, 0, 0xffff, "vqt_trackkern", "GDOS" }, /* NVDI 3.00 */
		{ 235, 0, 0xffff, "vqt_pairkern", "GDOS" }, /* NVDI 3.00 */
		{ 236, 0, 0xffff, "vst_charmap", "GDOS" }, /* NVDI 3.00 */
		{ 236, 0, 0xffff, "vst_map_mode", "GDOS" }, /* NVDI 4.00 */
		{ 237, 0, 0xffff, "vst_kern", "GDOS" }, /* NVDI 3.00 */
		{ 237, 0, 0xffff, "vst_track_offset", "GDOS" }, /* NVDI 3.00 */
		{ 238, 0, 0xffff, "vq_ptsinsz", "GDOS" },
		{ 239, 0, 0xffff, "v_getbitmap_info", "GDOS" }, /* NVDI 3.00 */
		{ 240, 0, 0xffff, "vqt_f_extent", "GDOS" }, /* NVDI 3.00 */
		{ 240, 4200, 0xffff, "vqt_real_extent", "GDOS" }, /* NVDI 3.00 */
		{ 241, 0, 0xffff, "v_ftext", "GDOS" }, /* NVDI 3.00 */
		{ 242, 0, 0xffff, "v_killoutline", "GDOS" }, /* FSM */
		{ 243, 0, 0xffff, "v_getoutline", "GDOS" }, /* NVDI 3.00 */
		{ 243, 1, 0xffff, "v_get_outline", "GDOS" }, /* NVDI 5.00 */
		{ 243, 31, 0xffff, "v_fgetoutline", "Speedo" }, /* SpeedoGDOS 5.0d */
		{ 244, 0, 0xffff, "vst_scratch", "Speedo" },
		{ 245, 0, 0xffff, "vst_error", "Speedo" }, /* SpeedoGDOS 4.00 */
		{ 246, 0, 0xffff, "vst_arbpt", "GDOS" }, /* SpeedoGDOS 4.00 */
		{ 246, 0, 0xffff, "vst_arbpt32", "GDOS" }, /* NVDI 3.00 */
		{ 247, 0, 0xffff, "vqt_advance", "GDOS" }, /* SpeedoGDOS 4.00 */
		{ 247, 0, 0xffff, "vqt_advance32", "GDOS" }, /* NVDI 3.00 */
		{ 248, 0, 0xffff, "vq_devinfo", "GDOS" }, /* NVDI 3.00 */
		{ 248, 0, 0xffff, "vqt_devinfo", "GDOS" }, /* SpeedoGDOS 4.00 */
		{ 248, 4242, 0xffff, "vq_ext_devinfo", "GDOS" }, /* NVDI 3.00 */
		{ 249, 0, 0xffff, "v_savecache", "Speedo" },
		{ 250, 0, 0xffff, "v_loadcache", "Speedo" },
		{ 251, 0, 0xffff, "v_flushcache", "GDOS" }, /* NVDI */
		{ 252, 0, 0xffff, "vst_setsize32", "GDOS" }, /* NVDI 3.00 */
		{ 252, 0, 0xffff, "vst_setsize", "GDOS" }, /* SpeedoGDOS 4.00 */
		{ 253, 0, 0xffff, "vst_skew", "GDOS" }, /* NVDI 3.00 */
		{ 254, 0, 0xffff, "vqt_get_table", "GDOS" }, /* SpeedoGDOS 4.00 */
		{ 255, 0, 0xffff, "vqt_cachesize", "Speedo" }, /* SpeedoGDOS 4.00 */
		{ 255, 100, 0xffff, "vqt_cacheinfo", "Speedo" }, /* SpeedoGDOS 4.00 */
	};

	*extra_info = NULL;
	if (opcode == 5)
	{
		if (subcode < ARRAY_SIZE(names_opcode5)) {
			return names_opcode5[subcode];
		}
	}
	else if (opcode == 11)
	{
		if (subcode < ARRAY_SIZE(names_opcode11)) {
			return names_opcode11[subcode];
		}
	}
	else if (opcode < ARRAY_SIZE(names_0))
	{
		if (opcode == 1 && nintin >= 16)
			return "v_opnprn";
		if (opcode == 6 && subcode == 13)
			return "v_bez";
		if (opcode == 9 && subcode == 13)
			return "v_bez_fill";
		return names_0[opcode];
	}
	else if (opcode > 100)
	{
		uint16_t idx = opcode - 100;
		if (idx < ARRAY_SIZE(names_100))
		{
			return names_100[idx];
		}
	}
	for (i = 0; i < ARRAY_SIZE(names_other); i++)
		if (names_other[i].opcode == opcode)
		{
			if ((names_other[i].subcode == subcode || names_other[i].subcode == 0xffff) &&
				(nintin >= names_other[i].nintin || names_other[i].nintin == 0xffff))
			{
				*extra_info = names_other[i].extra_info;
				return names_other[i].name;
			}
		}
	return "???";
}


/**
 * Verify given VDI table pointer and store variables from
 * it for later use. Return true for success
 */
static bool VDI_StoreVars(uint32_t TablePtr)
{
	if (!STMemory_CheckAreaType(TablePtr, 20, ABFLAG_RAM))
	{
		Log_Printf(LOG_WARN, "VDI param store failed due to invalid parameter block address 0x%x+%i\n", TablePtr, 20);
		return false;
	}
	/* store values for extended VDI resolution handling
	 * and debugger "info vdi" command
	 */
	VDI.Control = STMemory_ReadLong(TablePtr);
	VDI.Intin   = STMemory_ReadLong(TablePtr+4);
	VDI.Ptsin   = STMemory_ReadLong(TablePtr+8);
	VDI.Intout  = STMemory_ReadLong(TablePtr+12);
	VDI.Ptsout  = STMemory_ReadLong(TablePtr+16);
	/* TODO: copy/convert also above array contents to AES struct */
	VDI.OpCode  = STMemory_ReadWord(VDI.Control);
	return true;
}

/**
 * If opcodes argument is set, show VDI opcode/function name table,
 * otherwise VDI vectors information.
 */
void VDI_Info(FILE *fp, uint32_t bShowOpcodes)
{
	uint16_t opcode;
	const char *extra_info;

	if (bShowOpcodes)
	{
		for (opcode = 0; opcode <= 0x84; )
		{
			if (opcode == 0x28)
			{
				fputs("--- GDOS calls? ---\n", fp);
				opcode = 0x64;
			}
			fprintf(fp, "%02x %-16s",
				opcode, VDI_Opcode2Name(opcode, 0, 0, &extra_info));
			if (++opcode % 4 == 0)
				fputs("\n", fp);
		}
		if (opcode % 4)
			fputs("\n", fp);
		return;
	}
	opcode = Vars_GetVdiOpcode();
	if (opcode != INVALID_OPCODE)
	{
		/* we're on VDI trap -> store new values */
		if (!VDI_StoreVars(Regs[REG_D1]))
			return;
	}
	else
	{
#if !ENABLE_TRACING
		fputs("Hatari build with ENABLE_TRACING required to retain VDI call info!\n", fp);
		return;
#else
		if (!bVdiAesIntercept)
		{
			fputs("VDI/AES interception isn't enabled!\n", fp);
			return;
		}
		if (!VDI.Control)
		{
			fputs("No traced VDI calls -> no VDI call info!\n", fp);
			return;
		}
		opcode = STMemory_ReadWord(VDI.Control);
		if (opcode != VDI.OpCode)
		{
			fputs("VDI parameter block contents changed since last call!\n", fp);
			return;
		}
#endif
	}
	/* TODO: replace use of STMemory calls with getting the data
	 * from already converted VDI.* array members
	 */
	fputs("Latest VDI Parameter block:\n", fp);
	uint16_t subcode = STMemory_ReadWord(VDI.Control+2*5);
	uint16_t nintin = STMemory_ReadWord(VDI.Control+2*3);
	const char *name = VDI_Opcode2Name(opcode, subcode, nintin, &extra_info);
	fprintf(fp, "- Opcode/Subcode: 0x%02hX/0x%02hX (%s%s%s)\n",
		opcode, subcode, name, extra_info ? ", " : "", extra_info ? extra_info : "");
	fprintf(fp, "- Device handle: %d\n",
		STMemory_ReadWord(VDI.Control+2*6));
	fprintf(fp, "- Control: 0x%08x\n", VDI.Control);
	fprintf(fp, "- Ptsin:   0x%08x, %d coordinate word pairs\n",
		VDI.Ptsin, STMemory_ReadWord(VDI.Control+2*1));
	fprintf(fp, "- Ptsout:  0x%08x, %d coordinate word pairs\n",
		VDI.Ptsout, STMemory_ReadWord(VDI.Control+2*2));
	fprintf(fp, "- Intin:   0x%08x, %d words\n",
		VDI.Intin, STMemory_ReadWord(VDI.Control+2*3));
	fprintf(fp, "- Intout:  0x%08x, %d words\n",
		VDI.Intout, STMemory_ReadWord(VDI.Control+2*4));
	fflush(fp);
}


/*-----------------------------------------------------------------------*/
/**
 * Return true for only VDI opcodes that need to be handled at Trap exit.
 */
static inline bool VDI_isWorkstationOpen(uint16_t opcode)
{
	if (opcode == 1 || opcode == 100)
		return true;
	else
		return false;
}

/**
 * Check whether this is VDI/AES call and see if we need to re-direct
 * it to our own routines. Return true if VDI_Complete() function
 * needs to be called on OS call exit, otherwise return false.
 *
 * We enter here with Trap #2, so D0 tells which OS call it is (VDI/AES)
 * and D1 is pointer to VDI/AES vectors, i.e. Control, Intin, Ptsin etc...
 */
bool VDI_AES_Entry(void)
{
	uint16_t call = Regs[REG_D0];
#if ENABLE_TRACING
	uint32_t TablePtr = Regs[REG_D1];

	/* AES call? */
	if (call == 0xC8)
	{
		if (!AES_StoreVars(TablePtr))
			return false;
		if (LOG_TRACE_LEVEL(TRACE_OS_AES))
		{
			LOG_TRACE_DIRECT_INIT();
			LOG_TRACE_DIRECT("AES 0x%02hX ", AES.OpCode);
			AES_OpcodeInfo(TraceFile, AES.OpCode);
			fflush(TraceFile);
		}
		/* using same special opcode trick doesn't work for
		 * both VDI & AES as AES functions can be called
		 * recursively and VDI calls happen inside AES calls.
		 */
		return false;
	}
	/* VDI call? */
	if (call == 0x73)
	{
		uint16_t subcode, nintin;
		const char *extra_info, *name;

		if (!VDI_StoreVars(TablePtr))
			return false;
		subcode = STMemory_ReadWord(VDI.Control+2*5);
		nintin = STMemory_ReadWord(VDI.Control+2*3);
		name = VDI_Opcode2Name(VDI.OpCode, subcode, nintin, &extra_info);
		LOG_TRACE(TRACE_OS_VDI, "VDI 0x%02hX/0x%02hX (%s%s%s)\n",
			  VDI.OpCode, subcode, name, extra_info ? ", " : "", extra_info ? extra_info : "");
	}
#endif
	if (call == 0x73)
	{
		/* Only workstation open needs to be handled at trap return */
		return bUseVDIRes && VDI_isWorkstationOpen(VDI.OpCode);
	}

	LOG_TRACE((TRACE_OS_VDI|TRACE_OS_AES), "Trap #2 with D0 = 0x%hX\n", call);
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Modify Line-A structure for our VDI resolutions
 */
void VDI_LineA(uint32_t linea, uint32_t fontbase)
{
	uint32_t fontadr, font1, font2;

	LineABase = linea;
	FontBase = fontbase;

	LOG_TRACE(TRACE_OS_VDI, "VDI mode line-A variable init\n");
	if (bUseVDIRes)
	{
		int cel_ht, cel_wd;

		fontadr = STMemory_ReadLong(linea-0x1cc); /* def_font */
		if (fontadr == 0)
		{
			/* get 8x8 font header */
			font1 = STMemory_ReadLong(fontbase + 4);
			/* get 8x16 font header */
			font2 = STMemory_ReadLong(fontbase + 8);
			/* remove DEFAULT flag from 8x8 font */
			STMemory_WriteWord(font1 + 66, STMemory_ReadWord(font1 + 66) & ~0x01);
			/* remove DEFAULT flag from 8x16 font */
			STMemory_WriteWord(font2 + 66, STMemory_ReadWord(font2 + 66) & ~0x01);
			/* choose new font */
			if (VDIHeight >= 400)
			{
				fontadr = font2;
			} else
			{
				fontadr = font1;
			}
			/* make this new default font */
			STMemory_WriteLong(linea-0x1cc, fontadr);
			/* set DEFAULT flag for chosen font */
			STMemory_WriteWord(fontadr + 66, STMemory_ReadWord(fontadr + 66) | 0x01);
		}
		cel_wd = STMemory_ReadWord(fontadr + 52);
		cel_ht = STMemory_ReadWord(fontadr + 82);
		if (cel_wd <= 0)
		{
			Log_Printf(LOG_WARN, "VDI Line-A init failed due to bad cell width!\n");
			return;
		}
		if (cel_ht <= 0)
		{
			Log_Printf(LOG_WARN, "VDI Line-A init failed due to bad cell height!\n");
			return;
		}

		STMemory_WriteWord(linea-46, cel_ht);                 /* v_cel_ht */
		STMemory_WriteWord(linea-44, (VDIWidth/cel_wd)-1);    /* v_cel_mx (cols-1) */
		STMemory_WriteWord(linea-42, (VDIHeight/cel_ht)-1);   /* v_cel_my (rows-1) */
		STMemory_WriteWord(linea-40, cel_ht*((VDIWidth*VDIPlanes)/8));  /* v_cel_wr */

		STMemory_WriteLong(linea-22, STMemory_ReadLong(fontadr + 76)); /* v_fnt_ad */
		STMemory_WriteWord(linea-18, STMemory_ReadWord(fontadr + 38)); /* v_fnt_nd */
		STMemory_WriteWord(linea-16, STMemory_ReadWord(fontadr + 36)); /* v_fnt_st */
		STMemory_WriteWord(linea-14, STMemory_ReadWord(fontadr + 80)); /* v_fnt_wd */
		STMemory_WriteWord(linea-12, VDIWidth);               /* v_rez_hz */
		STMemory_WriteLong(linea-10, STMemory_ReadLong(fontadr + 72)); /* v_off_ad */
		STMemory_WriteWord(linea-4, VDIHeight);               /* v_rez_vt */
		STMemory_WriteWord(linea-2, (VDIWidth*VDIPlanes)/8);  /* bytes_lin */
		STMemory_WriteWord(linea+0, VDIPlanes);               /* planes */
		STMemory_WriteWord(linea+2, (VDIWidth*VDIPlanes)/8);  /* width */
	}
}


/*-----------------------------------------------------------------------*/
/**
 * This is called on completion of a VDI Trap workstation open,
 * to modify the return structure for extended resolutions.
 */
void VDI_Complete(void)
{
	/* right opcode? */
	assert(VDI_isWorkstationOpen(VDI.OpCode));
	/* not changed between entry and completion? */
	assert(VDI.OpCode == STMemory_ReadWord(VDI.Control));

	STMemory_WriteWord(VDI.Intout, VDIWidth-1);           /* IntOut[0] Width-1 */
	STMemory_WriteWord(VDI.Intout+1*2, VDIHeight-1);      /* IntOut[1] Height-1 */
	STMemory_WriteWord(VDI.Intout+13*2, 1 << VDIPlanes);  /* IntOut[13] #colors */
	STMemory_WriteWord(VDI.Intout+39*2, 512);             /* IntOut[39] #available colors */

	STMemory_WriteWord(LineABase-0x15a*2, VDIWidth-1);   /* WKXRez */
	STMemory_WriteWord(LineABase-0x159*2, VDIHeight-1);  /* WKYRez */

	VDI_LineA(LineABase, FontBase);  /* And modify Line-A structure accordingly */
	LOG_TRACE(TRACE_OS_VDI, "VDI mode Workstation Open return values fix\n");
}
