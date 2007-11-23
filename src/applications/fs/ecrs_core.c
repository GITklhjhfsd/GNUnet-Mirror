/*
     This file is part of GNUnet
     (C) 2001, 2002, 2003, 2004, 2005 Christian Grothoff (and other contributing authors)

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
 * @file applications/fs/module/ecrs_core.c
 * @brief support for ECRS encoding of files
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_protocols.h"
#include "ecrs_core.h"

/**
 * Perform on-demand content encoding.
 *
 * @param data the data to encode
 * @param len the length of the data
 * @param query the query that was used to query
 *  for the content (verified that it matches
 *  data)
 * @param value the encoded data (set);
 *        the anonymityLevel is to be set to 0
 *        (caller should have checked before calling
 *        this method).
 * @return GNUNET_OK on success, GNUNET_SYSERR if data does not
 *  match the query
 */
int
fileBlockEncode (const DBlock * data,
                 unsigned int len,
                 const GNUNET_HashCode * query, Datastore_Value ** value)
{
  GNUNET_HashCode hc;
  GNUNET_AES_SessionKey skey;
  GNUNET_AES_InitializationVector iv;   /* initial value */
  Datastore_Value *val;
  DBlock *db;

  GE_ASSERT (NULL, len >= sizeof (DBlock));
  GE_ASSERT (NULL, (data != NULL) && (query != NULL));
  GNUNET_hash (&data[1], len - sizeof (DBlock), &hc);
  GNUNET_hash_to_AES_key (&hc, &skey, &iv);
  val = GNUNET_malloc (sizeof (Datastore_Value) + len);
  val->size = htonl (sizeof (Datastore_Value) + len);
  val->type = htonl (D_BLOCK);
  val->prio = htonl (0);
  val->anonymityLevel = htonl (0);
  val->expirationTime = GNUNET_htonll (0);
  db = (DBlock *) & val[1];
  db->type = htonl (D_BLOCK);
  GE_ASSERT (NULL, len - sizeof (DBlock) < GNUNET_MAX_BUFFER_SIZE);
  GE_ASSERT (NULL,
             len - sizeof (DBlock)
             == GNUNET_AES_encrypt (&data[1],
                                    len - sizeof (DBlock), &skey, &iv,
                                    &db[1]));
  GNUNET_hash (&db[1], len - sizeof (DBlock), &hc);
  if (0 != memcmp (query, &hc, sizeof (GNUNET_HashCode)))
    {
      GNUNET_free (val);
      *value = NULL;
      return GNUNET_SYSERR;
    }
  *value = val;
  return GNUNET_OK;
}

/**
 * Get the key that will be used to decrypt
 * a certain block of data.
 */
void
fileBlockGetKey (const DBlock * data, unsigned int len, GNUNET_HashCode * key)
{
  GE_ASSERT (NULL, len >= sizeof (DBlock));
  GNUNET_hash (&data[1], len - sizeof (DBlock), key);
}

/**
 * Get the query that will be used to query for
 * a certain block of data.
 *
 * @param db the block in plaintext
 */
void
fileBlockGetQuery (const DBlock * db, unsigned int len,
                   GNUNET_HashCode * query)
{
  char *tmp;
  const char *data;
  GNUNET_HashCode hc;
  GNUNET_AES_SessionKey skey;
  GNUNET_AES_InitializationVector iv;

  GE_ASSERT (NULL, len >= sizeof (DBlock));
  data = (const char *) &db[1];
  len -= sizeof (DBlock);
  GE_ASSERT (NULL, len < GNUNET_MAX_BUFFER_SIZE);
  GNUNET_hash (data, len, &hc);
  GNUNET_hash_to_AES_key (&hc, &skey, &iv);
  tmp = GNUNET_malloc (len);
  GE_ASSERT (NULL, len == GNUNET_AES_encrypt (data, len, &skey, &iv, tmp));
  GNUNET_hash (tmp, len, query);
  GNUNET_free (tmp);
}

unsigned int
getTypeOfBlock (unsigned int size, const DBlock * data)
{
  if (size <= 4)
    {
      GE_BREAK (NULL, 0);
      return ANY_BLOCK;         /* signal error */
    }
  return ntohl (*((const unsigned int *) data));
}

/**
 * What is the main query (the one that is used in routing and for the
 * DB lookup) for the given content and block type?
 *
 * @param data the content (encoded)
 * @param query set to the query for the content
 * @return GNUNET_SYSERR if the content is invalid or
 *   the content type is not known
 */
