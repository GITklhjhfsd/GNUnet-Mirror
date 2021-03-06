/*
     This file is part of GNUnet.
     (C) 2006 Christian Grothoff (and other contributing authors)

     GNUnet is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published
     by the Free Software Foundation; either version 2, or (at your
     option) any later version.

     GNUnet is distributed in the hope that it will be useful, but
     WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
     General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with GNUnet; see the file COPYING.  If not, write to the
     Free Software Foundation, Inc., 59 Temple Place - Suite 330,
     Boston, MA 02111-1307, USA.
*/

/**
 * @file src/util/os/installpath.c
 * @brief get paths used by the program
 * @author Milan
 */

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "platform.h"
#include "gnunet_util_string.h"
#include "gnunet_util_config.h"
#include "gnunet_util_disk.h"
#include "gnunet_util_os.h"
#if OSX
#include <mach-o/ldsyms.h>
#include <mach-o/dyld.h>
#endif

#if LINUX
/**
 * Try to determine path by reading /proc/PID/exe
 */
static char *
get_path_from_proc_maps ()
{
  char fn[64];
  char *line;
  char *dir;
  FILE * f;

  GNUNET_snprintf (fn, 64, "/proc/%u/maps", getpid());
  line = GNUNET_malloc (1024);
  dir = GNUNET_malloc (1024);
  f = fopen(fn, "r");
  if (f != NULL) {
    while (NULL != fgets(line, 1024, f)) {
      if ( (1 == sscanf(line,
			"%*x-%*x %*c%*c%*c%*c %*x %*2u:%*2u %*u%*[ ]%s",
			dir)) &&
	   (NULL != strstr(dir,
			   "libgnunetutil")) ) {
	strstr(dir, "libgnunetutil")[0] = '\0';
	fclose(f);
        GNUNET_free (line);
	return dir;
      }
    }
    fclose(f);
  }
  GNUNET_free (dir);
  GNUNET_free (line);
  return NULL;
}

/**
 * Try to determine path by reading /proc/PID/exe
 */
static char *
get_path_from_proc_exe ()
{
  char fn[64];
  char *lnk;
  size_t size;

  GNUNET_snprintf (fn, 64, "/proc/%u/exe", getpid ());
  lnk = GNUNET_malloc (1024);
  size = readlink (fn, lnk, 1023);
  if ((size == 0) || (size >= 1024))
    {
      GNUNET_GE_LOG_STRERROR_FILE (NULL,
                                   GNUNET_GE_ERROR | GNUNET_GE_USER |
                                   GNUNET_GE_ADMIN | GNUNET_GE_IMMEDIATE,
                                   "readlink", fn);
      GNUNET_free (lnk);
      return NULL;
    }
  lnk[size] = '\0';
  while ((lnk[size] != '/') && (size > 0))
    size--;
  if ((size < 4) || (lnk[size - 4] != '/'))
    {
      /* not installed in "/bin/" -- binary path probably useless */
      GNUNET_free (lnk);
      return NULL;
    }
  lnk[size] = '\0';
  return lnk;
}
#endif

#if WINDOWS
/**
 * Try to determine path with win32-specific function
 */
static char *
get_path_from_module_filename ()
{
  char *path;
  char *idx;

  path = GNUNET_malloc (4097);
  GetModuleFileName (NULL, path, 4096);
  idx = path + strlen (path);
  while ((idx > path) && (*idx != '\\') && (*idx != '/'))
    idx--;
  *idx = '\0';
  return path;
}
#endif

#if OSX
typedef int (*MyNSGetExecutablePathProto) (char *buf, size_t * bufsize);

