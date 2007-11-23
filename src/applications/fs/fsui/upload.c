/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2003, 2004, 2006 Christian Grothoff (and other contributing authors)

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
 * @file applications/fs/fsui/upload.c
 * @brief upload functions
 * @author Krista Bennett
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util_error_loggers.h"
#include "gnunet_util_network_client.h"
#include "gnunet_ecrs_lib.h"
#include "gnunet_uritrack_lib.h"
#include "gnunet_fsui_lib.h"
#include "gnunet_identity_lib.h"
#include "fsui.h"
#include <extractor.h>

#define DEBUG_UPLOAD GNUNET_NO

/**
 * Transform an ECRS progress callback into an FSUI event.
 *
 * @param direct is this a direct ECRS trigger, or a recursive
 *        call from a child signaling progress to the parent?
 */
static void
progressCallbackR (unsigned long long totalBytes,
                   unsigned long long completedBytes, GNUNET_CronTime eta,
                   void *ptr, int direct, int add, int unaccounted)
{
  FSUI_UploadList *utc = ptr;
  FSUI_Event event;
  unsigned long long subtotal;
  FSUI_UploadList *pos;
  GNUNET_CronTime xeta;
  GNUNET_CronTime now;

  event.type = FSUI_upload_progress;
  event.data.UploadProgress.uc.pos = utc;
  event.data.UploadProgress.uc.cctx = utc->cctx;
  event.data.UploadProgress.uc.ppos = utc->parent;
  event.data.UploadProgress.uc.pcctx = utc->parent->cctx;
  if (GNUNET_YES == ECRS_isDirectory (utc->meta))
    {
      if (direct == GNUNET_YES)
        unaccounted = GNUNET_YES;
      if ((direct == GNUNET_YES) && (totalBytes == completedBytes))
        add = GNUNET_YES;
      if (add == GNUNET_NO)
        {
          event.data.UploadProgress.completed =
            completedBytes + utc->completed;
          event.data.UploadProgress.total =
            utc->total + ((unaccounted == GNUNET_NO) ? 0 : totalBytes);
          if (totalBytes == completedBytes)
            utc->completed += completedBytes;
        }
      else
        {
          GE_ASSERT (NULL, totalBytes == completedBytes);
          event.data.UploadProgress.completed =
            completedBytes + utc->completed;
          event.data.UploadProgress.total = totalBytes + utc->total;
          utc->total += completedBytes;
          utc->completed += completedBytes;
        }
    }
  else
    {
      /* simple file upload */
      event.data.UploadProgress.completed = completedBytes;
      event.data.UploadProgress.total = totalBytes;
      utc->completed = completedBytes;
    }
  event.data.UploadProgress.eta = eta;
  event.data.UploadProgress.filename = utc->filename;
  utc->shared->ctx->ecb (utc->shared->ctx->ecbClosure, &event);
  if (utc->parent != &utc->shared->ctx->activeUploads)
    {
      subtotal = 0;
      pos = utc->parent->child;
      while (pos != NULL)
        {
          subtotal += pos->completed;
          pos = pos->next;
        }
      now = GNUNET_get_time ();
      xeta = now;
      if (subtotal > 0)
        {
          xeta = (GNUNET_CronTime) (utc->parent->start_time +
                                    (((double) (now - utc->parent->start_time)
                                      / (double) subtotal)) *
                                    (double) utc->parent->total);
        }
      progressCallbackR (totalBytes, completedBytes, xeta, utc->parent,
                         GNUNET_NO, add, unaccounted);
    }
}

/**
 * Transform an ECRS progress callback into an FSUI event.
 */
static void
progressCallback (unsigned long long totalBytes,
                  unsigned long long completedBytes, GNUNET_CronTime eta,
                  void *ptr)
{
  progressCallbackR (totalBytes, completedBytes, eta, ptr, GNUNET_YES,
                     GNUNET_NO, GNUNET_NO);
}

