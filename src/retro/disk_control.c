/*
  Hatari - disk_control.c

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <libretro.h>

#include "main.h"
#include "main_retro.h"
#include "disk_control.h"
#include "file.h"
#include "floppy.h"
#include "log.h"

typedef struct
{
	char path[FILENAME_MAX];
	char label[FILENAME_MAX];
} DISKCONTROL_IMAGE;

static DISKCONTROL_IMAGE *DiskImages;
static unsigned int DiskImagesCount;
static unsigned int DiskImagesAllocated;
static unsigned int CurrentImageIndex;
static bool bTrayEjected;

/**
 * Make sure there is room
 */
static bool DiskControl_EnsureCapacity(unsigned int count)
{
	unsigned int newcap;
	DISKCONTROL_IMAGE *newarr;

	if (count <= DiskImagesAllocated)
		return true;

	newcap = DiskImagesAllocated ? DiskImagesAllocated * 2 : 4;
	if (newcap < count)
		newcap = count;

	newarr = realloc(DiskImages, newcap * sizeof(DISKCONTROL_IMAGE));
	if (!newarr)
	{
		Log_Printf(LOG_ERROR, "DiskControl: out of memory growing image list\n");
		return false;
	}
	DiskImages = newarr;
	DiskImagesAllocated = newcap;
	return true;
}

/**
 * Derive a display label
 */
static void DiskControl_UpdateLabel(unsigned int idx)
{
	const char *base;

	if (!DiskImages[idx].path[0])
	{
		DiskImages[idx].label[0] = '\0';
		return;
	}
	base = File_Basename(DiskImages[idx].path);
	strncpy(DiskImages[idx].label, base, sizeof(DiskImages[idx].label) - 1);
	DiskImages[idx].label[sizeof(DiskImages[idx].label) - 1] = '\0';
}

/**
 * Return true if 'p' looks like an absolute path.
 */
static bool DiskControl_PathIsAbsolute(const char *p)
{
	if (!p || !p[0])
		return false;
	if (p[0] == '/' || p[0] == '\\')
		return true;
#if defined(_WIN32) || defined(WIN32)
	if (p[1] == ':')
		return true;
#endif
	return false;
}

/**
 * Parse a .m3u file
 */
static unsigned int DiskControl_ParseM3U(const char *m3u_path)
{
	uint8_t *raw;
	long size = 0;
	char *text, *p;
	char dir[FILENAME_MAX], name[FILENAME_MAX], ext[FILENAME_MAX];

	DiskImagesCount = 0;

	raw = File_ReadAsIs(m3u_path, &size);
	if (!raw || size <= 0)
	{
		Log_Printf(LOG_ERROR, "DiskControl: could not read m3u file '%s'\n", m3u_path);
		free(raw);
		return 0;
	}

	text = malloc((size_t)size + 1);
	if (!text)
	{
		free(raw);
		return 0;
	}
	memcpy(text, raw, (size_t)size);
	text[size] = '\0';
	free(raw);

	File_SplitPath(m3u_path, dir, name, ext);

	p = text;
	while (*p)
	{
		char *line, *eol;
		char full[FILENAME_MAX];

		while (*p == '\r' || *p == '\n')
			p++;
		if (!*p)
			break;

		line = p;
		eol = strpbrk(p, "\r\n");
		if (eol)
		{
			*eol = '\0';
			p = eol + 1;
		}
		else
		{
			p += strlen(p);
		}

		while (*line == ' ' || *line == '\t')
			line++;
		if (!*line || *line == '#')
			continue;

		if (DiskControl_PathIsAbsolute(line))
		{
			strncpy(full, line, sizeof(full) - 1);
			full[sizeof(full) - 1] = '\0';
		}
		else
		{
			char *built = File_MakePath(dir, line, NULL);
			if (!built)
				continue;
			strncpy(full, built, sizeof(full) - 1);
			full[sizeof(full) - 1] = '\0';
			free(built);
		}

		if (!DiskControl_EnsureCapacity(DiskImagesCount + 1))
			break;

		strncpy(DiskImages[DiskImagesCount].path, full,
		        sizeof(DiskImages[DiskImagesCount].path) - 1);
		DiskImages[DiskImagesCount].path[sizeof(DiskImages[DiskImagesCount].path) - 1] = '\0';
		DiskControl_UpdateLabel(DiskImagesCount);
		DiskImagesCount++;
	}

	free(text);

	Log_Printf(LOG_INFO, "DiskControl: m3u '%s' parsed, %u file(s) found.\n",
	           m3u_path, DiskImagesCount);

	return DiskImagesCount;
}

/**
 * (Re-)insert the image at CurrentImageIndex into floppy drive A
 */
static void DiskControl_ApplyCurrentImage(void)
{
	/* clear drive B first if using manual disk control */
	Floppy_SetDiskFileNameNone(1);
	Floppy_EjectDiskFromDrive(1);

	if (CurrentImageIndex < DiskImagesCount && DiskImages[CurrentImageIndex].path[0])
	{
		if (Floppy_SetDiskFileName(0, DiskImages[CurrentImageIndex].path, NULL))
			Floppy_InsertDiskIntoDrive(0);
		else
			Log_Printf(LOG_ERROR, "DiskControl: failed to set/insert '%s' "
			           "into drive A\n", DiskImages[CurrentImageIndex].path);
	}
	else
	{
		Floppy_SetDiskFileNameNone(0);
		Floppy_EjectDiskFromDrive(0);
	}
}

