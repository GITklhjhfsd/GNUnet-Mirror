INCLUDES = -I$(top_srcdir)/src/include


if USE_COVERAGE
  AM_CFLAGS = -fprofile-arcs -ftest-coverage
endif

lib_LTLIBRARIES = \
  libgnunetecrs.la 

libgnunetecrs_la_SOURCES = \
  directory.c \
  download.c \
  ecrs.c ecrs.h \
  helper.c \
  indexinfo.c \
  keyspace.c \
  namespace.c \
  parser.c \
  search.c \
  tree.c tree.h \
  unindex.c \
  upload.c \
  uri.c
libgnunetecrs_la_LDFLAGS = \
  $(GN_LIB_LDFLAGS)
libgnunetecrs_la_LIBADD = \
  -lz \
  -lextractor \
  $(top_builddir)/src/applications/identity/libgnunetidentityapi.la \
  $(top_builddir)/src/applications/fs/libgnunetecrscore.la \
  $(top_builddir)/src/applications/fs/lib/libgnunetfs.la \
  $(top_builddir)/src/applications/getoption/libgnunetgetoptionapi.la \
  $(top_builddir)/src/util/libgnunetutil.la \
  $(GN_LIBINTL) 


check_PROGRAMS = \
  bincodertest \
  downloadtest \
  namespacetest \
  uritest \
  searchtest \
  directorytest \
  ecrstest \
  updowntest 

TESTS = $(check_PROGRAMS)

bincodertest_SOURCES = \
  bincodertest.c 
bincodertest_LDADD = \
  $(top_builddir)/src/util/libgnunetutil.la 

uritest_SOURCES = \
  uritest.c 
uritest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

namespacetest_SOURCES = \
  namespacetest.c 
namespacetest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

ecrstest_SOURCES = \
  ecrstest.c
ecrstest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

updowntest_SOURCES = \
  updowntest.c
updowntest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

downloadtest_SOURCES = \
  downloadtest.c
downloadtest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

searchtest_SOURCES = \
  searchtest.c
searchtest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

directorytest_SOURCES = \
  directorytest.c
directorytest_LDADD = \
  $(top_builddir)/src/applications/fs/ecrs/libgnunetecrs.la 

EXTRA_DIST = \
  check.conf peer.conf bincoder.c
