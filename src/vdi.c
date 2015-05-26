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
const char VDI_fileid[] = "Hatari vdi.c : " __DATE__ " " __TIME__;

#include "main.h"
#include "file.h"
#include "gemdos.h"
#include "m68000.h"
#include "options.h"
#include "screen.h"
#include "stMemory.h"
#include "vdi.h"
#include "video.h"
#include "configuration.h"


Uint32 VDI_OldPC;                  /* When call Trap#2, store off PC */

bool bVdiAesIntercept = false;     /* Set to true to trace VDI & AES calls */
bool bUseVDIRes = false;           /* Set to true (if want VDI), or false (ie for games) */
/* defaults */
int VDIRes = 0;                    /* 0,1 or 2 (low, medium, high) */
int VDIWidth = 640;                /* 640x480, 800x600 or 1024x768 */
int VDIHeight = 480;
int VDIPlanes = 4;

static Uint32 LineABase;           /* Line-A structure */
static Uint32 FontBase;            /* Font base, used for 16-pixel high font */

/* Last VDI opcode & vectors */
static Uint16 VDIOpCode;
static Uint32 VDIControl;
static Uint32 VDIIntin;
static Uint32 VDIPtsin;
static Uint32 VDIIntout;
static Uint32 VDIPtsout;
#if ENABLE_TRACING
/* Last AES opcode & vectors */
static Uint32 AESControl;
static Uint32 AESGlobal;
static Uint32 AESIntin;
static Uint32 AESIntout;
static Uint32 AESAddrin;
static Uint32 AESAddrout;
static Uint16 AESOpCode;
#endif


/*-----------------------------------------------------------------------*/
/* Desktop TOS 1.04 and TOS 2.06 desktop configuration files */
static const Uint8 DesktopScript[504] =
{
	0x23,0x61,0x30,0x30,0x30,0x30,0x30,0x30,0x0D,0x0A,0x23,0x62,0x30,0x30,0x30,0x30,
	0x30,0x30,0x0D,0x0A,0x23,0x63,0x37,0x37,0x37,0x30,0x30,0x30,0x37,0x30,0x30,0x30,
	0x36,0x30,0x30,0x30,0x37,0x30,0x30,0x35,0x35,0x32,0x30,0x30,0x35,0x30,0x35,0x35,
	0x35,0x32,0x32,0x32,0x30,0x37,0x37,0x30,0x35,0x35,0x37,0x30,0x37,0x35,0x30,0x35,
	0x35,0x35,0x30,0x37,0x37,0x30,0x33,0x31,0x31,0x31,0x31,0x30,0x33,0x0D,0x0A,0x23,
	0x64,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x0A,
	0x23,0x45,0x20,0x31,0x38,0x20,0x31,0x31,0x20,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,
	0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x37,0x20,0x32,0x36,0x20,0x30,0x43,0x20,
	0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,
	0x32,0x20,0x30,0x42,0x20,0x32,0x36,0x20,0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,
	0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x41,0x20,0x30,0x46,0x20,
	0x31,0x41,0x20,0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,
	0x30,0x20,0x30,0x30,0x20,0x30,0x45,0x20,0x30,0x31,0x20,0x31,0x41,0x20,0x30,0x39,
	0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x4D,0x20,0x30,0x31,0x20,0x30,0x30,0x20,
	0x30,0x30,0x20,0x46,0x46,0x20,0x43,0x20,0x48,0x41,0x52,0x44,0x20,0x44,0x49,0x53,
	0x4B,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x4D,0x20,0x30,0x30,0x20,0x30,0x30,0x20,
	0x30,0x30,0x20,0x46,0x46,0x20,0x41,0x20,0x46,0x4C,0x4F,0x50,0x50,0x59,0x20,0x44,
	0x49,0x53,0x4B,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x4D,0x20,0x30,0x30,0x20,0x30,
	0x31,0x20,0x30,0x30,0x20,0x46,0x46,0x20,0x42,0x20,0x46,0x4C,0x4F,0x50,0x50,0x59,
	0x20,0x44,0x49,0x53,0x4B,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x54,0x20,0x30,0x30,
	0x20,0x30,0x33,0x20,0x30,0x32,0x20,0x46,0x46,0x20,0x20,0x20,0x54,0x52,0x41,0x53,
	0x48,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x46,0x20,0x46,0x46,0x20,0x30,0x34,0x20,
	0x20,0x20,0x40,0x20,0x2A,0x2E,0x2A,0x40,0x20,0x0D,0x0A,0x23,0x44,0x20,0x46,0x46,
	0x20,0x30,0x31,0x20,0x20,0x20,0x40,0x20,0x2A,0x2E,0x2A,0x40,0x20,0x0D,0x0A,0x23,
	0x47,0x20,0x30,0x33,0x20,0x46,0x46,0x20,0x20,0x20,0x2A,0x2E,0x41,0x50,0x50,0x40,
	0x20,0x40,0x20,0x0D,0x0A,0x23,0x47,0x20,0x30,0x33,0x20,0x46,0x46,0x20,0x20,0x20,
	0x2A,0x2E,0x50,0x52,0x47,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x50,0x20,0x30,0x33,
	0x20,0x46,0x46,0x20,0x20,0x20,0x2A,0x2E,0x54,0x54,0x50,0x40,0x20,0x40,0x20,0x0D,
	0x0A,0x23,0x46,0x20,0x30,0x33,0x20,0x30,0x34,0x20,0x20,0x20,0x2A,0x2E,0x54,0x4F,
	0x53,0x40,0x20,0x40,0x20,0x0D,0x0A,0x1A
};

