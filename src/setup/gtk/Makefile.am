INCLUDES = -I$(top_srcdir)/src/include -I$(top_srcdir)/src/setup/lib

AM_CFLAGS = $(GTK_CFLAGS)

plugindir = $(libdir)/GNUnet

plugin_LTLIBRARIES = \
  libgnunetsetup_gtk.la

pkgdata_DATA = \
  gnunet-setup.glade

EXTRA_DIST = \
	$(pkgdata_DATA) \
        about.c ngconf.c gconf.h

if HAVE_GUILE
GUILE_SRC = \
	 about.c \
	 ngconf.c gconf.h
endif


libgnunetsetup_gtk_la_SOURCES = \
 $(GUILE_SRC) \
 glade_support.h glade_support.c \
 wizard_gtk.c wizard_gtk.h
libgnunetsetup_gtk_la_LDFLAGS = \
 $(GN_PLUGIN_LDFLAGS)
libgnunetsetup_gtk_la_CFLAGS = \
 -I$(top_scrdir)/include \
 @GNUNETGTK_CFLAGS@ \
 @GTK_CFLAGS@ 
libgnunetsetup_gtk_la_LIBADD = \
 @GTK_LIBS@ @EXT_LIB_PATH@ @EXT_LIBS@ \
 @GNUNETGTK_LIBS@ \
  -lgthread-2.0 \
 $(top_builddir)/src/util/libgnunetutil.la \
 $(top_builddir)/src/setup/lib/libgnunetsetup.la \
 $(GN_LIBINTL)

