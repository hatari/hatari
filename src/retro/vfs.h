/*
  Hatari - vfs.h

  This file is distributed under the GNU General Public License, version 2
  or at your option any later version. Read the file gpl.txt for details.
*/

#ifndef HATARI_VFS_H
#define HATARI_VFS_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define VFS_REQUIRED_VERSION 4

#if defined(__ANDROID__) && (defined(__arm__) || defined(__i386__))
#define VFS_ANDROID_32BIT 1
#else
#define VFS_ANDROID_32BIT 0
#endif

void VFS_Init(void);
bool VFS_IsActive(void);

typedef void *VFS_FILE;

VFS_FILE VFS_fopen(const char *path, const char *mode);
int VFS_fclose(VFS_FILE stream);
size_t VFS_fread(void *ptr, size_t size, size_t nmemb, VFS_FILE stream);
size_t VFS_fwrite(const void *ptr, size_t size, size_t nmemb, VFS_FILE stream);
int64_t VFS_fseek(VFS_FILE stream, int64_t offset, int whence);
int64_t VFS_ftell(VFS_FILE stream);
int64_t VFS_fsize(VFS_FILE stream);

bool VFS_FileExists(const char *path);
bool VFS_DirExists(const char *path);
int64_t VFS_FileLength(const char *path);

#endif /* HATARI_VFS_H */
