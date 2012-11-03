SUBDIRS = src doc icons

EXTRA_DIST = \
	README AUTHORS INSTALL ChangeLog.pre-2-26 \
	gnome-sound-recorder.desktop.in.in


Applicationsdir = $(datadir)/applications
Applications_in_files = gnome-sound-recorder.desktop.in.in
Applications_DATA = $(Applications_in_files:.desktop.in.in=.desktop)

@INTLTOOL_DESKTOP_RULE@

CLEANFILES=$(Applications_DATA)
DISTCLEANFILES=$(Applications_in_files:.desktop.in.in=.desktop.in)

install-data-local:

-include $(top_srcdir)/git.mk