static char *
get_path_from_NSGetExecutablePath ()
{
  static char zero = '\0';
  char *path;
  size_t len;
  MyNSGetExecutablePathProto func;
  int ret;

  path = NULL;
  func =
    (MyNSGetExecutablePathProto) dlsym (RTLD_DEFAULT, "_NSGetExecutablePath");
  if (!func)
    return NULL;
  path = &zero;
  len = 0;
  /* get the path len, including the trailing \0 */
  func (path, &len);
  if (len == 0)
    return NULL;
  path = GNUNET_malloc (len);
  ret = func (path, &len);
  if (ret != 0)
    {
      GNUNET_free (path);
      return NULL;
    }
  len = strlen (path);
  while ((path[len] != '/') && (len > 0))
    len--;
  path[len] = '\0';
  return path;
}

static char * get_path_from_dyld_image() {
  const char * path;
  char * p, * s;
  int i;
  int c;

  p = NULL;
  c = _dyld_image_count();
  for (i = 0; i < c; i++) {
    if (_dyld_get_image_header(i) == &_mh_dylib_header) {
      path = _dyld_get_image_name(i);
      if (path != NULL && strlen(path) > 0) {
        p = strdup(path);
        s = p + strlen(p);
        while ( (s > p) && (*s != '/') )
          s--;
        s++;
        *s = '\0';
      }
      break;
    }
  }
  return p;
}
#endif

static char *
get_path_from_PATH ()
{
  char *path;
  char *pos;
  char *end;
  char *buf;
  const char *p;
  size_t size;

  p = getenv ("PATH");
  if (p == NULL)
    return NULL;
  path = GNUNET_strdup (p);     /* because we write on it */
  buf = GNUNET_malloc (strlen (path) + 20);
  size = strlen (path);
  pos = path;

  while (NULL != (end = strchr (pos, ':')))
    {
      *end = '\0';
      sprintf (buf, "%s/%s", pos, "gnunetd");
      if (GNUNET_disk_file_test (NULL, buf) == GNUNET_YES)
        {
          pos = GNUNET_strdup (pos);
          GNUNET_free (buf);
          GNUNET_free (path);
          return pos;
        }
      pos = end + 1;
    }
  sprintf (buf, "%s/%s", pos, "gnunetd");
  if (GNUNET_disk_file_test (NULL, buf) == GNUNET_YES)
    {
      pos = GNUNET_strdup (pos);
      GNUNET_free (buf);
      GNUNET_free (path);
      return pos;
    }
  GNUNET_free (buf);
  GNUNET_free (path);
  return NULL;
}

static char *
get_path_from_GNUNET_PREFIX ()
{
  const char *p;

  p = getenv ("GNUNET_PREFIX");
  if (p != NULL)
    return GNUNET_strdup (p);
  return NULL;
}

/*
 * @brief get the path to GNUnet bin/ or lib/, prefering the lib/ path
 * @author Milan
 *
 * @return a pointer to the executable path, or NULL on error
 */
static char *
os_get_gnunet_path ()
{
  char *ret;

  ret = get_path_from_GNUNET_PREFIX ();
  if (ret != NULL)
    return ret;
#if LINUX
  ret = get_path_from_proc_maps ();
  if (ret != NULL)
    return ret;
  ret = get_path_from_proc_exe ();
  if (ret != NULL)
    return ret;
#endif
#if WINDOWS
  ret = get_path_from_module_filename ();
  if (ret != NULL)
    return ret;
#endif
#if OSX
  ret = get_path_from_dyld_image ();
  if (ret != NULL)
    return ret;
  ret = get_path_from_NSGetExecutablePath ();
  if (ret != NULL)
    return ret;
#endif
  ret = get_path_from_PATH ();
  if (ret != NULL)
    return ret;
  /* other attempts here */
  return NULL;
}

/*
 * @brief get the path to current app's bin/
 * @author Milan
 *
 * @return a pointer to the executable path, or NULL on error
 */
static char *
os_get_exec_path ()
{
  char *ret;

#if LINUX
  ret = get_path_from_proc_exe ();
  if (ret != NULL)
    return ret;
#endif
#if WINDOWS
  ret = get_path_from_module_filename ();
  if (ret != NULL)
    return ret;
#endif
#if OSX
  ret = get_path_from_NSGetExecutablePath ();
  if (ret != NULL)
    return ret;
#endif
  /* other attempts here */
  return NULL;
}