static int
testTerminate (void *cls)
{
  FSUI_UploadList *utc = cls;
  if (utc->state != FSUI_ACTIVE)
    return GNUNET_SYSERR;
  return GNUNET_OK;
}

/**
 * Take the current directory entries from utc, create
 * a directory, upload it and store the uri in  *uri.
 */
static char *
createDirectoryHelper (struct GE_Context *ectx,
                       struct GC_Configuration *cfg,
                       struct FSUI_UploadList *children,
                       struct ECRS_MetaData *meta, char **error)
{
  ECRS_FileInfo *fis;
  unsigned int count;
  unsigned int size;
  char *data;
  unsigned long long len;
  int ret;
  char *tempName;
  struct FSUI_UploadList *pos;
  int handle;
  struct GE_Memory *mem;
  struct GE_Context *ee;

  fis = NULL;
  size = 0;
  count = 0;
  pos = children;
  while (pos != NULL)
    {
      if (pos->uri != NULL)
        count++;
      pos = pos->next;
    }
  GNUNET_array_grow (fis, size, count);
  count = 0;
  pos = children;
  while (pos != NULL)
    {
      if (pos->uri != NULL)
        {
          fis[count].uri = pos->uri;
          fis[count].meta = pos->meta;
          count++;
        }
      pos = pos->next;
    }
  GE_BREAK (ectx, count == size);
  mem = GE_memory_create (2);
  ee =
    GE_create_context_memory (GE_USER | GE_ADMIN | GE_ERROR | GE_WARNING |
                              GE_FATAL | GE_BULK | GE_IMMEDIATE, mem);
  ret = ECRS_createDirectory (ee, &data, &len, size, fis, meta);
  GNUNET_array_grow (fis, size, 0);
  if (ret != GNUNET_OK)
    {
      *error = GNUNET_strdup (GE_memory_get (mem, 0));
      GE_free_context (ee);
      GE_memory_free (mem);
      return NULL;
    }
  pos = children;
  while (pos != NULL)
    {
      if (pos->uri != NULL)
        URITRACK_addState (ectx, cfg, pos->uri, URITRACK_DIRECTORY_ADDED);
      pos = pos->next;
    }
  GE_memory_reset (mem);
  tempName = GNUNET_strdup ("/tmp/gnunet-upload-dir.XXXXXX");
  handle = mkstemp (tempName);
  if (handle == -1)
    {
      GE_LOG_STRERROR_FILE (ee,
                            GE_ERROR | GE_USER | GE_BULK,
                            "mkstemp", tempName);
      GNUNET_free (tempName);
      GNUNET_free (data);
      *error = GNUNET_strdup (GE_memory_get (mem, 0));
      GE_free_context (ee);
      GE_memory_free (mem);
      return NULL;
    }
  if (len != WRITE (handle, data, len))
    {
      GE_LOG_STRERROR_FILE (ee,
                            GE_ERROR | GE_USER | GE_BULK, "write", tempName);
      *error = GNUNET_strdup (GE_memory_get (mem, 0));
      GE_free_context (ee);
      GE_memory_free (mem);
      GNUNET_free (data);
      return NULL;
    }
  GE_free_context (ee);
  GE_memory_free (mem);
  CLOSE (handle);
  GNUNET_free (data);
  return tempName;
}

/**
 * Signal upload error to client.
 */
static void
signalError (FSUI_UploadList * utc, const char *message)
{
  FSUI_Event event;

  utc->state = FSUI_ERROR;
  event.type = FSUI_upload_error;
  event.data.UploadError.uc.pos = utc;
  event.data.UploadError.uc.cctx = utc->cctx;
  event.data.UploadError.uc.ppos = utc->parent;
  event.data.UploadError.uc.pcctx = utc->parent->cctx;
  event.data.UploadError.message = message;
  utc->shared->ctx->ecb (utc->shared->ctx->ecbClosure, &event);
}

