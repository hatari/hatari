/*
  Hatari - dlgNewDisc.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
*/
char DlgNewDisc_rcsid[] = "Hatari $Id: dlgNewDisc.c,v 1.3 2005-02-13 16:18:52 thothy Exp $";

#include "main.h"
#include "configuration.h"
#include "createBlankImage.h"
#include "dialog.h"
#include "sdlgui.h"
#include "file.h"


#define DLGNEWDISC_DECTRACK   3
#define DLGNEWDISC_TRACKSTR   4
#define DLGNEWDISC_INCTRACK   5
#define DLGNEWDISC_SECTORS9   7
#define DLGNEWDISC_SECTORS10  8
#define DLGNEWDISC_SECTORS11  9
#define DLGNEWDISC_SIDES1     11
#define DLGNEWDISC_SIDES2     12
#define DLGNEWDISC_SAVE       13
#define DLGNEWDISC_EXIT       14

static char szTracks[3];
static int nTracks = 80;

/* The new disc image dialog: */
static SGOBJ newdiscdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 28,12, NULL },
	{ SGTEXT, 0, 0, 6,1, 16,1, "New floppy image" },
	{ SGTEXT, 0, 0, 2,3, 7,1, "Tracks:" },
	{ SGBUTTON, 0, 0, 12,3, 1,1, "\x04" },   /* Left-arrow button  */
	{ SGTEXT, 0, 0, 14,3, 2,1, szTracks },
	{ SGBUTTON, 0, 0, 17,3, 1,1, "\x03" },   /* Right-arrow button */
	{ SGTEXT, 0, 0, 2,5, 8,1, "Sectors:" },
	{ SGRADIOBUT, 0, SG_SELECTED, 12,5, 4,1, "9" },
	{ SGRADIOBUT, 0, 0, 17,5, 4,1, "10" },
	{ SGRADIOBUT, 0, 0, 22,5, 4,1, "11" },
	{ SGTEXT, 0, 0, 2,7, 6,1, "Sides:" },
	{ SGRADIOBUT, 0, 0, 12,7, 4,1, "1" },
	{ SGRADIOBUT, 0, SG_SELECTED, 17,7, 4,1, "2" },
	{ SGBUTTON, 0, 0, 4,10, 8,1, "Create" },
	{ SGBUTTON, 0, 0, 18,10, 6,1, "Back" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process the "new blank disc image" dialog.
*/
void DlgNewDisc_Main(void)
{
	int but;
	char *szNewDiscName;

	sprintf(szTracks, "%i", nTracks);

 	SDLGui_CenterDlg(newdiscdlg);

	/* Initialize disc image name: */
	szNewDiscName = malloc(FILENAME_MAX);
	if (!szNewDiscName)
	{
		perror("DlgNewDisc_Main");
		return;
	}
	strcpy(szNewDiscName, DialogParams.DiscImage.szDiscImageDirectory);
	if (strlen(szNewDiscName) < FILENAME_MAX-12)
		strcat(szNewDiscName, "new_disc.st");

	/* Draw and process the dialog */
	do
	{
		but = SDLGui_DoDialog(newdiscdlg, NULL);
		switch(but)
		{
		 case DLGNEWDISC_DECTRACK:
			if (nTracks > 40)
				nTracks -= 1;
			sprintf(szTracks, "%i", nTracks);
			break;
		 case DLGNEWDISC_INCTRACK:
			if (nTracks < 85)
				nTracks += 1;
			sprintf(szTracks, "%i", nTracks);
			break;
		 case DLGNEWDISC_SAVE:
			if (SDLGui_FileSelect(szNewDiscName, NULL, TRUE))
			{
				if (!File_DoesFileNameEndWithSlash(szNewDiscName))
				{
					int nSectors, nSides;

					/* Get number of sectors */
					if (newdiscdlg[DLGNEWDISC_SECTORS11].state & SG_SELECTED)
						nSectors = 11;
					else if (newdiscdlg[DLGNEWDISC_SECTORS10].state & SG_SELECTED)
						nSectors = 10;
					else
						nSectors = 9;

					/* Get number of sides */
					if (newdiscdlg[DLGNEWDISC_SIDES1].state & SG_SELECTED)
						nSides = 1;
					else
						nSides = 2;

					CreateBlankImage_CreateFile(szNewDiscName, nTracks, nSectors, nSides);
				}
			}
			break;
		}
	}
	while (but != DLGNEWDISC_EXIT && but != SDLGUI_QUIT && !bQuitProgram);

	free(szNewDiscName);
}
