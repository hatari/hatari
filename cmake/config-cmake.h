/* CMake config.h for Hatari */

/* Define if you have a PNG compatible library */
#cmakedefine HAVE_LIBPNG 1

/* Define if you have a readline compatible library */
#cmakedefine HAVE_LIBREADLINE 1

/* Define if you have the PortMidi library */
#cmakedefine HAVE_PORTMIDI 1

/* Define if you have the capsimage library */
#cmakedefine HAVE_CAPSIMAGE 1

/* Define if you have a X11 environment */
#cmakedefine HAVE_X11 1

/* Define to 1 if you have the `z' library (-lz). */
#cmakedefine HAVE_LIBZ 1

/* Define to 1 if you have the <zlib.h> header file. */
#cmakedefine HAVE_ZLIB_H 1

/* Define to 1 if you have the <termios.h> header file. */
#cmakedefine HAVE_TERMIOS_H 1

/* Define to 1 if you have the <sys/ioctl.h> header file. */
#cmakedefine HAVE_SYS_IOCTL_H 1

/* Define to 1 if you have the <glob.h> header file. */
#cmakedefine HAVE_GLOB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#cmakedefine HAVE_STRINGS_H 1

/* Define to 1 if you have the <SDL_config.h> header file. */
#cmakedefine HAVE_SDL_CONFIG_H 1

/* Define to 1 if you have the <sys/time.h> header file. */
#cmakedefine HAVE_SYS_TIME_H 1

/* Define to 1 if you have the <sys/times.h> header file. */
#cmakedefine HAVE_SYS_TIMES_H 1

/* Define to 1 if you have the <utime.h> header file. */
#cmakedefine HAVE_UTIME_H 1

/* Define to 1 if you have the <sys/utime.h> header file. */
#cmakedefine HAVE_SYS_UTIME_H 1

/* Define to 1 if you have the `cfmakeraw' function. */
#cmakedefine HAVE_CFMAKERAW 1

/* Define to 1 if you have the `tcsetattr' function. */
#cmakedefine HAVE_TCSETATTR 1

/* Define to 1 if you have the 'setenv' function. */
#cmakedefine HAVE_SETENV 1

/* Define to 1 if you have the `select' function. */
#cmakedefine HAVE_SELECT 1

/* Define to 1 if you have unix domain sockets */
#cmakedefine HAVE_UNIX_DOMAIN_SOCKETS 1

/* Define to 1 if you have the 'gettimeofday' function. */
#cmakedefine HAVE_GETTIMEOFDAY 1

/* Define to 1 if you have the 'nanosleep' function. */
#cmakedefine HAVE_NANOSLEEP 1

/* Define to 1 if you have the 'alphasort' function. */
#cmakedefine HAVE_ALPHASORT 1

/* Define to 1 if you have the 'scandir' function. */
#cmakedefine HAVE_SCANDIR 1

/* Define to 1 if you have the 'statvfs' function. */
#cmakedefine HAVE_STATVFS 1

/* Define to 1 if you have the 'fseeko' function. */
#cmakedefine HAVE_FSEEKO 1

/* Define to 1 if you have the 'ftello' function. */
#cmakedefine HAVE_FTELLO 1

/* Define to 1 if you have the 'flock' function. */
#cmakedefine HAVE_FLOCK 1

/* Define to 1 if you have the 'strlcpy' function. */
#cmakedefine HAVE_LIBC_STRLCPY 1

/* Define to 1 if you have the 'd_type' member in the 'dirent' struct */
#cmakedefine HAVE_DIRENT_D_TYPE 1

/* Relative path from bindir to datadir */
#define BIN2DATADIR "@BIN2DATADIR@"

/* Define to 1 to enable DSP 56k emulation for Falcon mode */
#cmakedefine ENABLE_DSP_EMU 1

/* Define to 1 to use less memory - at the expense of emulation speed */
#cmakedefine ENABLE_SMALL_MEM 1

/* Define to 1 to enable trace logs - undefine to slightly increase speed */
#cmakedefine ENABLE_TRACING 1

/* Define to 1 if udev support is available */
#cmakedefine HAVE_UDEV 1

/* Define to 1 if the capstone library with m68k support is available */
#cmakedefine HAVE_CAPSTONE_M68K 1
