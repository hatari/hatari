/*
  Hatari - dlgFileSelect.c

  This file is distributed under the GNU Public License, version 2 or at
  your option any later version. Read the file gpl.txt for details.

  A file selection dialog for the graphical user interface for Hatari.
*/
char DlgFileSelect_rcsid[] = "Hatari $Id: dlgFileSelect.c,v 1.4 2004-01-13 11:07:19 thothy Exp $";

#include <SDL.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

#include "main.h"
#include "memAlloc.h"
#include "screen.h"
#include "sdlgui.h"
#include "file.h"
#include "zip.h"


#define SGFSDLG_FILENAME  5
#define SGFSDLG_UPDIR     6
#define SGFSDLG_ROOTDIR   7
#define SGFSDLG_ENTRY1    10
#define SGFSDLG_ENTRY16   25
#define SGFSDLG_UP        26
#define SGFSDLG_DOWN      27
#define SGFSDLG_OKAY      28
#define SGFSDLG_CANCEL    29


static char dlgpath[39], dlgfname[33];    /* File and path name in the dialog */
static char dlgfilenames[16][36];

/* The dialog data: */
static SGOBJ fsdlg[] =
{
  { SGBOX, 0, 0, 0,0, 40,25, NULL },
  { SGTEXT, 0, 0, 13,1, 13,1, "Choose a file" },
  { SGTEXT, 0, 0, 1,2, 7,1, "Folder:" },
  { SGTEXT, 0, 0, 1,3, 38,1, dlgpath },
  { SGTEXT, 0, 0, 1,4, 6,1, "File:" },
  { SGTEXT, 0, 0, 7,4, 31,1, dlgfname },
  { SGBUTTON, 0, 0, 31,1, 4,1, ".." },
  { SGBUTTON, 0, 0, 36,1, 3,1, "/" },
  { SGBOX, 0, 0, 1,6, 38,16, NULL },
  { SGBOX, 0, 0, 38,7, 1,14, NULL },
  { SGTEXT, SG_EXIT, 0, 2,6, 35,1, dlgfilenames[0] },
  { SGTEXT, SG_EXIT, 0, 2,7, 35,1, dlgfilenames[1] },
  { SGTEXT, SG_EXIT, 0, 2,8, 35,1, dlgfilenames[2] },
  { SGTEXT, SG_EXIT, 0, 2,9, 35,1, dlgfilenames[3] },
  { SGTEXT, SG_EXIT, 0, 2,10, 35,1, dlgfilenames[4] },
  { SGTEXT, SG_EXIT, 0, 2,11, 35,1, dlgfilenames[5] },
  { SGTEXT, SG_EXIT, 0, 2,12, 35,1, dlgfilenames[6] },
  { SGTEXT, SG_EXIT, 0, 2,13, 35,1, dlgfilenames[7] },
  { SGTEXT, SG_EXIT, 0, 2,14, 35,1, dlgfilenames[8] },
  { SGTEXT, SG_EXIT, 0, 2,15, 35,1, dlgfilenames[9] },
  { SGTEXT, SG_EXIT, 0, 2,16, 35,1, dlgfilenames[10] },
  { SGTEXT, SG_EXIT, 0, 2,17, 35,1, dlgfilenames[11] },
  { SGTEXT, SG_EXIT, 0, 2,18, 35,1, dlgfilenames[12] },
  { SGTEXT, SG_EXIT, 0, 2,19, 35,1, dlgfilenames[13] },
  { SGTEXT, SG_EXIT, 0, 2,20, 35,1, dlgfilenames[14] },
  { SGTEXT, SG_EXIT, 0, 2,21, 35,1, dlgfilenames[15] },
  { SGBUTTON, SG_TOUCHEXIT, 0, 38,6, 1,1, "\x01" },          /* Arrow up */
  { SGBUTTON, SG_TOUCHEXIT, 0, 38,21, 1,1, "\x02" },         /* Arrow down */
  { SGBUTTON, 0, 0, 10,23, 8,1, "Okay" },
  { SGBUTTON, 0, 0, 24,23, 8,1, "Cancel" },
  { -1, 0, 0, 0,0, 0,0, NULL }
};


