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
 * @file module/table.h
 * @brief DV DHT connection table internal API
 * @author Nathan Evans
 * @author Christian Grothoff
 */

#ifndef DV_DHT_TABLE_H
#define DV_DHT_TABLE_H

#include "gnunet_util.h"
#include "gnunet_core.h"

/**
 * Select a peer from the routing table that would be a good routing
 * destination for sending a message for "target".  The resulting peer
 * must not be in the set of blocked peers.<p>
 *
 * Note that we should not ALWAYS select the closest peer to the
 * target, peers further away from the target should be chosen with
 * exponentially declining probability (this function is also used for
 * populating the target's routing table).
 *
 * @return GNUNET_OK on success, GNUNET_SYSERR on error
 */
int GNUNET_DV_DHT_select_peer (GNUNET_PeerIdentity * set,
                               const GNUNET_HashCode * target,
                               const GNUNET_PeerIdentity * blocked,
                               unsigned int blocked_size,
                               struct GNUNET_BloomFilter *bloom);

/**
 * Compute a (rough) estimate of the networks diameter.
 *
 * @return estimated network diameter
 */
unsigned int GNUNET_DV_DHT_estimate_network_diameter (void);

/**
 * Initialize table DV DHT component.
 *
 * @param capi the core API
 * @return GNUNET_OK on success
 */
int GNUNET_DV_DHT_table_init (GNUNET_CoreAPIForPlugins * capi);

/**
 * Shutdown table DV DHT component.
 *
 * @return GNUNET_OK on success
 */
int GNUNET_DV_DHT_table_done (void);

/*
 * Check whether my identity is closer than any known peers.
 *
 * Return GNUNET_YES if node location is closest, GNUNET_NO
 * otherwise.
 */
int GNUNET_DV_DHT_am_closest_peer (const GNUNET_HashCode * target);

/*
 * Consider adding the peer to table
 */
void GNUNET_DV_DHT_considerPeer (const GNUNET_PeerIdentity * peer);

#endif
