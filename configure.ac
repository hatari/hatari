# autoconf script for Hatari for all those people out there who are to lazy
# to manually edit the Makefile.cnf...
#
# To create all necessary files for configure, use:
#   aclocal && autoheader && automake --add-missing --copy && autoconf
# Later on you can use "autoreconf -i" after updating this file.

AC_INIT(hatari, 1.3.1)
AC_CONFIG_SRCDIR([src/main.c])
AC_PREREQ(2.60)

AC_CONFIG_HEADERS([config.h])

#AC_CANONICAL_HOST
AC_CANONICAL_TARGET

dnl Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_RANLIB

# extra command line options

AC_ARG_ENABLE([werror],
  [  --enable-werror         Compile with the "-Werror" C flag],
  [ ENABLE_WERROR="${enableval}" ],
  [ ENABLE_WERROR="no" ])

AC_ARG_ENABLE([tracing],
  [  --enable-tracing        Enable tracing],
  [ enable_tracing="${enableval}" ],
  [ enable_tracing="yes" ])
if test "${enable_tracing}" = "yes"; then
  AC_DEFINE(ENABLE_TRACING, 1, [Define to 1 to enable emulation tracing])
else
  AC_MSG_NOTICE([Tracing disabled])
fi

AC_ARG_ENABLE([small-mem],
  [  --enable-small-mem      Use less memory - at the expense of emulation speed],
  [ enable_smallmem="${enableval}" ],
  [ enable_smallmem="no" ])
if test "${enable_smallmem}" = "yes"; then
  AC_DEFINE(ENABLE_SMALL_MEM, 1,
     [Define to 1 to use less memory - at the expense of emulation speed])
fi

AC_ARG_WITH([extra-inc],
  [  --with-extra-inc=DIR    additional include files are in DIR],
  [ if test "${withval}" != no; then
      if test "${withval}" != yes; then
        INCL="${INCL} -I${withval}"
      fi
    fi ])

AC_ARG_WITH([extra-lib],
  [  --with-extra-lib=DIR    additional library files are in DIR],
  [ if test "${withval}" != no; then
      if test "${withval}" != yes; then
        LDFLAGS="${LDFLAGS} -L${withval}"
        if test x"${GCC}" = xyes; then
          LDFLAGS="${LDFLAGS} -Wl,-rpath,${withval}/lib"
        fi
      fi
    fi ])

AC_ARG_WITH([hostcc],
  [  --with-hostcc=x         Use x as compiler for the host system],
  [HOSTCC=$withval], [HOSTCC=$CC])


dnl Checks for libraries.

dnl Check for zlib
AC_CHECK_LIB(z, gzopen, ,
  AC_MSG_ERROR([You need the zlib to compile this program!]))

dnl Check for libpng
AC_CHECK_LIB(png, png_create_info_struct,
      [AC_CHECK_HEADER(png.h, png_ok=yes, png_ok=no)],
      png_ok=no, -lz -lm)
if test "x${png_ok}" = "xyes"; then
  AC_DEFINE(HAVE_LIBPNG, 1, [Whether libpng is present on the system])
  LIBS="${LIBS} -lpng"
else
  AC_MSG_WARN([*** Saving PNG screenshots will not be supported! ***])
fi

dnl Check for SDL
SDL_VERSION=1.2.0
AM_PATH_SDL($SDL_VERSION, :,
  AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!]))

dnl Check for readline library
VL_LIB_READLINE

dnl check for X11
no_x=yes
dnl first check for pkg-config itself
AC_CHECK_PROG(HAVE_PKG_CONFIG, pkg-config, yes)
dnl set same variables as AC_PATH_XTRA: X_CFLAGS, X_LIBS, no_x
if test "x${HAVE_PKG_CONFIG}" = "xyes"; then
  PKG_CHECK_MODULES(X, x11, no_x=no, no_x=yes)
fi
dnl if that failed, try finding X11 without pkg-config
if test "x${no_x}" = "xyes"; then
  AC_PATH_XTRA
fi
if test "x$with_x" = xno; then
  no_x=yes
fi
if test "x${no_x}" = "xyes"; then
  AC_MSG_NOTICE([No X11, disabled Hatari embedding support])