int
getQueryFor (unsigned int size,
             const DBlock * data, int verify, GNUNET_HashCode * query)
{
  unsigned int type;

  type = getTypeOfBlock (size, data);
  if (type == ANY_BLOCK)
    {
      GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;
    }
  switch (type)
    {
    case D_BLOCK:
      /* CHK: GNUNET_hash of content == query */
      GNUNET_hash (&data[1], size - sizeof (DBlock), query);
      return GNUNET_OK;
    case S_BLOCK:
      {
        const SBlock *sb;
        if (size < sizeof (SBlock))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        sb = (const SBlock *) data;
        if ((verify == GNUNET_YES) &&
            (GNUNET_OK != GNUNET_RSA_verify (&sb->identifier,
                                             size
                                             - sizeof (GNUNET_RSA_Signature)
                                             - sizeof (GNUNET_RSA_PublicKey)
                                             - sizeof (unsigned int),
                                             &sb->signature, &sb->subspace)))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        *query = sb->identifier;
        return GNUNET_OK;
      }
    case K_BLOCK:
      {
        const KBlock *kb;
        if (size < sizeof (KBlock))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        kb = (const KBlock *) data;
        if ((verify == GNUNET_YES) &&
            ((GNUNET_OK != GNUNET_RSA_verify (&kb[1],
                                              size - sizeof (KBlock),
                                              &kb->signature,
                                              &kb->keyspace))))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        GNUNET_hash (&kb->keyspace, sizeof (GNUNET_RSA_PublicKey), query);
        return GNUNET_OK;
      }
    case N_BLOCK:
      {
        const NBlock *nb;
        if (size < sizeof (NBlock))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        nb = (const NBlock *) data;
        if ((verify == GNUNET_YES) &&
            (GNUNET_OK != GNUNET_RSA_verify (&nb->identifier,
                                             size
                                             - sizeof (GNUNET_RSA_Signature)
                                             - sizeof (GNUNET_RSA_PublicKey)
                                             - sizeof (unsigned int),
                                             &nb->signature, &nb->subspace)))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        *query = nb->namespace; /* XOR with all zeros makes no difference... */
        return GNUNET_OK;
      }
    case KN_BLOCK:
      {
        const KNBlock *kb;
        if (size < sizeof (KNBlock))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        kb = (const KNBlock *) data;
        if ((verify == GNUNET_YES) &&
            ((GNUNET_OK != GNUNET_RSA_verify (&kb->nblock,
                                              size
                                              - sizeof (KBlock)
                                              - sizeof (unsigned int),
                                              &kb->kblock.signature,
                                              &kb->kblock.keyspace))))
          {
            GE_BREAK (NULL, 0);
            return GNUNET_SYSERR;
          }
        GNUNET_hash (&kb->kblock.keyspace, sizeof (GNUNET_RSA_PublicKey),
                     query);
        return GNUNET_OK;
      }
    case ONDEMAND_BLOCK:
      {
        GE_BREAK (NULL, 0);     /* should never be used here! */
        return GNUNET_SYSERR;
      }
    default:
      {
        GE_BREAK (NULL, 0);     /* unknown block type */
        return GNUNET_SYSERR;
      }
    }                           /* end switch */
}


/**
 * Verify that the given Datum is a valid response
 * to a given query.
 *
 * @param type the type of the query
 * @param size the size of the data
 * @param data the encoded data
 * @param hc result of getQueryFor
 * @param keyCount the number of keys in the query,
 *        use 0 to match only primary key
 * @param keys the keys of the query
 * @return GNUNET_YES if this data matches the query, otherwise
 *         GNUNET_NO; GNUNET_SYSERR if the keyCount does not match the
 *         query type
 */
int
isDatumApplicable (unsigned int type,
                   unsigned int size,
                   const DBlock * data,
                   const GNUNET_HashCode * hc,
                   unsigned int keyCount, const GNUNET_HashCode * keys)
{
  GNUNET_HashCode h;

  if (type != getTypeOfBlock (size, data))
    {
      GE_BREAK (NULL, 0);
      return GNUNET_SYSERR;     /* type mismatch */
    }
  if (0 != memcmp (hc, &keys[0], sizeof (GNUNET_HashCode)))
    {
      GE_BREAK (NULL, 0);       /* mismatch between primary queries,
                                   we should not even see those here. */
      return GNUNET_SYSERR;
    }
  if (keyCount == 0)
    return GNUNET_YES;          /* request was to match only primary key */
  switch (type)
    {
    case S_BLOCK:
      if (keyCount != 2)
        return GNUNET_SYSERR;   /* no match */
      GNUNET_hash (&((const SBlock *) data)->subspace,
                   sizeof (GNUNET_RSA_PublicKey), &h);
      if (0 == memcmp (&keys[1], &h, sizeof (GNUNET_HashCode)))
        return GNUNET_OK;
      return GNUNET_SYSERR;
    case N_BLOCK:
      if (keyCount != 2)
        return GNUNET_SYSERR;   /* no match */
      GNUNET_hash (&((const NBlock *) data)->subspace,
                   sizeof (GNUNET_RSA_PublicKey), &h);
      if (0 != memcmp (&keys[1], &h, sizeof (GNUNET_HashCode)))
        return GNUNET_SYSERR;
      return GNUNET_OK;
    case D_BLOCK:
    case K_BLOCK:
    case KN_BLOCK:
      if (keyCount != 1)
        GE_BREAK (NULL, 0);     /* keyCount should be 1 */
      return GNUNET_OK;         /* if query matches, everything matches! */
    case ANY_BLOCK:
      GE_BREAK (NULL, 0);       /* block type should be known */
      return GNUNET_SYSERR;
    default:
      GE_BREAK (NULL, 0);       /* unknown block type */
      return GNUNET_SYSERR;
    }
}

/* end of ecrs_core.c */
