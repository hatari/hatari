# autoconf script for Hatari for all those people out there who are to lazy
# to manually edit the Makefile.cnf...
#
# Process this file with autoconf to produce a configure script.
# Don't forget to run aclocal first!

AC_INIT(hatari, 0.40)
AC_CONFIG_SRCDIR([src/main.c])
AC_PREREQ(2.50)

#AC_CANONICAL_HOST
AC_CANONICAL_TARGET

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Use the following line for a release versions:
#CFLAGS="-O3 -fomit-frame-pointer"


# extra command line options

AC_ARG_ENABLE([werror],
  [  --enable-werror         Compile with the "-Werror" C flag],
  [ ENABLE_WERROR="${enableval}" ],
  [ ENABLE_WERROR="no" ])

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


# Checks for libraries.

dnl Check for zlib
AC_CHECK_LIB(z, gzopen, ,
  AC_MSG_ERROR([You need the zlib to compile this program!]))

dnl Check for SDL
SDL_VERSION=1.2.0
AM_PATH_SDL($SDL_VERSION, :,
  AC_MSG_ERROR([*** SDL version $SDL_VERSION not found!]))


CPPFLAGS="${CPPFLAGS} ${INCL}"


# Checks for header files.

AC_HEADER_STDC
AC_HEADER_DIRENT

AC_CHECK_HEADERS([unistd.h zlib.h], ,
  AC_MSG_ERROR([header file not found!]))


# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE


# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([strcasecmp strncasecmp],,
  [AC_MSG_ERROR([A necessary function could not be found.])])


# Check for compiler characteristics

# Do we have GNU C++ ?
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
  CFLAGS="${CFLAGS} -Wall -Wno-unused"

fi


# Output

if test "${datadir}" = '${prefix}/share'; then
  datadir='${prefix}/share/hatari'
fi

AC_SUBST(HOSTCC)

AC_OUTPUT(Makefile.cnf)


AC_MSG_NOTICE([+-----------------------------------------------------+])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| Configuration finished. Now you can compile Hatari. |])
AC_MSG_NOTICE([| Note that you have to use GNU-Make to build it.     |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([| - If you are using Linux then type:  make           |])
AC_MSG_NOTICE([| - If you are using *BSD then try :   gmake          |])
AC_MSG_NOTICE([|                                                     |])
AC_MSG_NOTICE([+-----------------------------------------------------+])