static const Uint8 NewDeskScript[786] =
{
	0x23,0x61,0x30,0x30,0x30,0x30,0x30,0x30,0x0D,0x0A,0x23,0x62,0x30,0x30,0x30,0x30,
	0x30,0x30,0x0D,0x0A,0x23,0x63,0x37,0x37,0x37,0x30,0x30,0x30,0x37,0x30,0x30,0x30,
	0x36,0x30,0x30,0x30,0x37,0x30,0x30,0x35,0x35,0x32,0x30,0x30,0x35,0x30,0x35,0x35,
	0x35,0x32,0x32,0x32,0x30,0x37,0x37,0x30,0x35,0x35,0x37,0x30,0x37,0x35,0x30,0x35,
	0x35,0x35,0x30,0x37,0x37,0x30,0x33,0x31,0x31,0x31,0x31,0x30,0x33,0x0D,0x0A,0x23,
	0x64,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
	0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x0D,0x0A,
	0x23,0x4B,0x20,0x34,0x46,0x20,0x35,0x33,0x20,0x34,0x43,0x20,0x30,0x30,0x20,0x34,
	0x36,0x20,0x34,0x32,0x20,0x34,0x33,0x20,0x35,0x37,0x20,0x34,0x35,0x20,0x35,0x38,
	0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,
	0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,
	0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x35,0x32,0x20,0x30,0x30,0x20,0x30,0x30,
	0x20,0x34,0x44,0x20,0x35,0x36,0x20,0x35,0x30,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,
	0x23,0x45,0x20,0x31,0x38,0x20,0x30,0x31,0x20,0x30,0x30,0x20,0x30,0x36,0x20,0x0D,
	0x0A,0x23,0x51,0x20,0x34,0x31,0x20,0x34,0x30,0x20,0x34,0x33,0x20,0x34,0x30,0x20,
	0x34,0x33,0x20,0x34,0x30,0x20,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,
	0x20,0x30,0x30,0x20,0x30,0x37,0x20,0x32,0x36,0x20,0x30,0x43,0x20,0x30,0x30,0x20,
	0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x32,0x20,0x30,
	0x42,0x20,0x32,0x36,0x20,0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,
	0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x41,0x20,0x30,0x46,0x20,0x31,0x41,0x20,
	0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,
	0x30,0x20,0x30,0x45,0x20,0x30,0x31,0x20,0x31,0x41,0x20,0x30,0x39,0x20,0x30,0x30,
	0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x34,0x20,
	0x30,0x37,0x20,0x32,0x36,0x20,0x30,0x43,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,
	0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x43,0x20,0x30,0x42,0x20,0x32,0x36,
	0x20,0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,
	0x30,0x30,0x20,0x30,0x38,0x20,0x30,0x46,0x20,0x31,0x41,0x20,0x30,0x39,0x20,0x30,
	0x30,0x20,0x40,0x0D,0x0A,0x23,0x57,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x36,
	0x20,0x30,0x31,0x20,0x31,0x41,0x20,0x30,0x39,0x20,0x30,0x30,0x20,0x40,0x0D,0x0A,
	0x23,0x4E,0x20,0x46,0x46,0x20,0x30,0x34,0x20,0x30,0x30,0x30,0x20,0x40,0x20,0x2A,
	0x2E,0x2A,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x44,0x20,0x46,0x46,0x20,0x30,0x31,
	0x20,0x30,0x30,0x30,0x20,0x40,0x20,0x2A,0x2E,0x2A,0x40,0x20,0x40,0x20,0x0D,0x0A,
	0x23,0x47,0x20,0x30,0x33,0x20,0x46,0x46,0x20,0x30,0x30,0x30,0x20,0x2A,0x2E,0x41,
	0x50,0x50,0x40,0x20,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x47,0x20,0x30,0x33,0x20,
	0x46,0x46,0x20,0x30,0x30,0x30,0x20,0x2A,0x2E,0x50,0x52,0x47,0x40,0x20,0x40,0x20,
	0x40,0x20,0x0D,0x0A,0x23,0x59,0x20,0x30,0x33,0x20,0x46,0x46,0x20,0x30,0x30,0x30,
	0x20,0x2A,0x2E,0x47,0x54,0x50,0x40,0x20,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x50,
	0x20,0x30,0x33,0x20,0x46,0x46,0x20,0x30,0x30,0x30,0x20,0x2A,0x2E,0x54,0x54,0x50,
	0x40,0x20,0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x46,0x20,0x30,0x33,0x20,0x30,0x34,
	0x20,0x30,0x30,0x30,0x20,0x2A,0x2E,0x54,0x4F,0x53,0x40,0x20,0x40,0x20,0x40,0x20,
	0x0D,0x0A,0x23,0x4D,0x20,0x30,0x30,0x20,0x30,0x31,0x20,0x30,0x30,0x20,0x46,0x46,
	0x20,0x43,0x20,0x48,0x41,0x52,0x44,0x20,0x44,0x49,0x53,0x4B,0x40,0x20,0x40,0x20,
	0x0D,0x0A,0x23,0x4D,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x30,0x30,0x20,0x46,0x46,
	0x20,0x41,0x20,0x46,0x4C,0x4F,0x50,0x50,0x59,0x20,0x44,0x49,0x53,0x4B,0x40,0x20,
	0x40,0x20,0x0D,0x0A,0x23,0x4D,0x20,0x30,0x31,0x20,0x30,0x30,0x20,0x30,0x30,0x20,
	0x46,0x46,0x20,0x42,0x20,0x46,0x4C,0x4F,0x50,0x50,0x59,0x20,0x44,0x49,0x53,0x4B,
	0x40,0x20,0x40,0x20,0x0D,0x0A,0x23,0x54,0x20,0x30,0x30,0x20,0x30,0x33,0x20,0x30,
	0x32,0x20,0x46,0x46,0x20,0x20,0x20,0x54,0x52,0x41,0x53,0x48,0x40,0x20,0x40,0x20,
	0x0D,0x0A
};

