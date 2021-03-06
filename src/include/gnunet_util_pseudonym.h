/*
     This file is part of GNUnet
     (C) 2004, 2005, 2006, 2008 Christian Grothoff (and other contributing authors)

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
 * @file include/gnunet_pseudonym_lib.h
 * @brief high-level support for pseudonyms
 * @author Christian Grothoff
 */

#ifndef GNUNET_PSEUDONYM_LIB_H
#define GNUNET_PSEUDONYM_LIB_H

#include "gnunet_util_crypto.h"
#include "gnunet_util_containers.h"

#ifdef __cplusplus
extern "C"
{
#if 0                           /* keep Emacsens' auto-indent happy */
}
#endif
#endif

/**
 * Iterator over all known pseudonyms.
 *
 * @param rating the local rating of the pseudonym
 * @return GNUNET_OK to continue iteration, GNUNET_SYSERR to abort
 */
typedef int (*GNUNET_PseudonymIterator) (void *cls,
                                         const GNUNET_HashCode *
                                         pseudonym,
                                         const struct
                                         GNUNET_MetaData * md, int rating);

/**
 * Change the ranking of a pseudonym.
 *
 * @param pseudonym id of the pseudonym
 * @param delta by how much should the rating be changed?
 * @return new rating of the namespace
 */
int GNUNET_pseudonym_rank (struct GNUNET_GE_Context *ectx,
                           struct GNUNET_GC_Configuration *cfg,
                           const GNUNET_HashCode * pseudonym, int delta);

/**
 * Add a pseudonym to the set of known pseudonyms.
 *
 * @param pseudonym the pseudonym's identifier
 */
void GNUNET_pseudonym_add (struct GNUNET_GE_Context *ectx,
                           struct GNUNET_GC_Configuration *cfg,
                           const GNUNET_HashCode * pseudo,
                           const struct GNUNET_MetaData *meta);


/**
 * List all known pseudonyms.
 */
int GNUNET_pseudonym_list_all (struct GNUNET_GE_Context *ectx,
                               struct GNUNET_GC_Configuration *cfg,
                               GNUNET_PseudonymIterator iterator,
                               void *closure);

/**
 * Register callback to be invoked whenever we discover
 * a new pseudonym.
 */
int GNUNET_pseudonym_register_discovery_callback (struct GNUNET_GE_Context
                                                  *ectx,
                                                  struct
                                                  GNUNET_GC_Configuration
                                                  *cfg,
                                                  GNUNET_PseudonymIterator
                                                  iterator, void *closure);

/**
 * Unregister namespace discovery callback.
 */
int
GNUNET_pseudonym_unregister_discovery_callback (GNUNET_PseudonymIterator
                                                iterator, void *closure);

/**
 * Return the unique, human readable name for the given pseudonym.
 *
 * @return NULL on failure (should never happen)
 */
char *GNUNET_pseudonym_id_to_name (struct GNUNET_GE_Context *ectx,
                                   struct GNUNET_GC_Configuration *cfg,
                                   const GNUNET_HashCode * pseudo);

/**
 * Get the pseudonym ID belonging to the given human readable name.
 *
 * @return GNUNET_OK on success
 */
int GNUNET_pseudonym_name_to_id (struct GNUNET_GE_Context *ectx,
                                 struct GNUNET_GC_Configuration *cfg,
                                 const char *hname, GNUNET_HashCode * psid);

#if 0                           /* keep Emacsens' auto-indent happy */
{
#endif
#ifdef __cplusplus
}
#endif


#endif