static void
signalUploadStarted (struct FSUI_UploadList *utc, int first_only)
{
  FSUI_Event event;

  while (utc != NULL)
    {
      event.type = FSUI_upload_started;
      event.data.UploadStarted.uc.pos = utc;
      event.data.UploadStarted.uc.cctx = utc->cctx;
      event.data.UploadStarted.uc.ppos = utc->parent;
      event.data.UploadStarted.uc.pcctx = utc->parent->cctx;
      event.data.UploadStarted.total = utc->total;
      event.data.UploadStarted.anonymityLevel = utc->shared->anonymityLevel;
      event.data.UploadStarted.filename = utc->filename;
      utc->cctx = utc->shared->ctx->ecb (utc->shared->ctx->ecbClosure,
                                         &event);
      signalUploadStarted (utc->child, 0);
      if (first_only)
        break;
      utc = utc->next;
    }
}

/**
 * Thread that does the upload.
 */
void *
FSUI_uploadThread (void *cls)
{
  FSUI_UploadList *utc = cls;
  FSUI_UploadList *cpos;
  FSUI_Event event;
  ECRS_FileInfo fi;
  int ret;
  struct GE_Context *ectx;
  char *filename;
  char *pfn;
  struct ECRS_URI *uri;
  struct ECRS_URI *loc;
  size_t tpos;
  char *error;
  struct GE_Memory *mem;
  struct GE_Context *ee;


  ectx = utc->shared->ctx->ectx;
  GE_ASSERT (ectx, utc->filename != NULL);
  cpos = utc->child;
  while (cpos != NULL)
    {
      if (cpos->state == FSUI_ACTIVE)
        FSUI_uploadThread (cpos);
      cpos = cpos->next;
    }
  if (utc->state != FSUI_ACTIVE)
    return NULL;                /* aborted or suspended */
  if (GNUNET_YES == GNUNET_disk_directory_test (ectx, utc->filename))
    {
      error = NULL;
      filename = createDirectoryHelper (ectx,
                                        utc->shared->ctx->cfg,
                                        utc->child, utc->meta, &error);
      if (filename == NULL)
        {
          if (error == NULL)
            error =
              GNUNET_strdup (_("Failed to create temporary directory."));
          signalError (utc, error);
          GNUNET_free (error);
          return NULL;
        }
    }
  else
    {
      filename = GNUNET_strdup (utc->filename);
    }
  utc->start_time = GNUNET_get_time ();
  mem = GE_memory_create (2);
  ee =
    GE_create_context_memory (GE_USER | GE_ADMIN | GE_ERROR | GE_WARNING |
                              GE_FATAL | GE_BULK | GE_IMMEDIATE, mem);
  ret =
    ECRS_uploadFile (ee, utc->shared->ctx->cfg, filename,
                     utc->shared->doIndex == GNUNET_YES ? (utc->child ==
                                                           NULL ? GNUNET_YES :
                                                           GNUNET_NO) :
                     GNUNET_NO, utc->shared->anonymityLevel,
                     utc->shared->priority, utc->shared->expiration,
                     &progressCallback, utc, &testTerminate, utc, &utc->uri);
  if (ret != GNUNET_OK)
    {
      if (utc->state == FSUI_ACTIVE)
        {
          const char *err;

          err = GE_memory_get (mem, 0);
          signalError (utc, err ? err : "");
        }
      else if (utc->state == FSUI_ABORTED)
        {
          event.type = FSUI_upload_aborted;
          event.data.UploadAborted.uc.pos = utc;
          event.data.UploadAborted.uc.cctx = utc->cctx;
          event.data.UploadAborted.uc.ppos = utc->parent;
          event.data.UploadAborted.uc.pcctx = utc->parent->cctx;
          utc->shared->ctx->ecb (utc->shared->ctx->ecbClosure, &event);
        }
      else
        {
          /* must be suspended */
          GE_BREAK (NULL, utc->state == FSUI_PENDING);
        }
      if (utc->child != NULL)
        UNLINK (filename);
      GNUNET_free (filename);
      GE_free_context (ee);
      GE_memory_free (mem);
      return NULL;
    }
  utc->state = FSUI_COMPLETED;
  if (utc->child == NULL)
    ECRS_extractMetaData (utc->shared->ctx->ectx,
                          utc->meta, utc->filename, utc->shared->extractors);
  while (GNUNET_OK ==
         ECRS_delFromMetaData (utc->meta, EXTRACTOR_FILENAME, NULL));
  /* only publish the last part of the path
     -- we do not want to publish $HOME or similar
     trivially deanonymizing information */
  tpos = strlen (utc->filename) - 1;
  if ((utc->filename[tpos] == DIR_SEPARATOR) && (tpos > 0))
    tpos--;
  while ((tpos > 0) && (utc->filename[tpos] != DIR_SEPARATOR))
    tpos--;
  pfn = GNUNET_malloc (strlen (&utc->filename[tpos + 1]) + 2);
  strcpy (pfn, &utc->filename[tpos + 1]);
  if ((utc->child != NULL) &&
      ((strlen (pfn) == 0) || (pfn[strlen (pfn) - 1] != DIR_SEPARATOR)))
    strcat (pfn, DIR_SEPARATOR_STR);
  ECRS_addToMetaData (utc->meta, EXTRACTOR_FILENAME, pfn);
  GNUNET_free (pfn);
  if ((utc->shared->anonymityLevel == 0)
      && (utc->shared->doIndex == GNUNET_YES))
    {
      /* generate location URI for non-anonymous download */
      struct GNUNET_ClientServerConnection *sock;
      GNUNET_MessageHello *hello;

      sock = GNUNET_client_connection_create (utc->shared->ctx->ectx,
                                              utc->shared->ctx->cfg);

      if (GNUNET_OK == GNUNET_IDENTITY_get_self (sock, &hello))
        {
          loc = ECRS_uriFromLocation (utc->uri,
                                      &hello->publicKey,
                                      ntohl (hello->expirationTime),
                                      (ECRS_SignFunction) &
                                      GNUNET_IDENTITY_sign_function, sock);

          GNUNET_free (hello);
        }
      else
        {
          /* may happen if no transports are available... */
          loc = ECRS_dupUri (utc->uri);
        }
      GNUNET_client_connection_destroy (sock);
    }
  else
    {
      /* no location URI, use standard URI
         (copied here to allow free later) */
      loc = ECRS_dupUri (utc->uri);
    }
  if (utc->shared->global_keywords != NULL)
    ECRS_addToKeyspace (ectx,
                        utc->shared->ctx->cfg,
                        utc->shared->global_keywords,
                        utc->shared->anonymityLevel,
                        utc->shared->priority,
                        utc->shared->expiration, loc, utc->meta);
  if (utc->keywords != NULL)
    ECRS_addToKeyspace (ectx,
                        utc->shared->ctx->cfg,
                        utc->keywords,
                        utc->shared->anonymityLevel,
                        utc->shared->priority,
                        utc->shared->expiration, loc, utc->meta);
  if (utc->shared->individualKeywords == GNUNET_YES)
    {
      uri = ECRS_metaDataToUri (utc->meta);
      ECRS_addToKeyspace (ectx,
                          utc->shared->ctx->cfg,
                          uri,
                          utc->shared->anonymityLevel,
                          utc->shared->priority,
                          utc->shared->expiration, loc, utc->meta);
      ECRS_freeUri (uri);
    }
  ECRS_freeUri (loc);
  loc = NULL;
  while (GNUNET_OK ==
         ECRS_delFromMetaData (utc->meta, EXTRACTOR_SPLIT, NULL));
  fi.meta = utc->meta;
  fi.uri = utc->uri;
  URITRACK_trackURI (ectx, utc->shared->ctx->cfg, &fi);
  URITRACK_addState (ectx,
                     utc->shared->ctx->cfg,
                     utc->uri,
                     utc->shared->doIndex ==
                     GNUNET_YES ? URITRACK_INDEXED : URITRACK_INSERTED);
  event.type = FSUI_upload_completed;
  event.data.UploadCompleted.uc.pos = utc;
  event.data.UploadCompleted.uc.cctx = utc->cctx;
  event.data.UploadCompleted.uc.ppos = utc->parent;
  event.data.UploadCompleted.uc.pcctx = utc->parent->cctx;
  event.data.UploadCompleted.total = utc->total;
  event.data.UploadCompleted.filename = utc->filename;
  event.data.UploadCompleted.uri = utc->uri;
  utc->shared->ctx->ecb (utc->shared->ctx->ecbClosure, &event);
  if (utc->child != NULL)
    UNLINK (filename);
  GNUNET_free (filename);
  GE_free_context (ee);
  GE_memory_free (mem);
  return NULL;
}