static void VDI_FixDesktopInf(void);


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
static bool VDI_ByteLimit(int *width, int *height, int planes)
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
		fputs("Bad VDI screen ratio / too small size -> use smallest valid size.\n", stderr);
	}
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
		VDIRes = 2;
		VDIPlanes = 1;
		break;
	 case GEMCOLOR_4:
		VDIRes = 1;
		VDIPlanes = 2;
		break;
	 case GEMCOLOR_16:
		VDIRes = 0;
		VDIPlanes = 4;
		break;
	}
	/* screen size in bytes needs to be below limit */
	VDI_ByteLimit(&w, &h, VDIPlanes);

	/* width needs to be aligned to 16 bytes */
	VDIWidth = Opt_ValueAlignMinMax(w, 128/VDIPlanes, MIN_VDI_WIDTH, MAX_VDI_WIDTH);
	/* height needs to be multiple of cell height (either 8 or 16) */
	VDIHeight = Opt_ValueAlignMinMax(h, 16, MIN_VDI_HEIGHT, MAX_VDI_HEIGHT);

	printf("VDI screen: request = %dx%d@%d, result = %dx%d@%d\n",
	       WidthRequest, HeightRequest, VDIPlanes, VDIWidth, VDIHeight, VDIPlanes);

	/* Write resolution to re-boot takes effect with correct bit-depth */
	VDI_FixDesktopInf();
}


