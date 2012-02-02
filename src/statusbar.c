/*
  Hatari - statusbar.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  Code to draw statusbar area, floppy leds etc.

  Use like this:
  - Before screen surface is (re-)created Statusbar_SetHeight()
    has to be called with the new screen height. Add the returned
    value to screen height (zero means no statusbar).  After this,
    Statusbar_GetHeight() can be used to retrieve the statusbar size
  - After screen surface is (re-)created, call Statusbar_Init()
    to re-initialize / re-draw the statusbar
  - Call Statusbar_SetFloppyLed() to set floppy drive led ON/OFF,
    or call Statusbar_EnableHDLed() to enabled HD led for a while
  - Whenever screen is redrawn, call Statusbar_Update() to draw the
    updated information to the statusbar (outside of screen locking)
  - If screen redraws may be partial, Statusbar_OverlayRestore()
    needs to be called before locking the screen for drawing and
    Statusbar_OverlayBackup() needs to be called after screen unlocking,
    but before calling Statusbar_Update().  These are needed for
    hiding the overlay drive led when drive leds are turned OFF.
  - If other information shown by Statusbar (TOS version etc) changes,
    call Statusbar_UpdateInfo()

  TODO:
  - re-calculate colors on each update to make sure they're
    correct in Falcon & TT 8-bit palette modes?
  - call Statusbar_AddMessage() from log.c?
*/
const char Statusbar_fileid[] = "Hatari statusbar.c : " __DATE__ " " __TIME__;

#include <assert.h>
#include "main.h"
#include "configuration.h"
#include "screenSnapShot.h"
#include "sdlgui.h"
#include "statusbar.h"
#include "tos.h"
#include "screen.h"
#include "video.h"
#include "wavFormat.h"
#include "ymFormat.h"
#include "avi_record.h"

#define DEBUG 0
#if DEBUG
#define DEBUGPRINT(x) printf x
#else
#define DEBUGPRINT(x)
#endif

#define MAX_DRIVE_LEDS (DRIVE_LED_HD + 1)

/* whether drive leds should be ON and their previous shown state */
static struct {
	bool state;
	bool oldstate;
	Uint32 expire;	/* when to disable led, valid only if >0 && state=TRUE */
	int offset;	/* led x-pos on screen */
} Led[MAX_DRIVE_LEDS];

/* drive leds size & y-pos */
static SDL_Rect LedRect;

/* overlay led size & pos */
static SDL_Rect OverlayLedRect;

/* screen contents left under overlay led */
static SDL_Surface *OverlayUnderside;

static enum {
	OVERLAY_NONE,
	OVERLAY_DRAWN,
	OVERLAY_RESTORED
} bOverlayState;

static SDL_Rect RecLedRect;
static bool bOldRecording;

/* led colors */
static Uint32 LedColorOn, LedColorOff, RecColorOn, RecColorOff;
static Uint32 GrayBg, LedColorBg;


#define MAX_MESSAGE_LEN 35
typedef struct msg_item {
	struct msg_item *next;
	char msg[MAX_MESSAGE_LEN+1];
	Uint32 timeout;	/* msecs, zero=no timeout */
	Uint32 expire;  /* when to expire message */
	bool shown;
} msg_item_t;

static msg_item_t DefaultMessage;
static msg_item_t *MessageList = &DefaultMessage;
static SDL_Rect MessageRect;

/* rect for both frame skip value and fast forward indicator */
static SDL_Rect FrameSkipsRect;
static int nOldFrameSkips;
static int bOldFastForward;


/* screen height above statusbar and height of statusbar below screen */
static int ScreenHeight;
static int StatusbarHeight;


/*-----------------------------------------------------------------------*/
/**
 * Return statusbar height for given width and height
 */
