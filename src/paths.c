/*
  Hatari - paths.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.

  Set up the various path strings.
*/
const char Paths_fileid[] = "Hatari paths.c";

#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "main.h"
#include "file.h"
#include "paths.h"
#include "str.h"

#if defined(WIN32) && !defined(mkdir)
#define mkdir(name,mode) mkdir(name)
#endif  /* WIN32 */

#if defined(__APPLE__)
	#define HATARI_HOME_DIR "Library/Application Support/Hatari"
#elif defined(WIN32)
	#define HATARI_HOME_DIR "AppData\\Local\\Hatari"
#else
	#define HATARI_HOME_DIR ".config/hatari"
#endif

static char *sWorkingDir;     /* Working directory */
static char *sDataDir;        /* Directory where data files of Hatari can be found */
static char *sUserHomeDir;    /* User's home directory ($HOME) */
static char *sHatariHomeDir;  /* Hatari's home directory ($HOME/.hatari/) */
static char *sScreenShotDir;  /* Directory to use for screenshots */

/**
 * Return pointer to current working directory string
 */
const char *Paths_GetWorkingDir(void)
{
	return sWorkingDir;
}

/**
 * Return pointer to data directory string
 */
const char *Paths_GetDataDir(void)
{
	return sDataDir;
}

/**
 * Return pointer to user's home directory string
 */
const char *Paths_GetUserHome(void)
{
	return sUserHomeDir;
}

/**
 * Return pointer to Hatari's home directory string
 */
const char *Paths_GetHatariHome(void)
{
	return sHatariHomeDir;
}

/**
 * Return pointer to screenshot directory string
 */
const char *Paths_GetScreenShotDir(void)
{
	return sScreenShotDir;
}

/**
 * Set new screenshot directory location
 */
void Paths_SetScreenShotDir(const char *sNewDir)
{
	Str_Free(sScreenShotDir);
	sScreenShotDir = Str_Dup(sNewDir);
}

/**
 * Explore the PATH environment variable to see where our executable is
 * installed.
 */
static void Paths_GetExecDirFromPATH(const char *argv0, char *pExecDir, int nMaxLen)
{
	char *pPathEnv;
	char *pAct;
	char *pTmpName;
	const char *pToken;

	/* Get the PATH environment string */
	pPathEnv = getenv("PATH");
	if (!pPathEnv)
		return;
	/* Duplicate the string because strtok destroys it later */
	pPathEnv = strdup(pPathEnv);
	if (!pPathEnv)
		return;

	pTmpName = malloc(FILENAME_MAX);
	if (!pTmpName)
	{
		perror("Paths_GetExecDirFromPATH");
		free(pPathEnv);
		return;
	}

	/* If there is a semicolon in the PATH, we assume it is the PATH
	 * separator token (like on Windows), otherwise we use a colon. */
	if (strchr((pPathEnv), ';'))
		pToken = ";";
	else
		pToken = ":";

	pAct = strtok (pPathEnv, pToken);
	while (pAct)
	{
		snprintf(pTmpName, FILENAME_MAX, "%s%c%s",
		         pAct, PATHSEP, argv0);
		if (File_Exists(pTmpName))
		{
			/* Found the executable - so use the corresponding path: */
			Str_Copy(pExecDir, pAct, nMaxLen);
			break;
		}
		pAct = strtok (NULL, pToken);
	}

	free(pPathEnv);
	free(pTmpName);
}


/**
 * Locate the directory where the hatari executable resides
 */
static char *Paths_InitExecDir(const char *argv0)
{
	char *psExecDir;  /* Path string where the hatari executable can be found */

	/* Allocate memory for storing the path string of the executable */
	psExecDir = malloc(FILENAME_MAX);
	if (!psExecDir)
	{
		fprintf(stderr, "Out of memory (Paths_Init)\n");
		exit(-1);
	}

	/* Determine the bindir...
	 * Start with empty string, then try to use OS specific functions,
	 * and finally analyze the PATH variable if it has not been found yet. */
	psExecDir[0] = '\0';

#if defined(__linux__)
	{
		int i;
		/* On Linux, we can analyze the symlink /proc/self/exe */
		i = readlink("/proc/self/exe", psExecDir, FILENAME_MAX-1);
		if (i > 0)
		{
			char *p;
			psExecDir[i] = '\0';
			p = strrchr(psExecDir, '/');    /* Search last slash */
			if (p)
				*p = 0;                     /* Strip file name from path */
		}
	}
//#elif defined(WIN32)
//	/* On Windows we can use GetModuleFileName for getting the exe path */
//	GetModuleFileName(NULL, psExecDir, FILENAME_MAX);
#endif

	/* If we do not have the execdir yet, analyze argv[0] and the PATH: */
	if (psExecDir[0] == 0)
	{
		if (strchr(argv0, PATHSEP) == NULL)
		{
			/* No separator in argv[0], we have to explore PATH... */
			Paths_GetExecDirFromPATH(argv0, psExecDir, FILENAME_MAX);
		}
		else
		{
			/* There was a path separator in argv[0], so let's assume a
			 * relative or absolute path to the current directory in argv[0] */
			char *p;
			Str_Copy(psExecDir, argv0, FILENAME_MAX);
			p = strrchr(psExecDir, PATHSEP);  /* Search last slash */
			if (p)
				*p = 0;                       /* Strip file name from path */
		}
	}

	return psExecDir;
}


