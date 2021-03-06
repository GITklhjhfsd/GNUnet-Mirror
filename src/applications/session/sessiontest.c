/*
     This file is part of GNUnet.
     (C) 2005, 2006, 2007 Christian Grothoff (and other contributing authors)

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
 * @file applications/session/sessiontest.c
 * @brief Session establishment testcase
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "gnunet_util.h"
#include "gnunet_testing_lib.h"
#include "gnunet_stats_lib.h"

#define START_PEERS GNUNET_YES

static int ok;

static int
waitForConnect (const char *name, unsigned long long value, void *cls)
{
  if ((value > 0) && (0 == strcmp (_("# of connected peers"), name)))
    {
      ok = 1;
      return GNUNET_SYSERR;
    }
  return GNUNET_OK;
}

/**
 * Testcase to test p2p session key exchange.
 *
 * @param argc number of arguments from the command line
 * @param argv command line arguments
 * @return 0: ok, -1: error
 */
int
main (int argc, char **argv)
{
#if START_PEERS
  struct GNUNET_TESTING_DaemonContext *peers;
#endif
  int ret;
  struct GNUNET_ClientServerConnection *sock1;
  struct GNUNET_ClientServerConnection *sock2;
  int left;
  struct GNUNET_GC_Configuration *cfg;

  cfg = GNUNET_GC_create ();
  if (-1 == GNUNET_GC_parse_configuration (cfg, "check.conf"))
    {
      GNUNET_GC_free (cfg);
      return -1;
    }
#if START_PEERS
  peers = GNUNET_TESTING_start_daemons (strstr (argv[0], "_") + 1,      /* tcp, udp or http */
                                        "advertising stats",
                                        "/tmp/gnunet-session-test", 2087,
                                        10000, 2);
  if (peers == NULL)
    {
      GNUNET_GC_free (cfg);
      return -1;
    }
#endif
  GNUNET_GE_ASSERT (NULL, GNUNET_OK == GNUNET_TESTING_connect_daemons (2087, 12087));
  if (GNUNET_OK ==
      GNUNET_wait_for_daemon_running (NULL, cfg, 30 * GNUNET_CRON_SECONDS))
    {
      sock1 = GNUNET_client_connection_create (NULL, cfg);
      GNUNET_GC_set_configuration_value_string (cfg,
                                                NULL,
                                                "NETWORK",
                                                "HOST", "localhost:12087");
      sock2 = GNUNET_client_connection_create (NULL, cfg);
      left = 30;                /* how many iterations should we wait? */
      while (GNUNET_OK ==
             GNUNET_STATS_get_statistics (NULL, sock1, &waitForConnect, NULL))
        {
          printf ("Waiting for peers to connect (%u iterations left)...\n",
                  left);
          sleep (5);
          left--;
          if (left == 0)
            {
              ret = 1;
              break;
            }
        }
#if 0
      if (ok == 1)
        {
          for (left = 0; left < 10; left++)
            {
              ok = 0;
              while (GNUNET_shutdown_test () == GNUNET_NO)
                {
                  printf ("Checking that peers are staying connected 1...\n");
                  GNUNET_STATS_get_statistics (NULL, sock1, &waitForConnect,
                                               NULL);
                  sleep (1);
                  if (ok == 0)
                    {
                      printf ("Peers disconnected!\n");
                      break;
                    }
                  printf ("Checking that peers are staying connected 2...\n");
                  GNUNET_STATS_get_statistics (NULL, sock2, &waitForConnect,
                                               NULL);
                  sleep (1);
                  if (ok == 0)
                    {
                      printf ("Peers disconnected!\n");
                      break;
                    }
                }
            }
        }
      else
        {
          printf ("Peers failed to connect!\n");
        }
#endif

      GNUNET_client_connection_destroy (sock1);
      GNUNET_client_connection_destroy (sock2);
    }
  else
    {
      printf ("Could not establish connection with peer.\n");
      ret = 1;
    }
#if START_PEERS
  GNUNET_TESTING_stop_daemons (peers);
#endif
  GNUNET_GC_free (cfg);
  return (ok == 0) ? 1 : 0;
}

/* end of sessiontest.c */