int Statusbar_GetHeightForSize(int width, int height)
{
	if (ConfigureParams.Screen.bShowStatusbar) {
		/* Should check the same thing as SDLGui_SetScreen()
		 * does to decide the font size.
		 */
		if (width >= 640 && height >= (400-24)) {
			return 24;
		} else {
			return 12;
		}
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
/**
 * Set screen height used for statusbar height calculation.
 *
 * Return height of statusbar that should be added to the screen
 * height when screen is (re-)created, or zero if statusbar will
 * not be shown
 */
int Statusbar_SetHeight(int width, int height)
{
	ScreenHeight = height;
	StatusbarHeight = Statusbar_GetHeightForSize(width, height);
	DEBUGPRINT(("Statusbar_SetHeight(%d, %d) -> %d\n", width, height, StatusbarHeight));
	return StatusbarHeight;
}

/*-----------------------------------------------------------------------*/
/**
 * Return height of statusbar set with Statusbar_SetHeight()
 */
int Statusbar_GetHeight(void)
{
	return StatusbarHeight;
}


/*-----------------------------------------------------------------------*/
/**
 * Enable HD drive led, it will be automatically disabled after a while.
 */
void Statusbar_EnableHDLed(void)
{
	/* leds are shown for 1/2 sec after enabling */
	Led[DRIVE_LED_HD].expire = SDL_GetTicks() + 1000/2;
	Led[DRIVE_LED_HD].state = true;
}

/*-----------------------------------------------------------------------*/
/**
 * Set given floppy drive led state, anything enabling led with this
 * needs also to take care of disabling it.
 */
void Statusbar_SetFloppyLed(drive_index_t drive, bool state)
{
	assert(drive == DRIVE_LED_A || drive == DRIVE_LED_B);
	Led[drive].state = state;
}


/*-----------------------------------------------------------------------*/
/**
 * Set overlay led size/pos on given screen to internal Rect
 * and free previous resources.
 */
static void Statusbar_OverlayInit(const SDL_Surface *surf)
{
	int h;
	/* led size/pos needs to be re-calculated in case screen changed */
	h = surf->h / 50;
	OverlayLedRect.w = 2*h;
	OverlayLedRect.h = h;
	OverlayLedRect.x = surf->w - 5*h/2;
	OverlayLedRect.y = h/2;
	/* free previous restore surface if it's incompatible */
	if (OverlayUnderside &&
	    OverlayUnderside->w == OverlayLedRect.w &&
	    OverlayUnderside->h == OverlayLedRect.h &&
	    OverlayUnderside->format->BitsPerPixel == surf->format->BitsPerPixel) {
		SDL_FreeSurface(OverlayUnderside);
		OverlayUnderside = NULL;
	}
	bOverlayState = OVERLAY_NONE;
}

/*-----------------------------------------------------------------------*/
/**
 * (re-)initialize statusbar internal variables for given screen surface
 * (sizes&colors may need to be re-calculated for the new SDL surface)
 * and draw the statusbar background.
 */
void Statusbar_Init(SDL_Surface *surf)
{
	msg_item_t *item;
	SDL_Rect ledbox, sbarbox;
	int i, fontw, fonth, offset;
	const char *text[MAX_DRIVE_LEDS] = { "A:", "B:", "HD:" };

	DEBUGPRINT(("Statusbar_Init()\n"));
	assert(surf);

	/* dark green and light green for leds themselves */
	LedColorOff = SDL_MapRGB(surf->format, 0x00, 0x40, 0x00);
	LedColorOn  = SDL_MapRGB(surf->format, 0x00, 0xe0, 0x00);
	LedColorBg  = SDL_MapRGB(surf->format, 0x00, 0x00, 0x00);
	RecColorOff = SDL_MapRGB(surf->format, 0x40, 0x00, 0x00);
	RecColorOn  = SDL_MapRGB(surf->format, 0xe0, 0x00, 0x00);
	GrayBg      = SDL_MapRGB(surf->format, 0xc0, 0xc0, 0xc0);

	/* disable leds */
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		Led[i].state = Led[i].oldstate = false;
		Led[i].expire = 0;
	}
	Statusbar_OverlayInit(surf);
	
	/* disable statusbar if it doesn't fit to video mode */
	if (surf->h < ScreenHeight + StatusbarHeight) {
		StatusbarHeight = 0;
	}
	if (!StatusbarHeight) {
		DEBUGPRINT(("Doesn't fit <- Statusbar_Init()\n"));
		return;
	}

	/* prepare fonts */
	SDLGui_Init();
	SDLGui_SetScreen(surf);
	SDLGui_GetFontSize(&fontw, &fonth);

	/* video mode didn't match, need to recalculate sizes */
	if (surf->h > ScreenHeight + StatusbarHeight) {
		StatusbarHeight = fonth + 2;
		/* actually statusbar vertical offset */
		ScreenHeight = surf->h - StatusbarHeight;
	} else {
		assert(fonth+2 < StatusbarHeight);
	}

	/* draw statusbar background gray so that text shows */
	sbarbox.x = 0;
	sbarbox.y = surf->h - StatusbarHeight;
	sbarbox.w = surf->w;
	sbarbox.h = StatusbarHeight;
	SDL_FillRect(surf, &sbarbox, GrayBg);

	/* led size */
	LedRect.w = fonth/2;
	LedRect.h = fonth - 4;
	LedRect.y = ScreenHeight + StatusbarHeight/2 - LedRect.h/2;

	/* black box for the leds */
	ledbox = LedRect;
	ledbox.y -= 1;
	ledbox.w += 2;
	ledbox.h += 2;

	offset = fontw;
	MessageRect.y = LedRect.y - 2;
	/* draw led texts and boxes + calculate box offsets */
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		SDLGui_Text(offset, MessageRect.y, text[i]);
		offset += strlen(text[i]) * fontw;
		offset += fontw/2;

		ledbox.x = offset - 1;
		SDL_FillRect(surf, &ledbox, LedColorBg);

		LedRect.x = offset;
		SDL_FillRect(surf, &LedRect, LedColorOff);

		Led[i].offset = offset;
		offset += LedRect.w + fontw;
	}

	/* draw frameskip */
	FrameSkipsRect.x = offset;
	FrameSkipsRect.y = MessageRect.y;
	SDLGui_Text(FrameSkipsRect.x, FrameSkipsRect.y, "FS:");
	FrameSkipsRect.x += 3 * fontw + fontw/2;
	FrameSkipsRect.w = 4 * fontw;
	FrameSkipsRect.h = fonth;

	if(ConfigureParams.System.bFastForward) {
		SDLGui_Text(FrameSkipsRect.x, FrameSkipsRect.y, "0 >>");
	} else {
		SDLGui_Text(FrameSkipsRect.x, FrameSkipsRect.y, "0");
	}

	nOldFrameSkips = 0;

	/* intialize messages */
	MessageRect.x = FrameSkipsRect.x + FrameSkipsRect.w + fontw;
	MessageRect.w = MAX_MESSAGE_LEN * fontw;
	MessageRect.h = fonth;
	for (item = MessageList; item; item = item->next) {
		item->shown = false;
	}

	/* draw recording led box */
	RecLedRect = LedRect;
	RecLedRect.x = surf->w - fontw - RecLedRect.w;
	ledbox.x = RecLedRect.x - 1;
	SDLGui_Text(ledbox.x - 4*fontw - fontw/2, MessageRect.y, "REC:");
	SDL_FillRect(surf, &ledbox, LedColorBg);
	SDL_FillRect(surf, &RecLedRect, RecColorOff);
	bOldRecording = false;

	/* and blit statusbar on screen */
	SDL_UpdateRects(surf, 1, &sbarbox);
	DEBUGPRINT(("Drawn <- Statusbar_Init()\n"));
}


