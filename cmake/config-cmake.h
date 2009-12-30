/* CMake config.h for Hatari */

/* Define if you have a PNG compatible library */
#cmakedefine HAVE_LIBPNG 1

/* Define if you have a readline compatible library */
#cmakedefine HAVE_LIBREADLINE 1

/* Define if you have a X11 environment */
#cmakedefine HAVE_X11 1

/* Define to 1 if you have the `z' library (-lz). */
#cmakedefine HAVE_LIBZ 1

/* Define to 1 if you have the <zlib.h> header file. */
#cmakedefine HAVE_ZLIB_H 1

/* Define to 1 if you have the <termios.h> header file. */
#cmakedefine HAVE_TERMIOS_H 1

/* Define to 1 if you have the <glob.h> header file. */
#cmakedefine HAVE_GLOB_H 1

/* Define to 1 if you have the <strings.h> header file. */
#cmakedefine HAVE_STRINGS_H 1

/* Define to 1 if you have the <SDL/SDL_config.h> header file. */
#cmakedefine HAVE_SDL_SDL_CONFIG_H 1

/* Define to 1 if you have the <sys/times.h> header file. */
#cmakedefine HAVE_SYS_TIMES_H 1

/* Define to 1 if you have the `cfmakeraw' function. */
#cmakedefine HAVE_CFMAKERAW 1

/* Define to 1 if you have the 'setenv' function. */
#cmakedefine HAVE_SETENV 1

/* Define to 1 if you have the `select' function. */
#cmakedefine HAVE_SELECT 1

/* Define to 1 if you have unix domain sockets */
#cmakedefine HAVE_UNIX_DOMAIN_SOCKETS 1

/* Relative path from bindir to datadir */
#define BIN2DATADIR "."

/* Define to 1 to use less memory - at the expense of emulation speed */
#cmakedefine ENABLE_SMALL_MEM

/* Define to 1 to enable trace logs - undefine to slightly increase speed */
#cmakedefine ENABLE_TRACING 1


/* Define to the full name of this package. */
#define PACKAGE_NAME "hatari"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "hatari devel"

/* Define to the version of this package. */
#define PACKAGE_VERSION "devel"
