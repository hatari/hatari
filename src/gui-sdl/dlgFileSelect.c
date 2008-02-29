/*
  Hatari - dlgFileSelect.c
 
  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.
 
  A file selection dialog for the graphical user interface for Hatari.
*/
const char DlgFileSelect_rcsid[] = "Hatari $Id: dlgFileSelect.c,v 1.21 2008-02-29 20:24:21 thothy Exp $";

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>

#include "main.h"
#include "scandir.h"
#include "sdlgui.h"
#include "file.h"
#include "zip.h"


#define SGFS_NUMENTRIES   16            /* How many entries are displayed at once */


#define SGFSDLG_FILENAME  5
#define SGFSDLG_UPDIR     6
#define SGFSDLG_HOMEDIR   7
#define SGFSDLG_ROOTDIR   8
#define SGFSDLG_ENTRY1    11
#define SGFSDLG_ENTRY16   26
#define SGFSDLG_UP        27
#define SGFSDLG_DOWN      28
#define SGFSDLG_SHOWHIDDEN  29
#define SGFSDLG_OKAY      30
#define SGFSDLG_CANCEL    31


#define DLGPATH_SIZE 62
static char dlgpath[DLGPATH_SIZE+1];    /* Path name in the dialog */

#define DLGFNAME_SIZE 56
static char dlgfname[DLGFNAME_SIZE+1];  /* Name of the selected file in the dialog */

#define DLGFILENAMES_SIZE 59
static char dlgfilenames[SGFS_NUMENTRIES][DLGFILENAMES_SIZE+1];  /* Visible file names in the dialog */

/* The dialog data: */
static SGOBJ fsdlg[] =
{
	{ SGBOX, 0, 0, 0,0, 64,25, NULL },
	{ SGTEXT, 0, 0, 25,1, 13,1, "Choose a file" },
	{ SGTEXT, 0, 0, 1,2, 7,1, "Folder:" },
	{ SGTEXT, 0, 0, 1,3, DLGPATH_SIZE,1, dlgpath },
	{ SGTEXT, 0, 0, 1,4, 6,1, "File:" },
	{ SGTEXT, 0, 0, 7,4, DLGFNAME_SIZE,1, dlgfname },
	{ SGBUTTON, 0, 0, 51,1, 4,1, ".." },
	{ SGBUTTON, 0, 0, 56,1, 3,1, "~" },
	{ SGBUTTON, 0, 0, 60,1, 3,1, "/" },
	{ SGBOX, 0, 0, 1,6, 62,16, NULL },
	{ SGBOX, 0, 0, 62,7, 1,14, NULL },
	{ SGTEXT, SG_EXIT, 0, 2,6, DLGFILENAMES_SIZE,1, dlgfilenames[0] },
	{ SGTEXT, SG_EXIT, 0, 2,7, DLGFILENAMES_SIZE,1, dlgfilenames[1] },
	{ SGTEXT, SG_EXIT, 0, 2,8, DLGFILENAMES_SIZE,1, dlgfilenames[2] },
	{ SGTEXT, SG_EXIT, 0, 2,9, DLGFILENAMES_SIZE,1, dlgfilenames[3] },
	{ SGTEXT, SG_EXIT, 0, 2,10, DLGFILENAMES_SIZE,1, dlgfilenames[4] },
	{ SGTEXT, SG_EXIT, 0, 2,11, DLGFILENAMES_SIZE,1, dlgfilenames[5] },
	{ SGTEXT, SG_EXIT, 0, 2,12, DLGFILENAMES_SIZE,1, dlgfilenames[6] },
	{ SGTEXT, SG_EXIT, 0, 2,13, DLGFILENAMES_SIZE,1, dlgfilenames[7] },
	{ SGTEXT, SG_EXIT, 0, 2,14, DLGFILENAMES_SIZE,1, dlgfilenames[8] },
	{ SGTEXT, SG_EXIT, 0, 2,15, DLGFILENAMES_SIZE,1, dlgfilenames[9] },
	{ SGTEXT, SG_EXIT, 0, 2,16, DLGFILENAMES_SIZE,1, dlgfilenames[10] },
	{ SGTEXT, SG_EXIT, 0, 2,17, DLGFILENAMES_SIZE,1, dlgfilenames[11] },
	{ SGTEXT, SG_EXIT, 0, 2,18, DLGFILENAMES_SIZE,1, dlgfilenames[12] },
	{ SGTEXT, SG_EXIT, 0, 2,19, DLGFILENAMES_SIZE,1, dlgfilenames[13] },
	{ SGTEXT, SG_EXIT, 0, 2,20, DLGFILENAMES_SIZE,1, dlgfilenames[14] },
	{ SGTEXT, SG_EXIT, 0, 2,21, DLGFILENAMES_SIZE,1, dlgfilenames[15] },
	{ SGBUTTON, SG_TOUCHEXIT, 0, 62,6, 1,1, "\x01" },           /* Arrow up */
	{ SGBUTTON, SG_TOUCHEXIT, 0, 62,21, 1,1, "\x02" },          /* Arrow down */
	{ SGCHECKBOX, SG_EXIT, SG_SELECTED, 2,23, 18,1, "Show hidden files" },
	{ SGBUTTON, SG_DEFAULT, 0, 32,23, 8,1, "Okay" },
	{ SGBUTTON, SG_CANCEL, 0, 50,23, 8,1, "Cancel" },
	{ -1, 0, 0, 0,0, 0,0, NULL }
};