/**
 * Initialize the users home directory string
 * and Hatari's home directory (~/.config/hatari)
 */
static void Paths_InitHomeDirs(void)
{
	char *psHome;

	psHome = getenv("HOME");
	if (psHome)
	{
		sUserHomeDir = Str_Dup(psHome);
	}
#if defined(WIN32)
	else
	{
		char *psDrive;
		int len = 0;
		/* Windows home path? */
		psDrive = getenv("HOMEDRIVE");
		if (psDrive)
			len = strlen(psDrive);
		psHome = getenv("HOMEPATH");
		if (psHome)
			len += strlen(psHome);
		if (len > 0)
		{
			sUserHomeDir = Str_Alloc(len);
			if (psDrive)
				strcpy(sUserHomeDir, psDrive);
			if (psHome)
				strcat(sUserHomeDir, psHome);
		}
	}
#endif
	if (!sUserHomeDir)
	{
		/* $HOME not set, so let's use current working dir as home */
		sUserHomeDir = Str_Dup(sWorkingDir);
		sHatariHomeDir = Str_Dup(sWorkingDir);
		return;
	}

	sHatariHomeDir = Str_Alloc(strlen(sUserHomeDir) + 1 + strlen(HATARI_HOME_DIR));

	/* Try to use a private hatari directory in the users home directory */
	sprintf(sHatariHomeDir, "%s%c%s", sUserHomeDir, PATHSEP, HATARI_HOME_DIR);
	if (File_DirExists(sHatariHomeDir))
	{
		return;
	}
	/* Try legacy location ~/.hatari */
	sprintf(sHatariHomeDir, "%s%c.hatari", sUserHomeDir, PATHSEP);
	if (File_DirExists(sHatariHomeDir))
	{
		return;
	}

	/* Hatari home directory does not exists yet...
	 * ... so let's try to create it: */
#if !defined(__APPLE__) && !defined(WIN32)
	sprintf(sHatariHomeDir, "%s%c.config", sUserHomeDir, PATHSEP);
	if (!File_DirExists(sHatariHomeDir))
	{
		/* ~/.config does not exist yet, create it first */
		if (mkdir(sHatariHomeDir, 0700) != 0)
		{
			perror("Failed to create ~/.config directory");
		}
	}
#endif
	sprintf(sHatariHomeDir, "%s%c%s", sUserHomeDir, PATHSEP, HATARI_HOME_DIR);
	if (mkdir(sHatariHomeDir, 0750) != 0)
	{
		/* Failed to create, so use user's home dir instead */
		strcpy(sHatariHomeDir, sUserHomeDir);
	}
}


/**
 * Initialize directory names
 *
 * The datadir will be initialized relative to the bindir (where the executable
 * has been installed to). This means a lot of additional effort since we first
 * have to find out where the executable is. But thanks to this effort, we get
 * a relocatable package (we don't have any absolute path names in the program)!
 */
void Paths_Init(const char *argv0)
{
	char *psExecDir;  /* Path string where the hatari executable can be found */

	/* Init working directory string */
	sWorkingDir = malloc(FILENAME_MAX);
	if (!sWorkingDir || getcwd(sWorkingDir, FILENAME_MAX) == NULL)
	{
		/* This should never happen... just in case... */
		sWorkingDir = Str_Dup(".");
	}

	/* Init the user's home directory string */
	Paths_InitHomeDirs();

	/* Init screenshot directory string */
#if !defined(__APPLE__)
	sScreenShotDir = Str_Dup(sWorkingDir);
#else
	sScreenShotDir = Paths_GetMacScreenShotDir();
	if (!sScreenShotDir)
	{
		/* Failsafe, but should not be able to happen */
		sScreenShotDir = Str_Dup(sWorkingDir);
	}
#endif

	/* Get the directory where the executable resides */
	psExecDir = Paths_InitExecDir(argv0);

	/* Now create the datadir path name from the bindir path name: */
	sDataDir = Str_Alloc(FILENAME_MAX);
	if (psExecDir && strlen(psExecDir) > 0)
	{
		sprintf(sDataDir, "%s%c%s", psExecDir, PATHSEP, BIN2DATADIR);
	}
	else
	{
		/* bindir could not be determined, let's assume datadir is relative
		 * to current working directory... */
		strcpy(sDataDir, BIN2DATADIR);
	}

	/* And finally make a proper absolute path out of datadir: */
	File_MakeAbsoluteName(sDataDir);

	free(psExecDir);

	/* fprintf(stderr, " WorkingDir = %s\n DataDir = %s\n UserHomeDir = %s\n HatariHomeDir = %s\n ScrenShotDir = %s\n",
	        sWorkingDir, sDataDir, sUserHomeDir, sHatariHomeDir, sScreenShotDir); */
}

void Paths_UnInit(void)
{
	Str_Free(sWorkingDir);
	Str_Free(sDataDir);
	Str_Free(sUserHomeDir);
	Str_Free(sHatariHomeDir);
	Str_Free(sScreenShotDir);
}