/*----------------------------------------------------------------------*/
/* libretro callbacks                                                    */
/*----------------------------------------------------------------------*/
static bool RETRO_CALLCONV DiskControl_SetEjectState(bool ejected)
{
	if (ejected == bTrayEjected)
		return true;

	if (ejected)
		Floppy_EjectDiskFromDrive(0);
	else
		DiskControl_ApplyCurrentImage();

	bTrayEjected = ejected;
	return true;
}

static bool RETRO_CALLCONV DiskControl_GetEjectState(void)
{
	return bTrayEjected;
}

static unsigned RETRO_CALLCONV DiskControl_GetImageIndex(void)
{
	return CurrentImageIndex;
}

static bool RETRO_CALLCONV DiskControl_SetImageIndex(unsigned int index)
{
	/* Only allow swapping while the tray is open */
	if (!bTrayEjected)
		return false;

	if (DiskImagesCount == 0 || index >= DiskImagesCount)
		return false;

	CurrentImageIndex = index;
	return true;
}

static unsigned RETRO_CALLCONV DiskControl_GetNumImages(void)
{
	return DiskImagesCount;
}

static bool RETRO_CALLCONV DiskControl_ReplaceImageIndex(unsigned int index,
                                                         const struct retro_game_info *info)
{
	unsigned int i;

	if (index >= DiskImagesCount || !bTrayEjected)
		return false;

	if (!info)
	{
		/* NULL means: remove this image from the list */
		for (i = index; i + 1 < DiskImagesCount; i++)
			DiskImages[i] = DiskImages[i + 1];
		DiskImagesCount--;
		if (CurrentImageIndex >= DiskImagesCount && DiskImagesCount > 0)
			CurrentImageIndex = DiskImagesCount - 1;
		return true;
	}

	if (!info->path)
		return false;

	strncpy(DiskImages[index].path, info->path, sizeof(DiskImages[index].path) - 1);
	DiskImages[index].path[sizeof(DiskImages[index].path) - 1] = '\0';
	DiskControl_UpdateLabel(index);

	return true;
}

static bool RETRO_CALLCONV DiskControl_AddImageIndex(void)
{
	if (!DiskControl_EnsureCapacity(DiskImagesCount + 1))
		return false;

	DiskImages[DiskImagesCount].path[0] = '\0';
	DiskImages[DiskImagesCount].label[0] = '\0';
	DiskImagesCount++;
	return true;
}

static bool RETRO_CALLCONV DiskControl_CB_SetInitialImage(unsigned int index, const char *path)
{
	(void)path;

	if (DiskImagesCount && index < DiskImagesCount)
		CurrentImageIndex = index;
	return true;
}

static bool RETRO_CALLCONV DiskControl_GetImagePath(unsigned int index, char *path, size_t len)
{
	if (index >= DiskImagesCount || !DiskImages[index].path[0] || len == 0)
		return false;

	strncpy(path, DiskImages[index].path, len - 1);
	path[len - 1] = '\0';
	return true;
}

static bool RETRO_CALLCONV DiskControl_GetImageLabel(unsigned int index, char *label, size_t len)
{
	if (index >= DiskImagesCount || !DiskImages[index].label[0] || len == 0)
		return false;

	strncpy(label, DiskImages[index].label, len - 1);
	label[len - 1] = '\0';
	return true;
}

static struct retro_disk_control_ext_callback DiskControlCallbacks =
{
	DiskControl_SetEjectState,
	DiskControl_GetEjectState,
	DiskControl_GetImageIndex,
	DiskControl_SetImageIndex,
	DiskControl_GetNumImages,
	DiskControl_ReplaceImageIndex,
	DiskControl_AddImageIndex,
	DiskControl_CB_SetInitialImage,
	DiskControl_GetImagePath,
	DiskControl_GetImageLabel
};

/*----------------------------------------------------------------------*/
/* API                                                                  */
/*----------------------------------------------------------------------*/
void DiskControl_Init(void)
{
	if (!environment_cb(RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE,
	                    &DiskControlCallbacks))
	{
		Log_Printf(LOG_WARN, "DiskControl: frontend does not support the "
		           "extended disk control interface, disk swapping via "
		           "the frontend won't be available.\n");
	}
}

void DiskControl_NewGame(const char *path)
{
	DiskImagesCount = 0;
	CurrentImageIndex = 0;
	bTrayEjected = false;

	if (path && path[0] && DiskControl_EnsureCapacity(1))
	{
		strncpy(DiskImages[0].path, path, sizeof(DiskImages[0].path) - 1);
		DiskImages[0].path[sizeof(DiskImages[0].path) - 1] = '\0';
		DiskControl_UpdateLabel(0);
		DiskImagesCount = 1;
	}
}

void DiskControl_NewGameM3U(const char *m3u_path)
{
	unsigned int n;

	bTrayEjected = false;

	n = DiskControl_ParseM3U(m3u_path);
	if (n == 0)
	{
		DiskImagesCount = 0;
		CurrentImageIndex = 0;
		Floppy_SetDiskFileNameNone(0);
		Floppy_EjectDiskFromDrive(0);
		return;
	}

	CurrentImageIndex = 0;
	DiskControl_ApplyCurrentImage();
}

void DiskControl_UnInit(void)
{
	free(DiskImages);
	DiskImages = NULL;
	DiskImagesCount = 0;
	DiskImagesAllocated = 0;
	CurrentImageIndex = 0;
	bTrayEjected = false;
}