#if ENABLE_TRACING

/*-----------------------------------------------------------------------*/

/* AES opcodes which have string args */
static const struct {
	int code;	/* AES opcode */
	int count;	/* number of char * args _first_ in addrin[] */
} AESStrings[] = {
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
static const char* AES_Opcode2Name(Uint16 opcode)
{
	int code = opcode - 10;
	if (code >= 0 && code < ARRAYSIZE(AESName_10) && AESName_10[code])
		return AESName_10[code];
	else
		return "???";
}

/**
 * Output AES call info, including some of args
 */
static void AES_OpcodeInfo(FILE *fp, Uint16 opcode)
{
	int code = opcode - 10;
	fprintf(fp, "AES call %3hd ", opcode);
	if (code >= 0 && code < ARRAYSIZE(AESName_10) && AESName_10[code])
	{
		bool first = true;
		int i, items;

		fprintf(fp, "%s(", AESName_10[code]);

		items = 0;
		/* there are so few of these that linear search is fine */
		for (i = 0; i < ARRAYSIZE(AESStrings); i++)
		{
			/* something that can be shown? */
			if (AESStrings[i].code == opcode)
			{
				items = AESStrings[i].count;
				break;
			}
		}
		/* addrin array size in longs enough for items? */
		if (items > 0 && items <= STMemory_ReadWord(AESControl+SIZE_WORD*3))
		{
			const char *str;
			fputs("addrin: ", fp);
			for (i = 0; i < items; i++)
			{
				if (first)
					first = false;
				else
					fputs(", ", fp);
				str = (const char *)STMemory_STAddrToPointer(STMemory_ReadLong(AESAddrin+SIZE_LONG*i));
				fprintf(fp, "\"%s\"", str);
			}
		}
		/* intin array size in words */
		items = STMemory_ReadWord(AESControl+SIZE_WORD*1);
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
				fprintf(fp, "0x%x", STMemory_ReadWord(AESIntin+SIZE_WORD*i));
			}
		}
		fputs(")\n", fp);
	}
	else
		fputs("???\n", fp);
	fflush(fp);
}

/**
 * If opcodes argument is set, show AES opcode/function name table,
 * otherwise AES vectors information.
 */
void AES_Info(FILE *fp, Uint32 bShowOpcodes)
{
	Uint16 opcode;
	
	if (bShowOpcodes)
	{
		for (opcode = 10; opcode < 0x86; opcode++)
		{
			fprintf(fp, "%02x %-16s", opcode, AES_Opcode2Name(opcode));
			if ((opcode-9) % 4 == 0) fputs("\n", fp);
		}
		return;
	}
	if (!bVdiAesIntercept)
	{
		fputs("VDI/AES interception isn't enabled!\n", fp);
		return;
	}
	if (!AESControl)
	{
		fputs("No traced AES calls!\n", fp);
		return;
	}
	opcode = STMemory_ReadWord(AESControl);
	if (opcode != AESOpCode)
	{
		fputs("AES parameter block contents changed since last call!\n", fp);
		return;
	}

	fputs("Latest AES Parameter block:\n", fp);
	fprintf(fp, "- Opcode: %3hd (%s)\n",
		opcode, AES_Opcode2Name(opcode));

	fprintf(fp, "- Control: %#8x\n", AESControl);
	fprintf(fp, "- Global:  %#8x, %d bytes\n",
		AESGlobal, 2+2+2+4+4+4+4+4+4);
	fprintf(fp, "- Intin:   %#8x, %d words\n",
		AESIntin, STMemory_ReadWord(AESControl+2*1));
	fprintf(fp, "- Intout:  %#8x, %d words\n",
		AESIntout, STMemory_ReadWord(AESControl+2*2));
	fprintf(fp, "- Addrin:  %#8x, %d longs\n",
		AESAddrin, STMemory_ReadWord(AESControl+2*3));
	fprintf(fp, "- Addrout: %#8x, %d longs\n",
		AESAddrout, STMemory_ReadWord(AESControl+2*4));
}


