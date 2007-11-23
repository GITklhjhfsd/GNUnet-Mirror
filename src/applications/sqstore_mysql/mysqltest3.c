/*
     This file is part of GNUnet.
     (C) 2004, 2005, 2006, 2007 Christian Grothoff (and other contributing authors)

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
/*
 * @file applications/sqstore_mysql/mysqltest3.c
 * @brief Profile sqstore iterators.
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_util_cron.h"
#include "gnunet_util_crypto.h"
#include "gnunet_util_config_impl.h"
#include "gnunet_protocols.h"
#include "gnunet_sqstore_service.h"
#include "core.h"

/**
 * Target datastore size (in bytes).  Realistic sizes are
 * more like 16 GB (not the default of 16 MB); however,
 * those take too long to run them in the usual "make check"
 * sequence.  Hence the value used for shipping is tiny.
 */
#define MAX_SIZE 1024LL * 1024 * 128

#define ITERATIONS 10

/**
 * Number of put operations equivalent to 1/10th of MAX_SIZE
 */
#define PUT_10 (MAX_SIZE / 32 / 1024 / ITERATIONS)

static unsigned long long stored_bytes;

static unsigned long long stored_entries;

static unsigned long long stored_ops;

static GNUNET_CronTime start_time;

static int
putValue (SQstore_ServiceAPI * api, int i)
{
  Datastore_Value *value;
  size_t size;
  static GNUNET_HashCode key;
  static int ic;

  /* most content is 32k */
  size = sizeof (Datastore_Value) + 32 * 1024;

  if (GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, 16) == 0)  /* but some of it is less! */
    size =
      sizeof (Datastore_Value) +
      GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, 32 * 1024);
  size = size - (size & 7);     /* always multiple of 8 */

  /* generate random key */
  key.bits[0] = (unsigned int) GNUNET_get_time ();
  GNUNET_hash (&key, sizeof (GNUNET_HashCode), &key);
  value = GNUNET_malloc (size);
  value->size = htonl (size);
  value->type = htonl (i);
  value->prio = htonl (GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, 100));
  value->anonymityLevel = htonl (i);
  value->expirationTime =
    GNUNET_htonll (GNUNET_get_time () + 60 * GNUNET_CRON_HOURS +
                   GNUNET_random_u32 (GNUNET_RANDOM_QUALITY_WEAK, 1000));
  memset (&value[1], i, size - sizeof (Datastore_Value));
  if (GNUNET_OK != api->put (&key, value))
    {
      GNUNET_free (value);
      fprintf (stderr, "E");
      return GNUNET_SYSERR;
    }
  ic++;
  stored_bytes += ntohl (value->size);
  stored_ops++;
  stored_entries++;
  GNUNET_free (value);
  return GNUNET_OK;
}

static int
iterateDummy (const GNUNET_HashCode * key, const Datastore_Value * val,
              void *cls, unsigned long long uid)
{
  if (GNUNET_shutdown_test () == GNUNET_YES)
    return GNUNET_SYSERR;
  return GNUNET_OK;
}

static int
test (SQstore_ServiceAPI * api)
{
  int i;
  int j;
  int ret;
  GNUNET_CronTime start;
  GNUNET_CronTime end;

  for (i = 0; i < ITERATIONS; i++)
    {
      /* insert data equivalent to 1/10th of MAX_SIZE */
      start = GNUNET_get_time ();
      for (j = 0; j < PUT_10; j++)
        {
          if (GNUNET_OK != putValue (api, j))
            break;
          if (GNUNET_shutdown_test () == GNUNET_YES)
            break;
        }
      end = GNUNET_get_time ();
      printf ("%3u insertion              took %20llums\n", i, end - start);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
      start = GNUNET_get_time ();
      ret = api->iterateLowPriority (0, &iterateDummy, api);
      end = GNUNET_get_time ();
      printf ("%3u low priority iteration took %20llums (%d)\n", i,
              end - start, ret);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
      start = GNUNET_get_time ();
      ret = api->iterateExpirationTime (0, &iterateDummy, api);
      end = GNUNET_get_time ();
      printf ("%3u expiration t iteration took %20llums (%d)\n", i,
              end - start, ret);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
      start = GNUNET_get_time ();
      ret = api->iterateNonAnonymous (0, &iterateDummy, api);
      end = GNUNET_get_time ();
      printf ("%3u non anonymou iteration took %20llums (%d)\n", i,
              end - start, ret);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
      start = GNUNET_get_time ();
      ret = api->iterateMigrationOrder (&iterateDummy, api);
      end = GNUNET_get_time ();
      printf ("%3u migration or iteration took %20llums (%d)\n", i,
              end - start, ret);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
      start = GNUNET_get_time ();
      ret = api->iterateAllNow (&iterateDummy, api);
      end = GNUNET_get_time ();
      printf ("%3u all now      iteration took %20llums (%d)\n", i,
              end - start, ret);
      if (GNUNET_shutdown_test () == GNUNET_YES)
        break;
    }
  api->drop ();
  return GNUNET_OK;
}

int
main (int argc, char *argv[])
{
  SQstore_ServiceAPI *api;
  int ok;
  struct GC_Configuration *cfg;
  struct GNUNET_CronManager *cron;

  cfg = GC_create ();
  if (-1 == GC_parse_configuration (cfg, "check.conf"))
    {
      GC_free (cfg);
      return -1;
    }
  cron = cron_create (NULL);
  initCore (NULL, cfg, cron, NULL);
  api = requestService ("sqstore");
  if (api != NULL)
    {
      start_time = GNUNET_get_time ();
      ok = test (api);
      releaseService (api);
    }
  else
    ok = GNUNET_SYSERR;
  doneCore ();
  GNUNET_cron_destroy (cron);
  GC_free (cfg);
  if (ok == GNUNET_SYSERR)
    return 1;
  return 0;
}

/* end of mysqltest3.c */
