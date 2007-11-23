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
 * @file applications/fs/collection/collectiontest.c
 * @brief testcase for collection library
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_collection_lib.h"
#include "gnunet_util_config_impl.h"
#include "gnunet_util_network_client.h"

#define CHECK(a) if (!(a)) { ok = GNUNET_NO; GE_BREAK(NULL, 0); goto FAILURE; }

#define START_DAEMON 1

int
main (int argc, char *argv[])
{
  struct GC_Configuration *cfg;
#if START_DAEMON
  pid_t daemon;
#endif
  int ok;
  struct GNUNET_ClientServerConnection *sock;
  struct ECRS_MetaData *meta;
  ECRS_FileInfo fi;
  char *have;

  cfg = GC_create ();
  if (-1 == GC_parse_configuration (cfg, "check.conf"))
    {
      GC_free (cfg);
      return -1;
    }
  sock = NULL;
  meta = NULL;
#if START_DAEMON
  daemon = GNUNET_daemon_start (NULL, cfg, "peer.conf", GNUNET_NO);
  GE_ASSERT (NULL, daemon > 0);
  CHECK (GNUNET_OK ==
         GNUNET_wait_for_daemon_running (NULL, cfg,
                                         300 * GNUNET_CRON_SECONDS));
  GNUNET_thread_sleep (5 * GNUNET_CRON_SECONDS);        /* give apps time to start */
#endif
  ok = GNUNET_YES;
  meta = ECRS_createMetaData ();
  ECRS_addToMetaData (meta, EXTRACTOR_MIMETYPE, "test/foo");
  sock = GNUNET_client_connection_create (NULL, cfg);
  CHECK (sock != NULL);
  CO_init (NULL, cfg);

  /* ACTUAL TEST CODE */
  CO_stopCollection ();
  ECRS_deleteNamespace (NULL, cfg, "test-collection");
  CHECK (NULL == CO_getCollection ());
  CHECK (GNUNET_OK == CO_startCollection (1, 100, 60,   /* 60s */
                                          "test-collection", meta));
  have = CO_getCollection ();
  CHECK (NULL != have);
  CHECK (0 == strcmp (have, "test-collection"));
  GNUNET_free (have);
  fi.meta = meta;
  fi.uri =
    ECRS_stringToUri (NULL,
                      "gnunet://ecrs/chk/0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.0000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000.0");
  CO_publishToCollection (&fi);
  ECRS_freeUri (fi.uri);
  CO_done ();
  CO_init (NULL, cfg);
  have = CO_getCollection ();
  CHECK (NULL != have);
  CHECK (0 == strcmp (have, "test-collection"));
  GNUNET_free (have);
  CO_publishCollectionNow ();
  CO_stopCollection ();
  ECRS_deleteNamespace (NULL, cfg, "test-collection");
  CHECK (NULL == CO_getCollection ());

  /* END OF TEST CODE */
FAILURE:
  if (sock != NULL)
    {
      CO_done ();
      GNUNET_client_connection_destroy (sock);
    }
  if (meta != NULL)
    ECRS_freeMetaData (meta);
#if START_DAEMON
  GE_ASSERT (NULL, GNUNET_OK == GNUNET_daemon_stop (NULL, daemon));
#endif
  GC_free (cfg);
  return (ok == GNUNET_YES) ? 0 : 1;
}

/* end of collectiontest.c */