/*-----------------------------------------------------------------------*/
/*
  Show and process a file selection dialog.
  Returns TRUE if the use selected "okay", FALSE if "cancel".
  input: zip_path = pointer to buffer to contain file path within a selected
  zip file, or NULL if browsing zip files is disallowed.
  bAllowNew: TRUE if the user is allowed to insert new file names.
*/
int SDLGui_FileSelect(char *path_and_name, char *zip_path, BOOL bAllowNew)
{
  int i,n;
  int entries = 0;                             /* How many files are in the actual directory? */
  int ypos = 0;
  struct dirent **files = NULL;
  char path[FILENAME_MAX], fname[128];         /* The actual file and path names */
  BOOL reloaddir = TRUE;                       /* Do we have to reload the directory file list? */
  BOOL refreshentries = TRUE;                  /* Do we have to update the file names in the dialog? */
  int retbut;
  int oldcursorstate;
  int selection = -1;                          /* The actual selection, -1 if none selected */
  char zipfilename[FILENAME_MAX];              /* Filename in zip file */
  char zipdir[FILENAME_MAX];
  BOOL browsingzip = FALSE;                    /* Are we browsing an archive? */
  zip_dir *zipfiles = NULL;

  zipfilename[0] = 0;
  SDLGui_CenterDlg(fsdlg);
  if(bAllowNew)
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
  File_splitpath(path_and_name, path, fname, NULL);
  File_ShrinkName(dlgpath, path, 38);
  File_ShrinkName(dlgfname, fname, 32);

  /* Save old mouse cursor state and enable cursor anyway */
  oldcursorstate = SDL_ShowCursor(SDL_QUERY);
  if( oldcursorstate==SDL_DISABLE )
    SDL_ShowCursor(SDL_ENABLE);

  do
    {
      if( reloaddir )
        {
          if (strlen(path) >= FILENAME_MAX)
            {
              fprintf(stderr, "SDLGui_FileSelect: Path name too long!\n");
              return FALSE;
            }

          /* Free old allocated memory: */
          if( files != NULL )
            {
              for(i=0; i<entries; i++)
                {
                  free(files[i]);
                }
              free(files);
              files = NULL;
            }

          if( browsingzip )
            {
              files = ZIP_GetFilesDir(zipfiles, zipdir, &entries);
            }
          else
            {
              /* Load directory entries: */
              entries = scandir(path, &files, 0, alphasort);
            }

          if(entries < 0)
            {
              fprintf(stderr, "SDLGui_FileSelect: Path not found.\n");
              return FALSE;
            }

          reloaddir = FALSE;
          refreshentries = TRUE;
        }/* reloaddir */


      if( refreshentries )
        {
          char *tempstr = Memory_Alloc(FILENAME_MAX);

          /* Copy entries to dialog: */
          for(i=0; i<16; i++)
            {
              if( i+ypos < entries )
                {
                  struct stat filestat;
                  /* Prepare entries: */
                  strcpy(tempstr, "  ");
                  strcat(tempstr, files[i+ypos]->d_name);
                  File_ShrinkName(dlgfilenames[i], tempstr, 35);
                  /* Mark folders: */
                  strcpy(tempstr, path);
                  strcat(tempstr, files[i+ypos]->d_name);

                  if( browsingzip )
                    {
                      if( tempstr[strlen(tempstr)-1] == '/'  )
                        dlgfilenames[i][0] = SGFOLDER;    /* Mark folders */
                    }
                  else
                    {
                      if( stat(tempstr, &filestat)==0 && S_ISDIR(filestat.st_mode) )
                        dlgfilenames[i][0] = SGFOLDER;    /* Mark folders */
                      if( File_FileNameIsZIP(tempstr) && browsingzip == FALSE)
                        dlgfilenames[i][0] = SGFOLDER;    /* Mark .ZIP archives as folders */
                    }
                }
              else
                dlgfilenames[i][0] = 0;  /* Clear entry */
            }
          refreshentries = FALSE;
          Memory_Free(tempstr);
        }/* refreshentries */

      /* Show dialog: */
      retbut = SDLGui_DoDialog(fsdlg);

      /* Has the user clicked on a file or folder? */
      if( retbut>=SGFSDLG_ENTRY1 && retbut<=SGFSDLG_ENTRY16 && retbut-SGFSDLG_ENTRY1+ypos<entries)
        {
          char *tempstr = Memory_Alloc(FILENAME_MAX);
          struct stat filestat;

          if( browsingzip == TRUE )
            {
              strcpy(tempstr, zipdir);
              strcat(tempstr, files[retbut-SGFSDLG_ENTRY1+ypos]->d_name);
              if(tempstr[strlen(tempstr)-1] == '/')
                {
                  /* handle the ../ directory */
                  if(strcmp(files[retbut-SGFSDLG_ENTRY1+ypos]->d_name, "../") == 0)
                    {
                      /* close the zip file */
                      if( strcmp(tempstr, "../") == 0 )
                        {
                          reloaddir = refreshentries = TRUE;
                          /* free zip file entries */
                          ZIP_FreeZipDir(zipfiles);
                          zipfiles = NULL;
                          /* Copy the path name to the dialog */
                          File_ShrinkName(dlgpath, path, 38);
                          browsingzip = FALSE;
                        }
                      else
                        {
                          i=strlen(tempstr)-1; n=0;
                          while(i > 0 && n < 3) if( tempstr[i--] == '/' )n++;
                          if(tempstr[i+1] == '/') tempstr[i+2] = '\0';
                          else tempstr[0] = '\0';

                          strcpy(zipdir, tempstr);
                          File_ShrinkName(dlgpath, zipdir, 38);
                        }
                    }
                  else /* not the "../" directory */
                    {
                      strcpy(zipdir, tempstr);
                      File_ShrinkName(dlgpath, zipdir, 38);
                    }
                  reloaddir = TRUE;
                  /* Copy the path name to the dialog */
                  selection = -1;                /* Remove old selection */
                  zipfilename[0] = '\0';
                  dlgfname[0] = 0;
                  ypos = 0;

                } else {
                  /* Select a file in the zip */
                  selection = retbut-SGFSDLG_ENTRY1+ypos;
                  strcpy(zipfilename, files[selection]->d_name);
                  File_ShrinkName(dlgfname, zipfilename, 32);
                }

            } /* if browsingzip */
          else
            {
              strcpy(tempstr, path);
              strcat(tempstr, files[retbut-SGFSDLG_ENTRY1+ypos]->d_name);
              if( stat(tempstr, &filestat)==0 && S_ISDIR(filestat.st_mode) )
                {
                  /* Set the new directory */
                  strcpy(path, tempstr);
                  if( strlen(path)>=3 )
                    {
                      if(path[strlen(path)-2]=='/' && path[strlen(path)-1]=='.')
                        path[strlen(path)-2] = 0;  /* Strip a single dot at the end of the path name */
                      if(path[strlen(path)-3]=='/' && path[strlen(path)-2]=='.' && path[strlen(path)-1]=='.')
                        {
                          /* Handle the ".." folder */
                          char *ptr;
                          if( strlen(path)==3 )
                            path[1] = 0;
                          else
                            {
                              path[strlen(path)-3] = 0;
                              ptr = strrchr(path, '/');
                              if(ptr)  *(ptr+1) = 0;
                            }
                        }
                    }
                  File_AddSlashToEndFileName(path);
                  reloaddir = TRUE;
                  /* Copy the path name to the dialog */
                  File_ShrinkName(dlgpath, path, 38);
                  selection = -1;                /* Remove old selection */
                  dlgfname[0] = 0;
                  ypos = 0;
                }
          else if ( File_FileNameIsZIP(tempstr) && zip_path != NULL )
            {
              /* open a zip file */
              zipfiles = ZIP_GetFiles(tempstr);
              if( zipfiles != NULL && browsingzip == FALSE )
                {
                  selection = retbut-SGFSDLG_ENTRY1+ypos;
                  strcpy(fname, files[selection]->d_name);
                  File_ShrinkName(dlgfname, fname, 32);
                  browsingzip=TRUE;
                  strcpy(zipdir, "");
                  File_ShrinkName(dlgpath, zipdir, 38);
                  reloaddir = refreshentries = TRUE;
                  ypos = 0;
                }

            } else {
              /* Select a file */
              selection = retbut-SGFSDLG_ENTRY1+ypos;
              strcpy(fname, files[selection]->d_name);
              File_ShrinkName(dlgfname, fname, 32);
            }

            } /* not browsingzip */

          Memory_Free(tempstr);
        }
      else    /* Has the user clicked on another button? */
        {
          switch(retbut)
          {
            case SGFSDLG_UPDIR:                 /* Change path to parent directory */

              if( browsingzip )
              {
              /* close the zip file */
              if( strcmp(zipdir, "") == 0 )
                {
                  reloaddir = refreshentries = TRUE;
                  /* free zip file entries */
                  ZIP_FreeZipDir(zipfiles);
                  zipfiles = NULL;
                  /* Copy the path name to the dialog */
                  File_ShrinkName(dlgpath, path, 38);
                  browsingzip = FALSE;
                  reloaddir = TRUE;
                  selection = -1;                 /* Remove old selection */
                  fname[0] = 0;
                  dlgfname[0] = 0;
                  ypos = 0;
                }
              else
                {
                  i=strlen(zipdir)-1; n=0;
                  while(i > 0 && n < 2) if( zipdir[i--] == '/' )n++;
                  if(zipdir[i+1] == '/') zipdir[i+2] = '\0';
                  else zipdir[0] = '\0';

                  File_ShrinkName(dlgpath, zipdir, 38);
                  reloaddir = TRUE;
                  selection = -1;                 /* Remove old selection */
                  zipfilename[0] = '\0';
                  dlgfname[0] = 0;
                  ypos = 0;
                }
              }  /* not a zip file: */
              else if( strlen(path)>2 )
                {
                  char *ptr;
                  File_CleanFileName(path);
                  ptr = strrchr(path, '/');
                  if(ptr)  *(ptr+1) = 0;
                  File_AddSlashToEndFileName(path);
                  reloaddir = TRUE;
                  File_ShrinkName(dlgpath, path, 38);  /* Copy the path name to the dialog */
                  selection = -1;                 /* Remove old selection */
                  fname[0] = 0;
                  dlgfname[0] = 0;
                  ypos = 0;
                }
              break;
            case SGFSDLG_ROOTDIR:               /* Change to root directory */
              if( browsingzip )
                {
                  /* free zip file entries */
                  ZIP_FreeZipDir(zipfiles);
                  zipfiles = NULL;
                  browsingzip = FALSE;
                }

              strcpy(path, "/");
              reloaddir = TRUE;
              strcpy(dlgpath, path);
              selection = -1;                   /* Remove old selection */
              fname[0] = 0;
              dlgfname[0] = 0;
              ypos = 0;
              break;
            case SGFSDLG_UP:                    /* Scroll up */
              if( ypos>0 )
                {
                  --ypos;
                  refreshentries = TRUE;
                }
              SDL_Delay(20);
              break;
            case SGFSDLG_DOWN:                  /* Scroll down */
              if( ypos+17<=entries )
                {
                  ++ypos;
                  refreshentries = TRUE;
                }
              SDL_Delay(20);
              break;
            case SGFSDLG_FILENAME:              /* User entered new filename */
              strcpy(fname, dlgfname);
              break;
          } /* switch */
        } /* other button code */


    } /* do */


  while(retbut!=SGFSDLG_OKAY && retbut!=SGFSDLG_CANCEL && !bQuitProgram);

  if( oldcursorstate==SDL_DISABLE )
    SDL_ShowCursor(SDL_DISABLE);

  File_makepath(path_and_name, path, fname, NULL);

  /* Free old allocated memory: */
  if( files!=NULL )
  {
    for(i=0; i<entries; i++)
    {
      free(files[i]);
    }
    free(files);
    files = NULL;
  }

  if( browsingzip )
    {
      /* free zip file entries */
      ZIP_FreeZipDir(zipfiles);
      zipfiles = NULL;
    }

  if( zip_path != NULL )
    {
      if( browsingzip )
        {
          strcpy(zip_path, zipdir);
          strcat(zip_path, zipfilename);
        } else zip_path[0] = '\0';
    }

  return(retbut == SGFSDLG_OKAY);
}

