/*
  Hatari - vfs.c
*/
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include <libretro.h>

#include "main_retro.h"
#include "vfs.h"
#include "log.h"

static struct retro_vfs_interface *vfs_iface = NULL;
static unsigned vfs_iface_version = 0;

void VFS_Init(void)
{
	if (!environment_cb)
		return;

	// already initialized
	if (vfs_iface != NULL)
		return;

	struct retro_vfs_interface_info info;

	memset(&info, 0, sizeof(info));
	info.required_interface_version = VFS_REQUIRED_VERSION;
	info.iface = NULL;

	if (environment_cb(RETRO_ENVIRONMENT_GET_VFS_INTERFACE, &info) && info.iface)
	{
		vfs_iface = info.iface;
		vfs_iface_version = info.required_interface_version;
	}

	if (vfs_iface)
	{
		Log_Printf(LOG_INFO, "Using libretro VFS interface v%u (requested v%u).\n",
		           vfs_iface_version, VFS_REQUIRED_VERSION);
	}
	else
	{
		Log_Printf(LOG_INFO, "Libretro VFS interface v%d not available, "
		           "falling back to native file access.\n", VFS_REQUIRED_VERSION);
	}
}


bool VFS_IsActive(void)
{
	return vfs_iface != NULL && vfs_iface->open != NULL;
}

static unsigned VFS_ModeToFlags(const char *mode)
{
	bool plus = strchr(mode, '+') != NULL;
	bool append = strchr(mode, 'a') != NULL;
	bool write = strchr(mode, 'w') != NULL;

	if (append)
		return RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING;
	if (write)
		return plus ? RETRO_VFS_FILE_ACCESS_READ_WRITE : RETRO_VFS_FILE_ACCESS_WRITE;

	return plus ? (RETRO_VFS_FILE_ACCESS_READ_WRITE | RETRO_VFS_FILE_ACCESS_UPDATE_EXISTING)
	            : RETRO_VFS_FILE_ACCESS_READ;
}

VFS_FILE VFS_fopen(const char *path, const char *mode)
{
	if (VFS_IsActive())
	{
		struct retro_vfs_file_handle *fh;
		unsigned flags = VFS_ModeToFlags(mode);

		fh = vfs_iface->open(path, flags, RETRO_VFS_FILE_ACCESS_HINT_NONE);
		if (fh && strchr(mode, 'a') && vfs_iface->seek)
			vfs_iface->seek(fh, 0, RETRO_VFS_SEEK_POSITION_END);

		return (VFS_FILE)fh;
	}

	return (VFS_FILE)fopen(path, mode);
}

int VFS_fclose(VFS_FILE stream)
{
	if (!stream)
		return 0;

	if (VFS_IsActive())
		return vfs_iface->close((struct retro_vfs_file_handle *)stream);

	return fclose((FILE *)stream);
}

size_t VFS_fread(void *ptr, size_t size, size_t nmemb, VFS_FILE stream)
{
	if (!stream || !size || !nmemb)
		return 0;

	if (VFS_IsActive())
	{
		int64_t got = vfs_iface->read((struct retro_vfs_file_handle *)stream,
		                              ptr, (uint64_t)size * nmemb);
		if (got <= 0)
			return 0;
		return (size_t)(got / (int64_t)size);
	}

	return fread(ptr, size, nmemb, (FILE *)stream);
}

size_t VFS_fwrite(const void *ptr, size_t size, size_t nmemb, VFS_FILE stream)
{
	if (!stream || !size || !nmemb)
		return 0;

	if (VFS_IsActive())
	{
		int64_t put = vfs_iface->write((struct retro_vfs_file_handle *)stream,
		                               ptr, (uint64_t)size * nmemb);
		if (put <= 0)
			return 0;
		return (size_t)(put / (int64_t)size);
	}

	return fwrite(ptr, size, nmemb, (FILE *)stream);
}

