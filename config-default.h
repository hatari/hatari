/* Default config.h for Hatari */

/* Define to `__inline__' or `__inline' if that's what the C compiler
   calls it, or to nothing if 'inline' is not supported under any name.  */
#if defined(_MSC_VER)
# define inline __inline
#endif

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'. */
#define HAVE_DIRENT_H 1

/* Define if you have a readline compatible library */
#undef HAVE_LIBREADLINE

/* Define to 1 if you have the `z' library (-lz). */
#define HAVE_LIBZ 1

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* Define to 1 if you have the <termios.h> header file. */
#if defined(WIN32) || defined(GEKKO)
# undef  HAVE_TERMIOS_H
#else
# define HAVE_TERMIOS_H 1
#endif

/* Define to 1 if you have the <glob.h> header file. */
#if defined(WIN32) || defined(GEKKO)
# undef  HAVE_GLOB_H
#else
# define HAVE_GLOB_H 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
#if defined(__CEGCC__) || defined(GEKKO)
# undef  HAVE_STRINGS_H
#else
# define HAVE_STRINGS_H 1
#endif

/* Define to 1 if you have the `strcasecmp' function. */
#define HAVE_STRCASECMP 1

/* Define to 1 if you have the `strncasecmp' function. */
#define HAVE_STRNCASECMP 1

/* Define to 1 if you have the `cfmakeraw' function. */
#if defined(__BEOS__) || (defined(__sun) && defined(__SVR4)) \
    || defined(__CYGWIN__)  || defined(GEKKO) \
    || defined(__AMIGAOS4__) || defined(__riscos) || defined(__CEGCC__)
# undef  HAVE_CFMAKERAW
#else
# define HAVE_CFMAKERAW 1
#endif

/* define to 1 if you the 'posix_memalign' function (for ide.c). */
#if defined(__GLIBC__)
# define HAVE_MALLOC_H 1
# define HAVE_POSIX_MEMALIGN 1
#else
# undef  HAVE_MALLOC_H
# undef  HAVE_POSIX_MEMALIGN
#endif

/* Define to 1 if you have the 'setenv' function. */
#if defined(WIN32) || (defined(__sun) && defined(__SVR4)) || defined(GEKKO)
# undef HAVE_SETENV
#else
# define HAVE_SETENV 1
#endif

/* Define to 1 if you have the `select' function. */
#if defined(GEKKO)
# undef HAVE_SELECT
#else
# define HAVE_SELECT 1
#endif

/* Define to 1 if you have unix domain sockets */
#if defined(WIN32) || defined(__CEGCC__)
# undef  HAVE_UNIX_DOMAIN_SOCKETS
#else
# define HAVE_UNIX_DOMAIN_SOCKETS 1
#endif

/* Relative path from bindir to datadir */
#ifndef BIN2DATADIR
# define BIN2DATADIR "."
#endif

/* Define to 1 to use less memory - at the expense of emulation speed */
#if defined(__CEGCC__) || defined(GEKKO)
# define ENABLE_SMALL_MEM 1
#else
# undef ENABLE_SMALL_MEM
#endif

/* Define to 1 to enable trace logs - undefine to slightly increase speed */
#define ENABLE_TRACING 1

/* Additional configuration for Visual-C */
#if defined(_MSC_VER)
# include "Visual.Studio/VisualStudioFix.h"
#endif

/* Define to the full name of this package. */
#define PACKAGE_NAME "hatari"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "hatari devel"

/* Define to the version of this package. */
#define PACKAGE_VERSION "devel"