/**
 * Thread that does the upload.
 */
static void *
FSUI_uploadThreadEvent (void *cls)
{
  FSUI_UploadList *utc = cls;

  if (utc->parent == &utc->shared->ctx->activeUploads)
    {
      /* top-level call: signal client! */
      signalUploadStarted (utc, 1);
    }
  return FSUI_uploadThread (utc);
}


static void
freeUploadList (struct FSUI_UploadList *ul)
{
  struct FSUI_UploadList *next;
  struct FSUI_Context *ctx;

  ctx = ul->shared->ctx;
  while (ul->child != NULL)
    freeUploadList (ul->child);
  GNUNET_mutex_lock (ctx->lock);
  GNUNET_free (ul->filename);
  if (ul->keywords != NULL)
    ECRS_freeUri (ul->keywords);
  if (ul->uri != NULL)
    ECRS_freeUri (ul->uri);
  if (ul->meta != NULL)
    ECRS_freeMetaData (ul->meta);
  /* unlink from parent */
  next = ul->parent->child;
  if (next == NULL)
    {
      GE_BREAK (NULL, 0);
      GNUNET_mutex_unlock (ctx->lock);
      return;
    }
  if (next == ul)
    {
      ul->parent->child = ul->next;
    }
  else
    {
      while (next->next != ul)
        {
          next = next->next;
          if (next == NULL)
            {
              GE_BREAK (NULL, 0);
              GNUNET_mutex_unlock (ctx->lock);
              return;
            }
        }
      next->next = ul->next;
    }
  GNUNET_free (ul);
  GNUNET_mutex_unlock (ctx->lock);
}