/*-----------------------------------------------------------------------*/

/**
 * Map VDI call opcode/sub-opcode to a VDI function name
 */
static const char* VDI_Opcode2Name(Uint16 opcode, Uint16 subcode)
{
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
		/* 131-233: no known opcodes
		 * 234-255: (Speedo) GDOS opcodes
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
	static const char* names_opcode5_98[] = {
		"v_meta_extents",
		"v_write_meta",
		"vm_filename",
		"???",
		"v_fontinit"
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
		"v_justified"
	};

	if (opcode == 5)
	{
		if (subcode < ARRAYSIZE(names_opcode5)) {
			return names_opcode5[subcode];
		}
		if (subcode >= 98) {
			subcode -= 98;
			if (subcode < ARRAYSIZE(names_opcode5_98)) {
				return names_opcode5_98[subcode];
			}
		}
	}
	else if (opcode == 11)
	{
		if (subcode < ARRAYSIZE(names_opcode11)) {
			return names_opcode11[subcode];
		}
	}
	else if (opcode < ARRAYSIZE(names_0))
	{
		return names_0[opcode];
	}
	else if (opcode >= 100)
	{
		opcode -= 100;
		if (opcode < ARRAYSIZE(names_100))
		{
			return names_100[opcode];
		}
	}
	return "GDOS?";
}

/**
 * If opcodes argument is set, show VDI opcode/function name table,
 * otherwise VDI vectors information.
 */
void VDI_Info(FILE *fp, Uint32 bShowOpcodes)
{
	Uint16 opcode, subcode;

	if (bShowOpcodes)
	{
		Uint16 opcode;
		for (opcode = 0; opcode < 0x84; )
		{
			if (opcode == 0x28)
			{
				fputs("--- GDOS calls? ---\n", fp);
				opcode = 0x64;
			}
			fprintf(fp, "%02x %-16s",
				opcode, VDI_Opcode2Name(opcode, 0));
			if (++opcode % 4 == 0) fputs("\n", fp);
		}
		return;
	}
	if (!bVdiAesIntercept)
	{
		fputs("VDI/AES interception isn't enabled!\n", fp);
		return;
	}
	if (!VDIControl)
	{
		fputs("No traced VDI calls!\n", fp);
		return;
	}
	opcode = STMemory_ReadWord(VDIControl);
	if (opcode != VDIOpCode)
	{
		fputs("VDI parameter block contents changed since last call!\n", fp);
		return;
	}

	fputs("Latest VDI Parameter block:\n", fp);
	subcode = STMemory_ReadWord(VDIControl+2*5);
	fprintf(fp, "- Opcode/Subcode: %hd/%hd (%s)\n",
		opcode, subcode, VDI_Opcode2Name(opcode, subcode));
	fprintf(fp, "- Device handle: %d\n",
		STMemory_ReadWord(VDIControl+2*6));
	fprintf(fp, "- Control: %#8x\n", VDIControl);
	fprintf(fp, "- Ptsin:   %#8x, %d co-ordinate word pairs\n",
		VDIPtsin, STMemory_ReadWord(VDIControl+2*1));
	fprintf(fp, "- Ptsout:  %#8x, %d co-ordinate word pairs\n",
		VDIPtsout, STMemory_ReadWord(VDIControl+2*2));
	fprintf(fp, "- Intin:   %#8x, %d words\n",
		VDIIntin, STMemory_ReadWord(VDIControl+2*3));
	fprintf(fp, "- Intout:  %#8x, %d words\n",
		VDIIntout, STMemory_ReadWord(VDIControl+2*4));
}

#else /* !ENABLE_TRACING */
void AES_Info(FILE *fp, Uint32 bShowOpcodes)
{
	fputs("Hatari isn't configured with ENABLE_TRACING\n", fp);
}
void VDI_Info(FILE *fp, Uint32 bShowOpcodes)
{
	fputs("Hatari isn't configured with ENABLE_TRACING\n", fp);
}
#endif /* !ENABLE_TRACING */


/*-----------------------------------------------------------------------*/
/**
 * Return true for only VDI opcodes that need to be handled at Trap exit.
 */
static inline bool VDI_isWorkstationOpen(Uint16 opcode)
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
	Uint16 call = Regs[REG_D0];
	Uint32 TablePtr = Regs[REG_D1];

#if ENABLE_TRACING
	/* AES call? */
	if (call == 0xC8)
	{
		if ( !STMemory_CheckAreaType ( TablePtr, 24, ABFLAG_RAM ) )
		{
			Log_Printf(LOG_WARN, "AES call failed due to invalid parameter block address 0x%x+%i\n", TablePtr, 24);
			return false;
		}
		/* store values for debugger "info aes" command */
		AESControl = STMemory_ReadLong(TablePtr);
		AESGlobal  = STMemory_ReadLong(TablePtr+4);
		AESIntin   = STMemory_ReadLong(TablePtr+8);
		AESIntout  = STMemory_ReadLong(TablePtr+12);
		AESAddrin  = STMemory_ReadLong(TablePtr+16);
		AESAddrout = STMemory_ReadLong(TablePtr+20);
		AESOpCode  = STMemory_ReadWord(AESControl);
		if (LOG_TRACE_LEVEL(TRACE_OS_AES))
		{
			AES_OpcodeInfo(TraceFile, AESOpCode);
		}
		/* using same special opcode trick doesn't work for
		 * both VDI & AES as AES functions can be called
		 * recursively and VDI calls happen inside AES calls.
		 */
		return false;
	}
#endif

	/* VDI call? */
	if (call == 0x73)
	{
		if ( !STMemory_CheckAreaType ( TablePtr, 20, ABFLAG_RAM ) )
		{
			Log_Printf(LOG_WARN, "VDI call failed due to invalid parameter block address 0x%x+%i\n", TablePtr, 20);
			return false;
		}
		/* store values for extended VDI resolution handling
		 * and debugger "info vdi" command
		 */
		VDIControl = STMemory_ReadLong(TablePtr);
		VDIIntin   = STMemory_ReadLong(TablePtr+4);
		VDIPtsin   = STMemory_ReadLong(TablePtr+8);
		VDIIntout  = STMemory_ReadLong(TablePtr+12);
		VDIPtsout  = STMemory_ReadLong(TablePtr+16);
		VDIOpCode  = STMemory_ReadWord(VDIControl);
#if ENABLE_TRACING
		{
		Uint16 subcode = STMemory_ReadWord(VDIControl+2*5);
		LOG_TRACE(TRACE_OS_VDI, "VDI call %3hd/%3hd (%s)\n",
			  VDIOpCode, subcode,
			  VDI_Opcode2Name(VDIOpCode, subcode));
		}
#endif
		/* Only workstation open needs to be handled at trap return */
		return bUseVDIRes && VDI_isWorkstationOpen(VDIOpCode);
	}

	LOG_TRACE((TRACE_OS_VDI|TRACE_OS_AES), "Trap #2 with D0 = 0x%hX\n", call);
	return false;
}