/*
 * @brief get the path to a specific GNUnet installation directory or,
 * with GNUNET_IPK_SELF_PREFIX, the current running apps installation directory
 * @author Milan
 * @return a pointer to the dir path (to be freed by the caller)
 */
char *
GNUNET_get_installation_path (enum GNUNET_INSTALL_PATH_KIND dirkind)
{
  size_t n;
  const char *dirname;
  char *execpath = NULL;
  char *tmp;
  int isbasedir;

  /* if wanted, try to get the current app's bin/ */
  if (dirkind == GNUNET_IPK_SELF_PREFIX)
    execpath = os_get_exec_path ();
  
  /* try to get GNUnet's bin/ or lib/, or if previous was unsuccessful some
   * guess for the current app */
  if (execpath == NULL)
    execpath = os_get_gnunet_path ();

  if (execpath == NULL)
    return NULL;

  n = strlen (execpath);
  if (n == 0)
    {
      /* should never happen, but better safe than sorry */
      GNUNET_free (execpath);
      return NULL;
    }
  while (n > 1 && execpath[n - 1] == DIR_SEPARATOR)
    execpath[--n] = '\0';

  isbasedir = 1;
  if ((n > 5) && ((0 == strcasecmp (&execpath[n - 5], "lib32")) ||
                  (0 == strcasecmp (&execpath[n - 5], "lib64"))))
    {
      if (dirkind != GNUNET_IPK_LIBDIR)
        {
          /* strip '/lib32/' or '/lib64/' */
          execpath[n - 5] = '\0';
          n -= 5;
        }
      else
        isbasedir = 0;
    }
  else if ((n > 3) && ((0 == strcasecmp (&execpath[n - 3], "bin")) ||
                  (0 == strcasecmp (&execpath[n - 3], "lib"))))
    {
      if (dirkind != GNUNET_IPK_LIBDIR || 
          (0 != strcasecmp (&execpath[n - 3], "lib")) )
        {
          /* strip '/bin/' or '/lib/' */
          execpath[n - 3] = '\0';
          n -= 3;
        }
      else
        isbasedir = 0;
    }
  while (n > 1 && execpath[n - 1] == DIR_SEPARATOR)
    execpath[--n] = '\0';

  switch (dirkind)
    {
    case GNUNET_IPK_PREFIX:
    case GNUNET_IPK_SELF_PREFIX:
      dirname = DIR_SEPARATOR_STR;
      break;
    case GNUNET_IPK_BINDIR:
      dirname = DIR_SEPARATOR_STR "bin" DIR_SEPARATOR_STR;
      break;
    case GNUNET_IPK_LIBDIR:
      if (isbasedir)
        dirname =
          DIR_SEPARATOR_STR "lib" DIR_SEPARATOR_STR "GNUnet" DIR_SEPARATOR_STR;
      else
        dirname =
          DIR_SEPARATOR_STR "GNUnet" DIR_SEPARATOR_STR;
      break;
    case GNUNET_IPK_DATADIR:
      dirname =
        DIR_SEPARATOR_STR "share" DIR_SEPARATOR_STR "GNUnet"
        DIR_SEPARATOR_STR;
      break;
    case GNUNET_IPK_LOCALEDIR:
      dirname =
        DIR_SEPARATOR_STR "share" DIR_SEPARATOR_STR "locale"
        DIR_SEPARATOR_STR;
      break;
    default:
      GNUNET_free (execpath);
      return NULL;
    }
  tmp = GNUNET_malloc (strlen (execpath) + strlen (dirname) + 1);
  sprintf (tmp, "%s%s", execpath, dirname);
  GNUNET_free (execpath);
  return tmp;
}

#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif
/* end of installpath.c */