/*-----------------------------------------------------------------------*/
/**
 * Qeueue new statusbar message 'msg' to be shown for 'msecs' milliseconds
 */
void Statusbar_AddMessage(const char *msg, Uint32 msecs)
{
	msg_item_t *item;

	if (!ConfigureParams.Screen.bShowStatusbar) {
		/* no sense in queuing messages that aren't shown */
		return;
	}
	item = calloc(1, sizeof(msg_item_t));
	assert(item);

	item->next = MessageList;
	MessageList = item;

	strncpy(item->msg, msg, MAX_MESSAGE_LEN);
	item->msg[MAX_MESSAGE_LEN] = '\0';
	DEBUGPRINT(("Add message: '%s'\n", item->msg));

	if (msecs) {
		item->timeout = msecs;
	} else {
		/* show items by default for 2.5 secs */
		item->timeout = 2500;
	}
	item->shown = false;
}

/*-----------------------------------------------------------------------*/
/**
 * Write given 'more' string to 'buffer' and return new end of 'buffer'
 */
static char *Statusbar_AddString(char *buffer, const char *more)
{
	while(*more) {
		*buffer++ = *more++;
	}
	return buffer;
}

/*-----------------------------------------------------------------------*/
/**
 * Retrieve/update default statusbar information
 */
