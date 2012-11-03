AC_PREREQ(2.60)

AC_INIT([gnome-media],
        m4_esyscmd([GNOME_TAG_PATTERN=GNOME_MEDIA build-aux/git-version-gen .tarball-version]),
        [http://bugzilla.gnome.org/enter_bug.cgi?product=gnome-media])

AC_CONFIG_AUX_DIR([build-aux])

AM_INIT_AUTOMAKE([1.9 no-dist-gzip dist-bzip2 -Wall -Wno-portability tar-ustar foreign])

AS_VERSION

AM_MAINTAINER_MODE

if type -p stow > /dev/null && test -d /usr/local/stow ; then
   AC_MSG_NOTICE([*** Found /usr/local/stow: default install prefix set to /usr/local/stow/${PACKAGE}-${VERSION} ***])
   ac_default_prefix="/usr/local/stow/${PACKAGE}-${VERSION}"
fi

AM_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])

IT_PROG_INTLTOOL([0.35.0])

AC_PROG_CC
AC_ISC_POSIX

# Checks for programs.
AC_PROG_INSTALL
AC_PROG_LN_S
AC_PROG_MAKE_SET

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h limits.h malloc.h strings.h sys/ioctl.h unistd.h])
AC_CHECK_HEADERS([sys/sockio.h sys/param.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_C_CONST
AC_C_INLINE
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_PROG_GCC_TRADITIONAL
AC_FUNC_MMAP

# CFLAGS
GNOME_COMPILE_WARNINGS

# AS_NANO(GM_RELEASE="yes", GM_RELEASE="no")
AS_COMPILER_FLAG(-Wall, GM_ERROR_CFLAGS="-Wall")

if test "x$GM_RELEASE" = "xno"
then
  AS_COMPILER_FLAG(-Werror, GM_ERROR_CFLAGS="$GM_ERROR_CFLAGS -Werror")
fi
AC_MSG_NOTICE([Using $GM_ERROR_CFLAGS as error checking CFLAGS])

dnl=======================================================================
dnl Check for GStreamer and GSR deps
dnl=======================================================================

GST_REQ=0.11.92
GST_PLUGINS_BASE_REQ=0.11.92
GST_MAJORMINOR=1.0

dnl make available to Makefile.am
AC_SUBST(GST_MAJORMINOR)
AC_DEFINE_UNQUOTED(GST_MAJORMINOR_QUOTED, "$GST_MAJORMINOR",
  [major/minor of GStreamer as a string constant])

have_gst=no

PKG_CHECK_MODULES(GSR, [
  gstreamer-$GST_MAJORMINOR >= $GST_REQ
  gstreamer-plugins-base-$GST_MAJORMINOR >= $GST_REQ
  gtk+-3.0 >= 2.6.0
  gstreamer-pbutils-1.0
  gio-2.0],
  have_gst=yes,
  [
    AC_MSG_RESULT(no)
    AC_MSG_WARN($GSR_PKG_ERRORS)
    PROGRAMS_GRECORD_ERROR="$GSR_PKG_ERRORS"
  ])

have_grecord=no
grecord_reason=""

AC_ARG_ENABLE([grecord],
  AS_HELP_STRING([--enable-grecord], [enable gnome sound recorder]),
  ,enable_grecord=yes)

if test "x$have_gst" = "xyes" && test "x$enable_grecord" = "xyes"; then
  have_grecord=yes
else
  if test "x$have_gst" = "xno"; then
    AC_MSG_WARN([You need GStreamer installed for the recorder to work])
    sleep 5 2>/dev/null
  fi
fi

AC_SUBST(GSR_CFLAGS)
AC_SUBST(GSR_LIBS)
AM_CONDITIONAL(HAVE_GSTREAMER, [test x$have_gst = xyes])
AM_CONDITIONAL(HAVE_GRECORD, [test x$have_grecord = xyes])

dnl uninstalled gsr ui dir
AC_DEFINE_UNQUOTED(GSR_UIDIR_UNINSTALLED, "`pwd`/grecord/src",
  [path to uninstalled gnome-sound-recorder UI dir])

AC_PATH_PROG(GLIB_GENMARSHAL, glib-genmarshal)

dnl=======================================================================
dnl i18n
dnl=======================================================================

GETTEXT_PACKAGE=gnome-media-2.0
AC_SUBST(GETTEXT_PACKAGE)
AC_DEFINE_UNQUOTED(GETTEXT_PACKAGE, "$GETTEXT_PACKAGE", [Define to the Gettext package name])
AM_GLIB_GNU_GETTEXT

GNOME_DOC_INIT

AM_PROG_LIBTOOL
AM_PROG_CC_C_O

dnl ---------------------------------------------------------------------------
dnl - Finish
dnl ---------------------------------------------------------------------------

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

GNOME_MAINTAINER_MODE_DEFINES
GNOME_COMPILE_WARNINGS([maximum])
GNOME_CXX_WARNINGS

# Turn on the additional warnings last, so warnings don't affect other tests.
AC_ARG_ENABLE(more-warnings,
        [AC_HELP_STRING([--enable-more-warnings],
        [Maximum compiler warnings])],
        set_more_warnings="$enableval",[
                if test -d $srcdir/.git; then
                        set_more_warnings=yes
                else
                        set_more_warnings=no
                fi
        ])
AC_MSG_CHECKING(for more warnings)
if test "$GCC" = "yes" -a "$set_more_warnings" != "no"; then
        AC_MSG_RESULT(yes)
        CFLAGS="\
        -Wall \
        -Wchar-subscripts -Wmissing-declarations -Wmissing-prototypes \
        -Wnested-externs -Wpointer-arith \
        -Wcast-align -Wsign-compare \
        $CFLAGS"

        for option in -Wno-unused-parameter -Wno-strict-aliasing -Wno-sign-compare; do
                SAVE_CFLAGS="$CFLAGS"
                CFLAGS="$CFLAGS $option"
                AC_MSG_CHECKING([whether gcc understands $option])
                AC_TRY_COMPILE([], [],
                        has_option=yes,
                        has_option=no,)
                if test $has_option = no; then
                        CFLAGS="$SAVE_CFLAGS"
                fi
                AC_MSG_RESULT($has_option)
                unset has_option
                unset SAVE_CFLAGS
        done
        unset option
else
        AC_MSG_RESULT(no)
fi


AC_SUBST(CFLAGS)
AC_SUBST(CPPFLAGS)
AC_SUBST(LIBS)
AC_SUBST(LDFLAGS)

AC_CONFIG_FILES([
Makefile
po/Makefile.in
grecord/Makefile
grecord/src/Makefile
grecord/doc/Makefile
grecord/icons/Makefile
grecord/icons/16x16/Makefile
grecord/icons/22x22/Makefile
grecord/icons/24x24/Makefile
grecord/icons/32x32/Makefile
grecord/icons/48x48/Makefile
grecord/icons/scalable/Makefile
grecord/gnome-sound-recorder.desktop.in
])

AC_OUTPUT

echo "

                    ${PACKAGE} ${VERSION}
                    ============


        Prefix:                 ${prefix}
        Source code location:   ${srcdir}
        Compiler:               ${CC}
        CFLAGS:                 ${CFLAGS}

        Sound recorder:         $have_grecord$grecord_reason
"