static struct FSUI_UploadList *addUploads (struct FSUI_UploadShared *shared,
                                           const char *filename,
                                           const struct ECRS_URI *keywords,
                                           const struct ECRS_MetaData *md,
                                           struct FSUI_UploadList *parent);

static int
addChildUpload (const char *name, const char *dirName, void *data)
{
  struct FSUI_UploadList *parent = data;
  char *filename;
  struct FSUI_UploadList *child;
  struct ECRS_MetaData *md;

  filename = GNUNET_malloc (strlen (dirName) + strlen (name) + 2);
  strcpy (filename, dirName);
  strcat (filename, DIR_SEPARATOR_STR);
  strcat (filename, name);
  md = ECRS_createMetaData ();
  child = addUploads (parent->shared, filename, NULL, md, parent);
  GNUNET_free (filename);
  ECRS_freeMetaData (md);
  if (child == NULL)
    return GNUNET_SYSERR;
  parent->total += child->total;
  return GNUNET_OK;
}

static struct FSUI_UploadList *
addUploads (struct FSUI_UploadShared *shared,
            const char *filename,
            const struct ECRS_URI *keywords,
            const struct ECRS_MetaData *md, struct FSUI_UploadList *parent)
{
  FSUI_UploadList *utc;

  utc = GNUNET_malloc (sizeof (FSUI_UploadList));
  utc->completed = 0;
  utc->total = 0;               /* to be set later */
  utc->start_time = GNUNET_get_time ();
  utc->shared = shared;
  utc->next = NULL;
  utc->child = NULL;
  utc->parent = parent;
  utc->uri = NULL;
  utc->cctx = NULL;             /* to be set later */
  utc->state = FSUI_ACTIVE;
  if (GNUNET_YES == GNUNET_disk_file_test (shared->ctx->ectx, filename))
    {
      /* add this file */
      if (GNUNET_OK != GNUNET_disk_file_size (shared->ctx->ectx,
                                              filename, &utc->total,
                                              GNUNET_YES))
        {
          GNUNET_free (utc);
          return NULL;
        }
      utc->meta = ECRS_dupMetaData (md);
    }
  else
    {
      if (GNUNET_SYSERR == shared->dsc (shared->dscClosure,
                                        filename, &addChildUpload, utc))
        {
          /* error scanning upload directory */
          while (utc->child != NULL)
            freeUploadList (utc->child);
          GNUNET_free (utc);
          return NULL;
        }
      utc->meta = ECRS_dupMetaData (md);
      ECRS_addToMetaData (utc->meta,
                          EXTRACTOR_MIMETYPE, GNUNET_DIRECTORY_MIME);
    }
  if (keywords != NULL)
    utc->keywords = ECRS_dupUri (keywords);
  else
    utc->keywords = NULL;
  utc->filename = GNUNET_strdup (filename);

  /* finally, link with parent */
  GNUNET_mutex_lock (shared->ctx->lock);
  utc->next = parent->child;
  parent->child = utc;
  GNUNET_mutex_unlock (shared->ctx->lock);
  return utc;
}

