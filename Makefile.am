NULL =

SUBDIRS = po src doc icons

DISTCHECK_CONFIGURE_FLAGS = \
	--disable-scrollkeeper	\
	--enable-more-warnings  \
	--enable-compile-warnings=maximum

DIST_SUBDIRS = \
	po 			\
	$(NULL)

EXTRA_DIST = \
	.version		\
	autogen.sh		\
	configure		\
	COPYING			\
	COPYING-DOCS		\
	COPYING.grecord		\
	MAINTAINERS		\
        build-aux/git-version-gen \
	build-aux/gnome-doc-utils.make	\
	m4/as-compiler-flag.m4	\
	m4/as-version.m4	\
	ChangeLog.pre-2-26	\
	gnome-sound-recorder.desktop.in.in \
	README AUTHORS INSTALL \
	$(INTLTOOL_BUILT:=.in)	\
	$(NULL)

Applicationsdir = $(datadir)/applications
Applications_in_files = gnome-sound-recorder.desktop.in.in
Applications_DATA = $(Applications_in_files:.desktop.in.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

CLEANFILES=$(Applications_DATA)

DISTCLEANFILES = \
	$(Applications_in_files:.desktop.in.in=.desktop.in) \
	build-aux/gnome-doc-utils.make	\
	po/.intltool-merge-cache	\
	$(NULL)

ACLOCAL_AMFLAGS = -I m4

# see build-aux/git-version-get for details
BUILT_SOURCES = $(top_srcdir)/.version
$(top_srcdir)/.version:
	echo $(VERSION) > $@-t && mv $@-t $@
dist-hook:
	echo $(VERSION) > $(distdir)/.tarball-version

-include $(top_srcdir)/git.mk