int64_t VFS_fseek(VFS_FILE stream, int64_t offset, int whence)
{
	int position;

	if (!stream)
		return -1;

	if (VFS_IsActive())
	{
		switch (whence)
		{
		 case SEEK_SET: position = RETRO_VFS_SEEK_POSITION_START;   break;
		 case SEEK_CUR: position = RETRO_VFS_SEEK_POSITION_CURRENT; break;
		 case SEEK_END: position = RETRO_VFS_SEEK_POSITION_END;     break;
		 default: return -1;
		}
		return vfs_iface->seek((struct retro_vfs_file_handle *)stream, offset, position);
	}

#if VFS_ANDROID_32BIT
	return fseek((FILE *)stream, (long)offset, whence);
#else
	return fseeko((FILE *)stream, (off_t)offset, whence);
#endif
}

int64_t VFS_ftell(VFS_FILE stream)
{
	if (!stream)
		return -1;

	if (VFS_IsActive())
		return vfs_iface->tell((struct retro_vfs_file_handle *)stream);

#if VFS_ANDROID_32BIT
	return ftell((FILE *)stream);
#else
	return ftello((FILE *)stream);
#endif
}

int64_t VFS_fsize(VFS_FILE stream)
{
	if (!stream)
		return -1;

	if (VFS_IsActive() && vfs_iface->size)
		return vfs_iface->size((struct retro_vfs_file_handle *)stream);

	// fallback
	{
#if VFS_ANDROID_32BIT
		long current;
		long size;

		current = ftell((FILE *)stream);
		if (current < 0)
			return -1;

		if (fseek((FILE *)stream, 0, SEEK_END) != 0)
			return -1;

		size = ftell((FILE *)stream);

		if (fseek((FILE *)stream, current, SEEK_SET) != 0)
			return -1;

		return (int64_t)size;
#else
		off_t current;
		off_t size;

		current = ftello((FILE *)stream);
		if (current < 0)
			return -1;

		if (fseeko((FILE *)stream, 0, SEEK_END) != 0)
			return -1;

		size = ftello((FILE *)stream);

		if (fseeko((FILE *)stream, current, SEEK_SET) != 0)
			return -1;

		return (int64_t)size;
#endif
	}
}

bool VFS_FileExists(const char *path)
{
	if (VFS_IsActive() && vfs_iface->stat_64)
	{
		int64_t size = 0;
		int flags = vfs_iface->stat_64(path, &size);
		return (flags & RETRO_VFS_STAT_IS_VALID) && !(flags & RETRO_VFS_STAT_IS_DIRECTORY);
	}

	// fallback
	{
		struct stat st;
		return (stat(path, &st) == 0 &&
		        (st.st_mode & (S_IRUSR|S_IWUSR)) && !S_ISDIR(st.st_mode));
	}
}

bool VFS_DirExists(const char *path)
{
	if (VFS_IsActive() && vfs_iface->stat_64)
	{
		int64_t size = 0;
		int flags = vfs_iface->stat_64(path, &size);
		return (flags & RETRO_VFS_STAT_IS_VALID) && (flags & RETRO_VFS_STAT_IS_DIRECTORY);
	}

	if (VFS_IsActive() && vfs_iface->opendir && vfs_iface->closedir)
	{
		struct retro_vfs_dir_handle *dh = vfs_iface->opendir(path, false);
		if (!dh)
			return false;
		vfs_iface->closedir(dh);
		return true;
	}

	// fallback
	{
		struct stat st;
		return (stat(path, &st) == 0 && S_ISDIR(st.st_mode));
	}
}

int64_t VFS_FileLength(const char *path)
{
	if (VFS_IsActive() && vfs_iface->stat_64)
	{
		int64_t size = 0;
		int flags = vfs_iface->stat_64(path, &size);
		return (flags & RETRO_VFS_STAT_IS_VALID) ? (int64_t)size : -1;
	}

	// fallback
	{
		struct stat st;
		if (stat(path, &st) == 0)
			return (int64_t)st.st_size;
		return -1;
	}
}
