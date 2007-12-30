/* Default config.h for Hatari */

/* Define to 1 if you have the <dirent.h> header file, and it defines `DIR'. */
#define HAVE_DIRENT_H 1

/* Define if you have a readline compatible library */
#undef HAVE_LIBREADLINE

/* Define to 1 if you have the `z' library (-lz). */
#define HAVE_LIBZ 1

/* Define to 1 if you have the <zlib.h> header file. */
#define HAVE_ZLIB_H 1

/* Define to 1 if you have the <termios.h> header file. */
#if defined(WIN32)
# undef  HAVE_TERMIOS_H
#else
# define HAVE_TERMIOS_H 1
#endif

/* Define to 1 if you have the <glob.h> header file. */
#if defined(WIN32)
# undef  HAVE_GLOB_H
#else
# define HAVE_GLOB_H 1
#endif

/* Define to 1 if you have the <strings.h> header file. */
#if defined(__CEGCC__)
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
    || defined(__AMIGAOS4__) || defined(__riscos) || defined(__CEGCC__)
# undef  HAVE_CFMAKERAW
#else
# define HAVE_CFMAKERAW 1
#endif

/* Define to 1 if you have the 'setenv' function. */
#if defined(WIN32) || (defined(__sun) && defined(__SVR4))
# undef HAVE_SETENV
#else
# define HAVE_SETENV 1
#endif

/* Relative path from bindir to datadir */
#define BIN2DATADIR "."

/* Define to 1 to use less memory - at the expense of emulation speed */
#if defined(__CEGCC__)
# define ENABLE_SMALL_MEM 1
#else
# undef ENABLE_SMALL_MEM
#endif

/* Define to the full name of this package. */
#define PACKAGE_NAME "hatari"

/* Define to the full name and version of this package. */
#define PACKAGE_STRING "hatari CVS"

/* Define to the version of this package. */
#define PACKAGE_VERSION "CVS"