void Statusbar_UpdateInfo(void)
{
	char *end = DefaultMessage.msg;

	/* CPU MHz */
	if (ConfigureParams.System.nCpuFreq > 9) {
		*end++ = '0' + ConfigureParams.System.nCpuFreq / 10;
	}
	*end++ = '0' + ConfigureParams.System.nCpuFreq % 10;
	end = Statusbar_AddString(end, "MHz/");

	/* CPU type */
	if(ConfigureParams.System.nCpuLevel > 0) {
		*end++ = '0';
		*end++ = '0' + ConfigureParams.System.nCpuLevel % 10;
		*end++ = '0';
		*end++ = '/';
	}

	/* amount of memory */
	if (ConfigureParams.Memory.nMemorySize > 9) {
		*end++ = '1';
		*end++ = '0' + ConfigureParams.Memory.nMemorySize % 10;
	} else {
		if (ConfigureParams.Memory.nMemorySize) {
			*end++ = '0' + ConfigureParams.Memory.nMemorySize;
		} else {
			end = Statusbar_AddString(end, "0.5");
		}
	}
	end = Statusbar_AddString(end, "MB ");

	/* machine type */
	switch (ConfigureParams.System.nMachineType) {
	case MACHINE_ST:
		end = Statusbar_AddString(end, "ST");
		break;
	case MACHINE_STE:
		end = Statusbar_AddString(end, "STE");
		break;
	case MACHINE_MEGA_STE:
		end = Statusbar_AddString(end, "MegaSTE");
		break;
	case MACHINE_TT:
		end = Statusbar_AddString(end, "TT");
		break;
	case MACHINE_FALCON:
		end = Statusbar_AddString(end, "Falcon");
		break;
	default:
		end = Statusbar_AddString(end, "???");
		break;
	}

	/* TOS type/version */
	if (bIsEmuTOS) {
		end = Statusbar_AddString(end, ", EmuTOS");
	} else {
		end = Statusbar_AddString(end, ", TOS v");
		*end++ = '0' + ((TosVersion & 0xf00) >> 8);
		*end++ = '.';
		*end++ = '0' + ((TosVersion & 0xf0) >> 4);
		*end++ = '0' + (TosVersion & 0xf);
	}
	*end = '\0';

	assert(end - DefaultMessage.msg < MAX_MESSAGE_LEN);
	DEBUGPRINT(("Set default message: '%s'\n", DefaultMessage.msg));
	/* make sure default message gets (re-)drawn when next checked */
	DefaultMessage.shown = false;
}

/*-----------------------------------------------------------------------*/
/**
 * Draw 'msg' centered to the message area
 */
static void Statusbar_DrawMessage(SDL_Surface *surf, const char *msg)
{
	int fontw, fonth, offset;
	SDL_FillRect(surf, &MessageRect, GrayBg);
	if (*msg) {
		SDLGui_GetFontSize(&fontw, &fonth);
		offset = (MessageRect.w - strlen(msg) * fontw) / 2;
		SDLGui_Text(MessageRect.x + offset, MessageRect.y, msg);
	}
	SDL_UpdateRects(surf, 1, &MessageRect);
	DEBUGPRINT(("Draw message: '%s'\n", msg));
}