static void
signalUploadStopped (struct FSUI_UploadList *ul, int first_only)
{
  FSUI_Event event;

  while (ul != NULL)
    {
      signalUploadStopped (ul->child, 0);
      event.type = FSUI_upload_stopped;
      event.data.UploadStopped.uc.pos = ul;
      event.data.UploadStopped.uc.cctx = ul->cctx;
      event.data.UploadStopped.uc.ppos = ul->parent;
      event.data.UploadStopped.uc.pcctx = ul->parent->cctx;
      ul->shared->ctx->ecb (ul->shared->ctx->ecbClosure, &event);
      if (first_only)
        break;
      ul = ul->next;
    }
}

static void
freeShared (struct FSUI_UploadShared *shared)
{
  if (shared->global_keywords != NULL)
    ECRS_freeUri (shared->global_keywords);
  EXTRACTOR_removeAll (shared->extractors);
  GNUNET_free_non_null (shared->extractor_config);
  GNUNET_free (shared);
}

/**
 * Start uploading a file.  Note that an upload cannot be stopped once
 * started (not necessary anyway), but it can fail.  The function also
 * automatically the uploaded file in the global keyword space under
 * the given keywords.
 *
 * @return GNUNET_OK on success (at least we started with it),
 *  GNUNET_SYSERR if the file does not exist or gnunetd is not
 *  running
 */