/*-----------------------------------------------------------------------*/
/**
 * Modify Line-A structure for our VDI resolutions
 */
void VDI_LineA(Uint32 linea, Uint32 fontbase)
{
	LineABase = linea;
	FontBase = fontbase;

	if (bUseVDIRes)
	{
		int cel_ht = STMemory_ReadWord(linea-46);             /* v_cel_ht */
		if (cel_ht <= 0)
		{
			Log_Printf(LOG_WARN, "VDI Line-A init failed due to bad cell height!\n");
			return;
		}
		STMemory_WriteWord(linea-44, (VDIWidth/8)-1);         /* v_cel_mx (cols-1) */
		STMemory_WriteWord(linea-42, (VDIHeight/cel_ht)-1);   /* v_cel_my (rows-1) */
		STMemory_WriteWord(linea-40, cel_ht*((VDIWidth*VDIPlanes)/8));  /* v_cel_wr */

		STMemory_WriteWord(linea-12, VDIWidth);               /* v_rez_hz */
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
	assert(VDI_isWorkstationOpen(VDIOpCode));
	/* not changed between entry and completion? */
	assert(VDIOpCode == STMemory_ReadWord(VDIControl));

	STMemory_WriteWord(VDIIntout, VDIWidth-1);           /* IntOut[0] Width-1 */
	STMemory_WriteWord(VDIIntout+1*2, VDIHeight-1);      /* IntOut[1] Height-1 */
	STMemory_WriteWord(VDIIntout+13*2, 1 << VDIPlanes);  /* IntOut[13] #colors */
	STMemory_WriteWord(VDIIntout+39*2, 512);             /* IntOut[39] #available colors */

	STMemory_WriteWord(LineABase-0x15a*2, VDIWidth-1);   /* WKXRez */
	STMemory_WriteWord(LineABase-0x159*2, VDIHeight-1);  /* WKYRez */

	VDI_LineA(LineABase, FontBase);  /* And modify Line-A structure accordingly */
}


/*-----------------------------------------------------------------------*/
/**
 * Save desktop configuration file for VDI, eg desktop.inf(TOS 1.04) or newdesk.inf(TOS 2.06)
 */
static void VDI_SaveDesktopInf(char *pszFileName, const Uint8 *Script, long ScriptSize)
{
	/* Just save file */
	File_Save(pszFileName, Script, ScriptSize, false);
}


/*-----------------------------------------------------------------------*/
/**
 * Modify exisiting ST desktop configuration files to set resolution(keep user settings)
 */
static void VDI_ModifyDesktopInf(char *pszFileName)
{
	long InfSize;
	Uint8 *pInfData;
	int i;

	/* Load our '.inf' file */
	pInfData = File_Read(pszFileName, &InfSize, NULL);
	if (pInfData)
	{
		/* Scan file for '#E' */
		i = 0;
		while (i < (InfSize-8))
		{
			if ((pInfData[i]=='#') && (pInfData[i+1]=='E'))
			{
				/* Modify resolution */
				pInfData[i+7] = '1'+VDIRes;
				break;
			}

			i++;
		}

		/* And save */
		File_Save(pszFileName, pInfData, InfSize, false);
		/* Free */
		free(pInfData);
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Modify (or create) ST desktop configuration files so VDI boots up in
 * correct color depth
 */
static void VDI_FixDesktopInf(void)
{
	char *szDesktopFileName, *szNewDeskFileName;

	/* Modifying DESKTOP.INF only makes sense when we emulate the GEMDOS
	 * hard disk 'C:' (i.e. the HD we boot from) - if not, simply return */
	if (!GemDOS_IsDriveEmulated(2))
	{
		return;
	}

	szDesktopFileName = malloc(2 * FILENAME_MAX);
	if (!szDesktopFileName)
	{
		perror("VDI_FixDesktopInf");
		return;
	}
	szNewDeskFileName = szDesktopFileName + FILENAME_MAX;

	/* Create filenames for hard-drive */
	GemDOS_CreateHardDriveFileName(2, "\\DESKTOP.INF", szDesktopFileName, FILENAME_MAX);
	GemDOS_CreateHardDriveFileName(2, "\\NEWDESK.INF", szNewDeskFileName, FILENAME_MAX);

	/* First, check if files exist(ie modify or replace) */
	if (!File_Exists(szDesktopFileName))
		VDI_SaveDesktopInf(szDesktopFileName,DesktopScript,sizeof(DesktopScript));
	VDI_ModifyDesktopInf(szDesktopFileName);

	if (!File_Exists(szNewDeskFileName))
		VDI_SaveDesktopInf(szNewDeskFileName,NewDeskScript,sizeof(NewDeskScript));
	VDI_ModifyDesktopInf(szNewDeskFileName);

	free(szDesktopFileName);
}