/*-----------------------------------------------------------------------*/
/**
 * If message's not shown, show it.  If message's timed out,
 * remove it and show next one.
 */
static void Statusbar_ShowMessage(SDL_Surface *surf, Uint32 ticks)
{
	msg_item_t *next;

	if (MessageList->shown) {
		if (!MessageList->expire) {
			/* last/default message newer expires */
			return;
		}
		if (MessageList->expire > ticks) {
			/* not timed out yet */
			return;
		}
		assert(MessageList->next); /* last message shouldn't end here */
		next = MessageList->next;
		free(MessageList);
		MessageList = next;
		/* make sure next message gets shown */
		MessageList->shown = false;
	}
	if (!MessageList->shown) {
		/* not shown yet, show */
		Statusbar_DrawMessage(surf,  MessageList->msg);
		if (MessageList->timeout && !MessageList->expire) {
			MessageList->expire = ticks + MessageList->timeout;
		}
		MessageList->shown = true;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Save the area that will be left under overlay led
 */
void Statusbar_OverlayBackup(SDL_Surface *surf)
{
	if ((StatusbarHeight && ConfigureParams.Screen.bShowStatusbar)
	    || !ConfigureParams.Screen.bShowDriveLed) {
		/* overlay not used with statusbar */
		return;
	}
	assert(surf);
	if (!OverlayUnderside) {
		SDL_Surface *bak;
		SDL_PixelFormat *fmt = surf->format;
		bak = SDL_CreateRGBSurface(surf->flags,
					   OverlayLedRect.w, OverlayLedRect.h,
					   fmt->BitsPerPixel,
					   fmt->Rmask, fmt->Gmask, fmt->Bmask,
					   fmt->Amask);
		assert(bak);
		OverlayUnderside = bak;
	}
	SDL_BlitSurface(surf, &OverlayLedRect, OverlayUnderside, NULL);
}

/*-----------------------------------------------------------------------*/
/**
 * Restore the area left under overlay led
 */
void Statusbar_OverlayRestore(SDL_Surface *surf)
{
	if ((StatusbarHeight && ConfigureParams.Screen.bShowStatusbar)
	    || !ConfigureParams.Screen.bShowDriveLed) {
		/* overlay not used with statusbar */
		return;
	}
	if (bOverlayState == OVERLAY_DRAWN && OverlayUnderside) {
		assert(surf);
		SDL_BlitSurface(OverlayUnderside, NULL, surf, &OverlayLedRect);
		/* this will make the draw function to update this the screen */
		bOverlayState = OVERLAY_RESTORED;
	}
}

/*-----------------------------------------------------------------------*/
/**
 * Draw overlay led
 */
static void Statusbar_OverlayDrawLed(SDL_Surface *surf, Uint32 color)
{
	SDL_Rect rect;
	if (bOverlayState == OVERLAY_DRAWN) {
		/* some led already drawn */
		return;
	}
	bOverlayState = OVERLAY_DRAWN;

	/* enabled led with border */
	rect = OverlayLedRect;
	rect.x += 1;
	rect.y += 1;
	rect.w -= 2;
	rect.h -= 2;
	SDL_FillRect(surf, &OverlayLedRect, LedColorBg);
	SDL_FillRect(surf, &rect, color);
}

/*-----------------------------------------------------------------------*/
/**
 * Draw overlay led onto screen surface if any drives are enabled.
 */
static void Statusbar_OverlayDraw(SDL_Surface *surf)
{
	Uint32 currentticks = SDL_GetTicks();
	int i;

	assert(surf);
	if (bRecordingYM || bRecordingWav || bRecordingAvi) {
		Statusbar_OverlayDrawLed(surf, RecColorOn);
	}
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		if (Led[i].state) {
			if (Led[i].expire && Led[i].expire < currentticks) {
				Led[i].state = false;
				continue;
			}
			Statusbar_OverlayDrawLed(surf, LedColorOn);
			break;
		}
	}
	/* possible state transitions:
	 *   NONE -> DRAWN -> RESTORED -> DRAWN -> RESTORED -> NONE
	 * Other than NONE state needs to be updated on screen
	 */
	switch (bOverlayState) {
	case OVERLAY_RESTORED:
		bOverlayState = OVERLAY_NONE;
	case OVERLAY_DRAWN:
		SDL_UpdateRects(surf, 1, &OverlayLedRect);
		DEBUGPRINT(("Overlay LED = %s\n", bOverlayState==OVERLAY_DRAWN?"ON":"OFF"));
		break;
	case OVERLAY_NONE:
		break;
	}
}


/*-----------------------------------------------------------------------*/
/**
 * Update statusbar information (leds etc) if/when needed.
 * 
 * May not be called when screen is locked (SDL limitation).
 */
void Statusbar_Update(SDL_Surface *surf)
{
	Uint32 color, currentticks;
	SDL_Rect rect;
	int i;

	if (!(StatusbarHeight && ConfigureParams.Screen.bShowStatusbar)) {
		/* not enabled (anymore), show overlay led instead? */
		if (ConfigureParams.Screen.bShowDriveLed) {
			Statusbar_OverlayDraw(surf);
		}
		return;
	}
	assert(surf);
	/* Statusbar_Init() not called before this? */
#if DEBUG
	if (surf->h != ScreenHeight + StatusbarHeight) {
		printf("%d != %d + %d\n", surf->h, ScreenHeight, StatusbarHeight);
	}
#endif
	assert(surf->h == ScreenHeight + StatusbarHeight);

	rect = LedRect;
	currentticks = SDL_GetTicks();
	for (i = 0; i < MAX_DRIVE_LEDS; i++) {
		if (Led[i].expire && Led[i].expire < currentticks) {
			Led[i].state = false;
		}
		if (Led[i].state == Led[i].oldstate) {
			continue;
		}
		Led[i].oldstate = Led[i].state;
		if (Led[i].state) {
			color = LedColorOn;
		} else {
			color = LedColorOff;
		}
		rect.x = Led[i].offset;
		SDL_FillRect(surf, &rect, color);
		SDL_UpdateRects(surf, 1, &rect);
		DEBUGPRINT(("LED[%d] = %s\n", i, Led[i].state?"ON":"OFF"));
	}

	Statusbar_ShowMessage(surf, currentticks);

	if (nOldFrameSkips != nFrameSkips ||
	    bOldFastForward != ConfigureParams.System.bFastForward) {
		char fscount[5];
		int end = 2;

		if (nFrameSkips < 10)
			fscount[0] = '0' + nFrameSkips;
		else
			fscount[0] = 'X';
		fscount[1] = ' ';
		if(ConfigureParams.System.bFastForward) {
			fscount[2] = '>';
			fscount[3] = '>';
			end = 4;
		}
		fscount[end] = '\0';

		SDL_FillRect(surf, &FrameSkipsRect, GrayBg);
		SDLGui_Text(FrameSkipsRect.x, FrameSkipsRect.y, fscount);
		SDL_UpdateRects(surf, 1, &FrameSkipsRect);
		DEBUGPRINT(("FS = %s\n", fscount));
		nOldFrameSkips = nFrameSkips;
		bOldFastForward = ConfigureParams.System.bFastForward;
	}

	if ((bRecordingYM || bRecordingWav || bRecordingAvi)
	    != bOldRecording) {
		bOldRecording = !bOldRecording;
		if (bOldRecording) {
			color = RecColorOn;
		} else {
			color = RecColorOff;
		}
		SDL_FillRect(surf, &RecLedRect, color);
		SDL_UpdateRects(surf, 1, &RecLedRect);
		DEBUGPRINT(("REC = ON\n"));
	}
}