else
  AC_DEFINE(HAVE_X11, 1, [Whether X11 is present on the system])
fi

CPPFLAGS="${CPPFLAGS} ${INCL}"


dnl Checks for header files.

AC_HEADER_STDC
AC_HEADER_DIRENT

AC_CHECK_HEADERS([unistd.h zlib.h], ,
  AC_MSG_ERROR([header file not found!]))
AC_CHECK_HEADERS([termios.h glob.h malloc.h])
dnl If present, used to check SDL configuration
AC_CHECK_HEADERS([SDL/SDL_config.h])
AC_CHECK_HEADERS([sys/times.h])

dnl Checks for types.
AC_TYPE_MODE_T
AC_TYPE_SIZE_T

dnl Checks for structures.
AC_STRUCT_TM


dnl Checks for compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_C_VOLATILE

# Do we have GNU-C ?
if test x"${GCC}" = xyes; then

  GCCVERSION=`${CC} -dumpversion`
  case $GCCVERSION in
   2.96*)
    AC_MSG_WARN([You have GCC $GCCVERSION - This compiler is probably buggy.])
    AC_MSG_WARN([So I am using -O instead of -O2 (or greater) now!])
    CFLAGS=`echo "${CFLAGS}" | sed 's/-O[[2-9]]/-O/'`
    ;;
  esac

  if test x"${ENABLE_WERROR}" != xno; then
    CFLAGS="${CFLAGS} -Werror"
  fi
  CFLAGS="${CFLAGS} -Wall"

fi


# Cross-compiling ?

HOSTCFLAGS="-O"
HOSTLDFLAGS=""
if test "${build}" != "${target}" -a "${HOSTCC}" == "${CC}"; then
  AC_MSG_WARN([Cross compiling... I will now use cc as host compiler!])
  HOSTCC=cc
else
  HOSTCFLAGS="${CFLAGS}"
  HOSTLDFLAGS="${LDFLAGS}"
fi


dnl Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MEMCMP
#AC_FUNC_REALLOC
AC_FUNC_STAT
AC_CHECK_FUNCS([getcwd memmove memset mkdir rmdir strcasecmp \
  strchr strerror strncasecmp strrchr strstr],,
  [AC_MSG_ERROR([A necessary function could not be found.])])
AC_CHECK_FUNCS([cfmakeraw setenv select])
AC_CHECK_FUNCS([posix_memalign memalign valloc])

AC_MSG_CHECKING([for unix domain sockets])
AC_COMPILE_IFELSE([
#include <sys/socket.h>
#include <sys/un.h>
int main() { return socket(AF_UNIX, SOCK_STREAM, 0); } ],
  [AC_MSG_RESULT([yes]) AC_DEFINE([HAVE_UNIX_DOMAIN_SOCKETS], [1],
                             [Define to 1 if you have unix domain sockets])],
  [AC_MSG_RESULT([no])])


dnl Output

# Convert e.g. /usr/share to /usr/share/hatari :
if test "${datadir}" = '${prefix}/share'; then
  datadir='${prefix}/share/hatari'
fi
if test "${datadir}" = '${datarootdir}'; then
  datadir='${datarootdir}/hatari'
fi

adl_COMPUTE_RELATIVE_PATHS([bindir:datadir:bin2datadir])
AC_DEFINE_UNQUOTED([BIN2DATADIR], ["${bin2datadir}"],
                   [Relative path from bindir to datadir])

AC_SUBST(HOSTCC)
AC_SUBST(HOSTCFLAGS)
AC_SUBST(HOSTLDFLAGS)

AC_OUTPUT(Makefile.cnf)


AC_MSG_NOTICE([+-----------------------------------------------------+])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| Configuration finished. Now you can compile Hatari. |])
AC_MSG_NOTICE([| Note that you have to use GNU-Make to build it.     |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| - If you are using Linux then type:  make           |])
AC_MSG_NOTICE([| - If you are using *BSD then try :   gmake          |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| After compiling, you can install the program by     |])
AC_MSG_NOTICE([| typing:  make install  (or: gmake install)          |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| For information about the configuration variables,  |])
AC_MSG_NOTICE([| please have a look at Makefile.cnf and config.h.    |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([+-----------------------------------------------------+])