static int ypos;                        /* First entry number to be displayed */
static BOOL refreshentries;             /* Do we have to update the file names in the dialog? */
static int entries;                     /* How many files are in the actual directory? */


/*-----------------------------------------------------------------------*/
/*
  Update the file name strings in the dialog.
  Returns FALSE if it failed, TRUE on success.
*/
static int DlgFileSelect_RefreshEntries(struct dirent **files, char *path, BOOL browsingzip)
{
	int i;
	char *tempstr = malloc(FILENAME_MAX);

	if (!tempstr)
	{
		perror("DlgFileSelect_RefreshEntries");
		return FALSE;
	}

	/* Copy entries to dialog: */
	for (i=0; i<SGFS_NUMENTRIES; i++)
	{
		if (i+ypos < entries)
		{
			struct stat filestat;
			/* Prepare entries: */
			strcpy(tempstr, "  ");
			strcat(tempstr, files[i+ypos]->d_name);
			File_ShrinkName(dlgfilenames[i], tempstr, DLGFILENAMES_SIZE);
			/* Mark folders: */
			strcpy(tempstr, path);
			strcat(tempstr, files[i+ypos]->d_name);

			if (browsingzip)
			{
				if (File_DoesFileNameEndWithSlash(tempstr))
					dlgfilenames[i][0] = SGFOLDER;    /* Mark folders */
			}
			else
			{
				if( stat(tempstr, &filestat)==0 && S_ISDIR(filestat.st_mode) )
					dlgfilenames[i][0] = SGFOLDER;    /* Mark folders */
				if (ZIP_FileNameIsZIP(tempstr) && browsingzip == FALSE)
					dlgfilenames[i][0] = SGFOLDER;    /* Mark .ZIP archives as folders */
			}
		}
		else
			dlgfilenames[i][0] = 0;  /* Clear entry */
	}

	free(tempstr);
	return TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Remove all hidden files (files with file names that begin with a dot) from
  the list.
*/
static void DlgFileSelect_RemoveHiddenFiles(struct dirent **files)
{
	int i;
	int nActPos = -1;
	int nOldEntries;

	nOldEntries = entries;

	/* Scan list for hidden files and remove them. */
	for (i = 0; i < nOldEntries; i++)
	{
		/* Does file name start with a dot? -> hidden file! */
		if (files[i]->d_name[0] == '.')
		{
			if (nActPos == -1)
				nActPos = i;
			/* Remove file from list: */
			free(files[i]);
			files[i] = NULL;
			entries -= 1;
		}
	}

	/* Now close the gaps in the list: */
	if (nActPos != -1)
	{
		for (i = nActPos; i < nOldEntries; i++)
		{
			if (files[i] != NULL)
			{
				/* Move entry to earlier position: */
				files[nActPos] = files[i];
				files[i] = NULL;
				nActPos += 1;
			}
		}
	}
}


/*-----------------------------------------------------------------------*/
/*
  Prepare to scroll up one entry.
*/
static void DlgFileSelect_ScrollUp(void)
{
	if (ypos > 0)
	{
		--ypos;
		refreshentries = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/*
  Prepare to scroll down one entry.
*/
static void DlgFileSelect_ScrollDown(void)
{
	if (ypos+SGFS_NUMENTRIES < entries)
	{
		++ypos;
		refreshentries = TRUE;
	}
}


/*-----------------------------------------------------------------------*/
/*
  Handle SDL events.
*/
static void DlgFileSelect_HandleSdlEvents(SDL_Event *pEvent)
{
	int oldypos = ypos;
	switch (pEvent->type)
	{
	 case SDL_MOUSEBUTTONDOWN:
		if (pEvent->button.button == SDL_BUTTON_WHEELUP)
			DlgFileSelect_ScrollUp();
		else if (pEvent->button.button == SDL_BUTTON_WHEELDOWN)
			DlgFileSelect_ScrollDown();
		break;
	 case SDL_KEYDOWN:
		switch (pEvent->key.keysym.sym)
		{
		 case SDLK_UP:       DlgFileSelect_ScrollUp(); break;
		 case SDLK_DOWN:     DlgFileSelect_ScrollDown(); break;
		 case SDLK_HOME:     ypos = 0; break;
		 case SDLK_END:      ypos = entries-SGFS_NUMENTRIES; break;
		 case SDLK_PAGEUP:   ypos -= SGFS_NUMENTRIES; break;
		 case SDLK_PAGEDOWN:
			if (ypos+2*SGFS_NUMENTRIES < entries)
				ypos += SGFS_NUMENTRIES;
			else
				ypos = entries-SGFS_NUMENTRIES;
			break;
		 default:
			break;
		}
		break;
	default:
		break;
	}
	if (ypos < 0)
		ypos = 0;
	if (ypos != oldypos)
		refreshentries = TRUE;
}


/*-----------------------------------------------------------------------*/
/*
  Free file entries
*/
static struct dirent **files_free(struct dirent **files)
{
	int i;
	if (files != NULL)
	{
		for(i=0; i<entries; i++)
		{
			free(files[i]);
		}
		free(files);
	}
	return NULL;
}


/*-----------------------------------------------------------------------*/
/*
  Copy to dst src+add if they are below maxlen and return true,
  otherwise return false
*/
static int strcat_maxlen(char *dst, int maxlen, const char *src, const char *add)
{
	int slen, alen;
	slen = strlen(src);
	alen = strlen(add);
	if (slen + alen < maxlen)
	{
		strcpy(dst, src);
		strcpy(dst+slen, add);
		return 1;
	}
	return 0;
}

/*-----------------------------------------------------------------------*/
/*
  Create and return suitable path into zip file
*/
static char* zip_get_path(const char *zipdir, const char *zipfilename, int browsingzip)
{
	if (browsingzip)
	{
		char *zippath;
		zippath = malloc(strlen(zipdir) + strlen(zipfilename) + 1);
		strcpy(zippath, zipdir);
		strcat(zippath, zipfilename);
		return zippath;
	}
	return strdup("");
}

/* string for zip root needs to be empty, check and correct if needed */
static void correct_zip_root(char *zippath)
{
	if (zippath[0] == PATHSEP && !zippath[1])
	{
		zippath[0] = '\0';
	}
}

/*-----------------------------------------------------------------------*/
/*
  Show and process a file selection dialog.
  Returns path/name user selected or NULL if user canceled
  input: zip_path = pointer's pointer to buffer to contain file path
  within a selected zip file, or NULL if browsing zip files is disallowed.
  bAllowNew: TRUE if the user is allowed to insert new file names.
*/
char* SDLGui_FileSelect(const char *path_and_name, char **zip_path, BOOL bAllowNew)
{
	struct dirent **files = NULL;
	char *pStringMem;
	char *retpath;
	char *home, *path, *fname;          /* The actual file and path names */
	BOOL reloaddir = TRUE;              /* Do we have to reload the directory file list? */
	int retbut;
	int oldcursorstate;
	int selection = -1;                 /* The actual selection, -1 if none selected */
	char *zipfilename;                  /* Filename in zip file */
	char *zipdir;
	BOOL browsingzip = FALSE;           /* Are we browsing an archive? */
	zip_dir *zipfiles = NULL;
	SDL_Event sdlEvent;
	struct stat filestat;

	ypos = 0;
	refreshentries = TRUE;
	entries = 0;

	/* Allocate memory for the file and path name strings: */
	pStringMem = malloc(4 * FILENAME_MAX);
	path = pStringMem;
	fname = pStringMem + FILENAME_MAX;
	zipdir = pStringMem + 2 * FILENAME_MAX;
	zipfilename = pStringMem + 3 * FILENAME_MAX;
	zipfilename[0] = 0;

	SDLGui_CenterDlg(fsdlg);
	if (bAllowNew)
	{
		fsdlg[SGFSDLG_FILENAME].type = SGEDITFIELD;
		fsdlg[SGFSDLG_FILENAME].flags |= SG_EXIT;
	}
	else
	{
		fsdlg[SGFSDLG_FILENAME].type = SGTEXT;
		fsdlg[SGFSDLG_FILENAME].flags &= ~SG_EXIT;
	}

	/* Prepare the path and filename variables */
	if (path_and_name && path_and_name[0])
	{
		strncpy(path, path_and_name, FILENAME_MAX);
		path[FILENAME_MAX-1] = '\0';
	}
	else
	{
		if (!getcwd(path, FILENAME_MAX))
		{
			perror("SDLGui_FileSelect");
			return NULL;
		}
	}
	if (stat(path, &filestat) == 0 && S_ISDIR(filestat.st_mode))
	{
		File_AddSlashToEndFileName(path);
		fname[0] = 0;
	}
	else
		File_SplitPath(path, path, fname, NULL);
	File_MakeAbsoluteName(path);
	File_MakeValidPathName(path);
	File_ShrinkName(dlgpath, path, DLGPATH_SIZE);
	File_ShrinkName(dlgfname, fname, DLGFNAME_SIZE);

	/* Save old mouse cursor state and enable cursor */
	oldcursorstate = SDL_ShowCursor(SDL_QUERY);
	if (oldcursorstate == SDL_DISABLE)
		SDL_ShowCursor(SDL_ENABLE);

	do
	{
		if (reloaddir)
		{
			files = files_free(files);

			if (browsingzip)
			{
				files = ZIP_GetFilesDir(zipfiles, zipdir, &entries);
			}
			else
			{
				/* Load directory entries: */
				entries = scandir(path, &files, 0, alphasort);
			}
			
			/* Remove hidden files from the list if necessary: */
			if (!(fsdlg[SGFSDLG_SHOWHIDDEN].state & SG_SELECTED))
			{
				DlgFileSelect_RemoveHiddenFiles(files);
			}

			if (entries < 0)
			{
				fprintf(stderr, "SDLGui_FileSelect: Path not found.\n");
				free(pStringMem);
				return FALSE;
			}

			/* reload always implies refresh */
			reloaddir = FALSE;
			refreshentries = TRUE;
		}/* reloaddir */

		/* Update the file name strings in the dialog? */
		if (refreshentries)
		{
			if (!DlgFileSelect_RefreshEntries(files, path, browsingzip))
			{
				free(pStringMem);
				return FALSE;
			}
			refreshentries = FALSE;
		}

		/* Show dialog: */
		retbut = SDLGui_DoDialog(fsdlg, &sdlEvent);

		/* Has the user clicked on a file or folder? */
		if (retbut>=SGFSDLG_ENTRY1 && retbut<=SGFSDLG_ENTRY16 && retbut-SGFSDLG_ENTRY1+ypos<entries)
		{
			char *tempstr;
			
			tempstr = malloc(FILENAME_MAX);
			if (!tempstr)
			{
				perror("Error while allocating temporary memory in SDLGui_FileSelect()");
				free(pStringMem);
				return FALSE;
			}

			if (browsingzip == TRUE)
			{
				if (!strcat_maxlen(tempstr, FILENAME_MAX,
						   zipdir, files[retbut-SGFSDLG_ENTRY1+ypos]->d_name))
				{
					fprintf(stderr, "SDLGui_FileSelect: Path name too long!\n");
					free(pStringMem);
					return FALSE;
				}
				/* directory? */
				if (File_DoesFileNameEndWithSlash(tempstr))
				{
					/* handle the ../ directory */
					if (strcmp(files[retbut-SGFSDLG_ENTRY1+ypos]->d_name, "../") == 0)
					{
						/* close the zip file */
						if (strcmp(tempstr, "../") == 0)
						{
							/* free zip file entries */
							ZIP_FreeZipDir(zipfiles);
							zipfiles = NULL;
							/* Copy the path name to the dialog */
							File_ShrinkName(dlgpath, path, DLGPATH_SIZE);
							browsingzip = FALSE;
						}
						else
						{
							/* remove "../" and previous dir from path */
							File_PathShorten(tempstr, 2);
							correct_zip_root(tempstr);
							strcpy(zipdir, tempstr);
							File_ShrinkName(dlgpath, zipdir, DLGPATH_SIZE);
						}
					}
					else /* not the "../" directory */
					{
						strcpy(zipdir, tempstr);
						File_ShrinkName(dlgpath, zipdir, DLGPATH_SIZE);
					}
					reloaddir = TRUE;
					/* Copy the path name to the dialog */
					selection = -1;                /* Remove old selection */
					zipfilename[0] = '\0';
					dlgfname[0] = 0;
					ypos = 0;
				}
				else
				{
					/* not dir, select a file in the zip */
					selection = retbut-SGFSDLG_ENTRY1+ypos;
					strcpy(zipfilename, files[selection]->d_name);
					File_ShrinkName(dlgfname, zipfilename, DLGFNAME_SIZE);
				}

			}
			else /* not browsingzip */
			{
				if (!strcat_maxlen(tempstr, FILENAME_MAX,
						   path, files[retbut-SGFSDLG_ENTRY1+ypos]->d_name))
				{
					fprintf(stderr, "SDLGui_FileSelect: Path name too long!\n");
					free(pStringMem);
					return FALSE;
				}
				if (stat(tempstr, &filestat) == 0 && S_ISDIR(filestat.st_mode))
				{
					File_HandleDotDirs(tempstr);
					File_AddSlashToEndFileName(tempstr);
					/* Copy the path name to the dialog */
					File_ShrinkName(dlgpath, tempstr, DLGPATH_SIZE);
					strcpy(path, tempstr);
					reloaddir = TRUE;
					selection = -1;                /* Remove old selection */
					dlgfname[0] = 0;
					ypos = 0;
				}
				else if (ZIP_FileNameIsZIP(tempstr) && zip_path != NULL)
				{
					/* open a zip file */
					zipfiles = ZIP_GetFiles(tempstr);
					if (zipfiles != NULL && browsingzip == FALSE)
					{
						selection = retbut-SGFSDLG_ENTRY1+ypos;
						strcpy(fname, files[selection]->d_name);
						File_ShrinkName(dlgfname, fname, DLGFNAME_SIZE);
						browsingzip = TRUE;
						zipdir[0] = '\0'; /* zip root */
						File_ShrinkName(dlgpath, zipdir, DLGPATH_SIZE);
						reloaddir = TRUE;
						ypos = 0;
					}

				}
				else
				{
					/* Select a file */
					selection = retbut-SGFSDLG_ENTRY1+ypos;
					strcpy(fname, files[selection]->d_name);
					File_ShrinkName(dlgfname, fname, DLGFNAME_SIZE);
				}

			} /* not browsingzip */

			free(tempstr);
		}
		else    /* Has the user clicked on another button? */
		{
			switch(retbut)
			{
			case SGFSDLG_UPDIR:                 /* Change path to parent directory */

				if (browsingzip)
				{
					/* close the zip file? */
					if (!zipdir[0])
					{
						/* free zip file entries */
						ZIP_FreeZipDir(zipfiles);
						browsingzip = FALSE;
						zipfiles = NULL;
						File_ShrinkName(dlgpath, path, DLGPATH_SIZE);
					}
					else
					{
						/* remove last dir from zipdir path */
						File_PathShorten(zipdir, 1);
						correct_zip_root(zipdir);
						File_ShrinkName(dlgpath, zipdir, DLGPATH_SIZE);
						zipfilename[0] = '\0';
					}
				}  /* not a zip file: */
				else
				{
					File_PathShorten(path, 1);
					File_ShrinkName(dlgpath, path, DLGPATH_SIZE);
				}
				reloaddir = TRUE;
				break;

			case SGFSDLG_HOMEDIR:               /* Change to home directory */
				home = getenv("HOME");
				if (home == NULL)
					break;
				if (browsingzip)
				{
					/* free zip file entries */
					ZIP_FreeZipDir(zipfiles);
					zipfiles = NULL;
					browsingzip = FALSE;
				}
				strcpy(path, home);
				File_AddSlashToEndFileName(path);
				File_ShrinkName(dlgpath, path, DLGPATH_SIZE);
				reloaddir = TRUE;
				break;

			case SGFSDLG_ROOTDIR:               /* Change to root directory */
				if (browsingzip)
				{
					/* free zip file entries */
					ZIP_FreeZipDir(zipfiles);
					zipfiles = NULL;
					browsingzip = FALSE;
				}
				strcpy(path, "/");
				strcpy(dlgpath, path);
				reloaddir = TRUE;
				break;
			case SGFSDLG_UP:                    /* Scroll up */
				DlgFileSelect_ScrollUp();
				SDL_Delay(10);
				break;
			case SGFSDLG_DOWN:                  /* Scroll down */
				DlgFileSelect_ScrollDown();
				SDL_Delay(10);
				break;
			case SGFSDLG_FILENAME:              /* User entered new filename */
				strcpy(fname, dlgfname);
				break;
			case SGFSDLG_SHOWHIDDEN:            /* Show/hide hidden files */
				reloaddir = TRUE;
				ypos = 0;
				break;
			case SDLGUI_UNKNOWNEVENT:
				DlgFileSelect_HandleSdlEvents(&sdlEvent);
				break;
			} /* switch */
      
			if (reloaddir)
			{
				/* Remove old selection */
				selection = -1;
				fname[0] = 0;
				dlgfname[0] = 0;
				ypos = 0;
			}
		} /* other button code */


	} /* do */
	while (retbut!=SGFSDLG_OKAY && retbut!=SGFSDLG_CANCEL
	       && retbut!=SDLGUI_QUIT && retbut != SDLGUI_ERROR && !bQuitProgram);

	if (oldcursorstate == SDL_DISABLE)
		SDL_ShowCursor(SDL_DISABLE);

	files_free(files);

	if (browsingzip)
	{
		/* free zip file entries */
		ZIP_FreeZipDir(zipfiles);
		zipfiles = NULL;
	}

	if (retbut == SGFSDLG_OKAY)
	{
		if (zip_path)
			*zip_path = zip_get_path(zipdir, zipfilename, browsingzip);
		retpath = File_MakePath(path, fname, NULL);
	}
	else
		retpath = NULL;
	free(pStringMem);
	return retpath;
}


/*-----------------------------------------------------------------------*/
/* Let user browse for a file, confname is used as default.
 * If bAllowNew is true, user can select new files also.
 * 
 * If no file is selected, or there's some problem with the file,
 * return FALSE and clear dlgname & confname.
 * Otherwise return TRUE, set dlgname & confname to the new file name
 * (dlgname is shrinked & limited to maxlen and confname is assumed
 * to have FILENAME_MAX amount of space).
 */
BOOL SDLGui_FileConfSelect(char *dlgname, char *confname, int maxlen, BOOL bAllowNew)
{
	char *selname;
	
	selname = SDLGui_FileSelect(confname, NULL, bAllowNew);
	if (selname)
	{
		if (!File_DoesFileNameEndWithSlash(selname) &&
		    (bAllowNew || File_Exists(selname)))
		{
			strncpy(confname, selname, FILENAME_MAX);
			confname[FILENAME_MAX-1] = '\0';
			File_ShrinkName(dlgname, selname, maxlen);
		}
		else
		{
			dlgname[0] = confname[0] = 0;
		}
		free(selname);
		return TRUE;
	}
	return FALSE;
}