struct FSUI_UploadList *
FSUI_startUpload (struct FSUI_Context *ctx,
                  const char *filename,
                  DirectoryScanCallback dsc,
                  void *dscClosure,
                  unsigned int anonymityLevel,
                  unsigned int priority,
                  int doIndex,
                  int doExtract,
                  int individualKeywords,
                  GNUNET_CronTime expiration,
                  const struct ECRS_MetaData *md,
                  const struct ECRS_URI *globalURI,
                  const struct ECRS_URI *keyUri)
{
  char *config;
  EXTRACTOR_ExtractorList *extractors;
  struct FSUI_UploadShared *shared;
  struct FSUI_UploadList *ul;

  config = NULL;
  extractors = NULL;
  if (doExtract)
    {
      extractors = EXTRACTOR_loadDefaultLibraries ();
      if (GC_have_configuration_value (ctx->cfg, "FS", "EXTRACTORS"))
        {
          GC_get_configuration_value_string (ctx->cfg,
                                             "FS",
                                             "EXTRACTORS", NULL, &config);
          if (config != NULL)
            {
              extractors = EXTRACTOR_loadConfigLibraries (extractors, config);
            }
        }
    }
  shared = GNUNET_malloc (sizeof (FSUI_UploadShared));
  shared->dsc = dsc;
  shared->dscClosure = dscClosure;
  shared->extractors = extractors;
  shared->expiration = expiration;
  shared->ctx = ctx;
  shared->handle = NULL;
  shared->global_keywords =
    globalURI != NULL ? ECRS_dupUri (globalURI) : NULL;
  shared->extractor_config = config;
  shared->doIndex = doIndex;
  shared->anonymityLevel = anonymityLevel;
  shared->priority = priority;
  shared->individualKeywords = individualKeywords;
  shared->handle = NULL;
  ul = addUploads (shared, filename, keyUri, md, &ctx->activeUploads);
  if (ul == NULL)
    {
      freeShared (shared);
      return NULL;
    }
  shared->handle =
    GNUNET_thread_create (&FSUI_uploadThreadEvent, ul, 128 * 1024);
  if (shared->handle == NULL)
    {
      GE_LOG_STRERROR (ctx->ectx,
                       GE_ERROR | GE_USER | GE_BULK, "PTHREAD_CREATE");
      freeUploadList (ul);
      freeShared (shared);
      return NULL;
    }
  GE_ASSERT (ctx->ectx, ul->shared == shared);
  return ul;
}

/**
 * Abort an upload.  If the context is for a recursive
 * upload, all sub-uploads will also be aborted.
 * Note that if this is not the top-level upload,
 * the top-level upload will continue without the
 * subtree selected using this abort command.
 *
 * @return GNUNET_SYSERR on error
 */
int
FSUI_abortUpload (struct FSUI_Context *ctx, struct FSUI_UploadList *ul)
{
  FSUI_UploadList *c;

  GE_ASSERT (ctx->ectx, ul != NULL);
  if ((ul->state != FSUI_ACTIVE) && (ul->state != FSUI_PENDING))
    return GNUNET_NO;
  if (ul->state == FSUI_ACTIVE)
    {
      ul->state = FSUI_ABORTED;
      c = ul->child;
      while (c != NULL)
        {
          FSUI_abortUpload (ctx, c);
          c = c->next;
        }
      GNUNET_thread_stop_sleep (ul->shared->handle);
    }
  else
    {
      ul->state = FSUI_ABORTED_JOINED;
      c = ul->child;
      while (c != NULL)
        {
          FSUI_abortUpload (ctx, c);
          c = c->next;
        }
    }
  return GNUNET_OK;
}

/**
 * Stop an upload.  Only to be called for the top-level
 * upload.
 *
 * @return GNUNET_SYSERR on error
 */
int
FSUI_stopUpload (struct FSUI_Context *ctx, struct FSUI_UploadList *ul)
{
  void *unused;
  struct FSUI_UploadShared *shared;

  GE_ASSERT (ctx->ectx, ul != NULL);
  GE_ASSERT (ctx->ectx, ul->parent == &ctx->activeUploads);
  if ((ul->state == FSUI_ACTIVE) ||
      (ul->state == FSUI_COMPLETED) ||
      (ul->state == FSUI_ABORTED) || (ul->state == FSUI_ERROR))
    {
      GE_ASSERT (ctx->ectx, ul->shared->handle != NULL);
      GNUNET_thread_join (ul->shared->handle, &unused);
      ul->shared->handle = NULL;
      if (ul->state == FSUI_ACTIVE)
        ul->state = FSUI_PENDING;
      else
        ul->state++;            /* add _JOINED */
    }
  else
    {
      GE_ASSERT (ctx->ectx, ul->shared->handle == NULL);
    }
  signalUploadStopped (ul, 1);
  shared = ul->shared;
  freeUploadList (ul);
  freeShared (shared);
  return GNUNET_OK;
}

/* end of upload.c */
