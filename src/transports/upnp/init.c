/*
     This file is part of GNUnet
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
 * @file src/transports/upnp/init.c
 * @brief API for UPnP access
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_util_cron.h"
#include "upnp.h"
#include "gnunet_upnp_service.h"
#include "gnunet_core.h"

static struct GE_Context *ectx;

static struct GC_Configuration *cfg;

static struct GNUNET_CronManager *cron;

static struct GNUNET_Mutex *lock;

typedef struct
{
  unsigned short port;
  const char *proto;
} PMap;

static PMap *maps;

static unsigned int maps_size;

static struct GNUNET_ThreadHandle *discovery;

static int discovery_socket;

/**
 * Obtain the public/external IP address.
 *
 * @return GNUNET_SYSERR on error, GNUNET_OK on success
 */
static int
gnunet_upnp_get_public_ip (GNUNET_IPv4Address * address)
{
  const char *ip;

  ip = gaim_upnp_get_public_ip ();
  if (ip == NULL)
    return GNUNET_SYSERR;
  return GNUNET_get_host_by_name (ectx, ip, address);
}

static void
kill_discovery ()
{
  void *unused;

  if (discovery != NULL)
    {
      SHUTDOWN (discovery_socket, SHUT_RDWR);
      CLOSE (discovery_socket);
      GNUNET_thread_join (discovery, &unused);
      discovery = NULL;
    }
}

static void *
discover_thread ()
{
  gaim_upnp_discover (ectx, cfg, discovery_socket);
  return NULL;
}

/**
 * Periodically try to (re)discover UPnP access points.
 */
static void
discover (void *unused)
{
  kill_discovery ();
  discovery_socket = SOCKET (PF_INET, SOCK_DGRAM, 0);
  if (discovery_socket == -1)
    return;
  discovery = GNUNET_thread_create (&discover_thread, NULL, 1024 * 128);
}

/**
 * Periodically repeat our requests for port mappings.
 */
static void
portmap (void *unused)
{
  unsigned int i;

  GNUNET_mutex_lock (lock);
  for (i = 0; i < maps_size; i++)
    gaim_upnp_change_port_mapping (ectx,
                                   cfg, GNUNET_NO, maps[i].port,
                                   maps[i].proto);
  GNUNET_mutex_unlock (lock);
}


/**
 * Get the external IP address for the local machine.
 *
 * @return GNUNET_SYSERR on error, GNUNET_OK on success
 */
static int
gnunet_upnp_get_ip (unsigned short port,
                    const char *protocol, GNUNET_IPv4Address * address)
{
  unsigned int i;

  GNUNET_mutex_lock (lock);
  for (i = 0; i < maps_size; i++)
    if ((0 == strcmp (maps[i].proto, protocol)) && (maps[i].port == port))
      break;
  if (i == maps_size)
    {
      /* new entry! */
      GNUNET_array_grow (maps, maps_size, maps_size + 1);
      maps[i].proto = protocol;
      maps[i].port = port;
      gaim_upnp_change_port_mapping (ectx, cfg, GNUNET_YES, port, protocol);
    }
  GNUNET_mutex_unlock (lock);
  return gnunet_upnp_get_public_ip (address);
}


/**
 * Get the external IP address for the local machine.
 */
UPnP_ServiceAPI *
provide_module_upnp (CoreAPIForApplication * capi)
{
  static UPnP_ServiceAPI api;

  ectx = capi->ectx;
  cfg = capi->cfg;
  cron = cron_create (ectx);
  lock = GNUNET_mutex_create (GNUNET_NO);
  GNUNET_cron_start (cron);
  GNUNET_cron_add_job (cron, &discover, 0, 5 * GNUNET_CRON_MINUTES, NULL);
  GNUNET_cron_add_job (cron, &portmap, 150 * GNUNET_CRON_SECONDS,
                       5 * GNUNET_CRON_MINUTES, NULL);
  api.get_ip = gnunet_upnp_get_ip;
  return &api;
}

/**
 * Shutdown UPNP.
 */
int
release_module_upnp ()
{
  unsigned int i;

  if (cron == NULL)
    return GNUNET_SYSERR;       /* not loaded! */
  for (i = 0; i < maps_size; i++)
    gaim_upnp_change_port_mapping (ectx,
                                   cfg, GNUNET_NO, maps[i].port,
                                   maps[i].proto);
  GNUNET_cron_stop (cron);
  GNUNET_cron_del_job (cron, &discover, 5 * GNUNET_CRON_MINUTES, NULL);
  GNUNET_cron_del_job (cron, &portmap, 5 * GNUNET_CRON_MINUTES, NULL);
  GNUNET_cron_destroy (cron);
  kill_discovery ();
  cron = NULL;
  GNUNET_mutex_destroy (lock);
  lock = NULL;
  GNUNET_array_grow (maps, maps_size, 0);
  ectx = NULL;
  cfg = NULL;
  return GNUNET_OK;
}


/* end of init.c */
