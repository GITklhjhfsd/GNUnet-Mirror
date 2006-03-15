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
 * @file applications/datastore/prefetch.c
 * @brief This module is responsible for prefetching
 *   content that can be pushed out into the network
 * @author Christian Grothoff, Igor Wronsky
 */

#include "platform.h"
#include "prefetch.h"

#define DEBUG_PREFETCH YES

/* use a 64-entry RCB buffer */
#define RCB_SIZE 64

/* how many blocks to cache from on-demand files in a row */
#define RCB_ONDEMAND_MAX 16

/**
 * Buffer with pre-fetched, encoded random content for migration.
 */
typedef struct {
  HashCode512 key;
  Datastore_Value * value;
  /**
   * 0 if we have never used this content with any peer.  Otherwise
   * the value is set to the lowest 32 bit of the peer ID (to avoid 
   * sending it to the same peer twice).  After sending out the
   * content twice, it is discarded.
   */
  int used;
} ContentBuffer;


static ContentBuffer randomContentBuffer[RCB_SIZE];

/**
 * SQ-store handle
 */
static SQstore_ServiceAPI * sq;

/**
 * Semaphore on which the RCB acquire thread waits
 * if the RCB buffer is full.
 */
static Semaphore * acquireMoreSignal;

static Semaphore * doneSignal;

/**
 * Lock for the RCB buffer.
 */
static Mutex lock;

/**
 * Highest index in RCB that is valid.
 */
static int rCBPos = 0;

static PTHREAD_T gather_thread;


static int aquire(const HashCode512 * key,
		  const Datastore_Value * value,
		  void * closure) {
  int load;

  if (doneSignal != NULL)
    return SYSERR;
  SEMAPHORE_DOWN(acquireMoreSignal);
  if (doneSignal != NULL)
    return SYSERR;
  MUTEX_LOCK(&lock);
  load = 0;
  while (randomContentBuffer[rCBPos].value != NULL) {
    rCBPos = (rCBPos + 1) % RCB_SIZE;
    load++;
    if (load > RCB_SIZE) {
      BREAK();
      MUTEX_UNLOCK(&lock);
      return SYSERR;
    }
  }  
#if DEBUG_PREFETCH
  LOG(LOG_DEBUG,
      "Adding content to prefetch buffer (%u)\n",
      rCBPos);
#endif
  randomContentBuffer[rCBPos].key = *key;
  randomContentBuffer[rCBPos].used = 0;
  randomContentBuffer[rCBPos].value
    = MALLOC(ntohl(value->size));
  memcpy(randomContentBuffer[rCBPos].value,
	 value,
	 ntohl(value->size));
  MUTEX_UNLOCK(&lock);
  load = getCPULoad(); /* FIXME: should use 'IO load' here */
  if (load < 10)
    load = 10;    /* never sleep less than 500 ms */
  if (load > 100)
    load = 100;   /* never sleep longer than 5 seconds since that
		     might show up badly in the shutdown sequence... */
  if (doneSignal != NULL)
    return SYSERR;
  /* the higher the load, the longer the sleep */
  gnunet_util_sleep(50 * cronMILLIS * load);
  if (doneSignal != NULL)
    return SYSERR;
  return OK;
}

/**
 * Acquire new block(s) to the migration buffer.
 */
static void * rcbAcquire(void * unused) {
  int load;
  while (doneSignal == NULL) {
    sq->iterateExpirationTime(0,
			      &aquire,
			      NULL);
    /* sleep here, too - otherwise we start looping immediately
       if there is no content in the DB! */
    load = getCPULoad();
    if (load < 10)
      load = 10;    /* never sleep less than 500 ms */
    if (load > 100)
      load = 100;   /* never sleep longer than 5 seconds since that
		       might show up badly in the shutdown sequence... */
    gnunet_util_sleep(50 * cronMILLIS * load);
  }
  SEMAPHORE_UP(doneSignal);
  return NULL;
}

/**
 * Select content for active migration.  Takes the best match from the
 * randomContentBuffer (if the RCB is non-empty) and returns it.
 *
 * @return SYSERR if the RCB is empty
 */
int getRandom(const HashCode512 * receiver,
	      unsigned int sizeLimit,
	      HashCode512 * key,
	      Datastore_Value ** value,
	      unsigned int type) {
  unsigned int dist;
  unsigned int minDist;
  int minIdx;
  int i;

  minIdx = -1;
  minDist = -1; /* max */
  MUTEX_LOCK(&lock);
  for (i=0;i<RCB_SIZE;i++) {
    if (randomContentBuffer[i].value == NULL)
      continue;
    if (randomContentBuffer[i].used == *(int*) receiver)
      continue; /* used this content for this peer already! */
    if ( ( ( (type != ntohl(randomContentBuffer[i].value->type)) &&
	     (type != 0) ) ) ||
	 (sizeLimit < ntohl(randomContentBuffer[i].value->size)) )
      continue;
    dist = distanceHashCode512(&randomContentBuffer[i].key,
			       receiver);
    if (dist < minDist) {
      minIdx = i;
      minDist = dist;
    }
  }
  if (minIdx == -1) {
    MUTEX_UNLOCK(&lock);
#if DEBUG_PREFETCH
    LOG(LOG_DEBUG,
	"Failed to find content in prefetch buffer\n");
#endif
    return SYSERR;
  }
#if DEBUG_PREFETCH
    LOG(LOG_DEBUG,
	"Found content in prefetch buffer (%u)\n",
	minIdx);
#endif
  *key = randomContentBuffer[minIdx].key;
  *value = randomContentBuffer[minIdx].value;

  if ( (randomContentBuffer[minIdx].used == 0) &&
       (0 != *(int*) receiver) ) {
    /* re-use once more! */
    randomContentBuffer[minIdx].used = *(int*) receiver;
    randomContentBuffer[minIdx].value = MALLOC(ntohl((*value)->size));
    memcpy(randomContentBuffer[minIdx].value,
	   *value,
	   ntohl((*value)->size));
  } else {
    randomContentBuffer[minIdx].used = 0;
    randomContentBuffer[minIdx].value = NULL;
    SEMAPHORE_UP(acquireMoreSignal);
  }
  MUTEX_UNLOCK(&lock);
  return OK;
}
				
void initPrefetch(SQstore_ServiceAPI * s) {
  sq = s;
  memset(randomContentBuffer,
	 0,
	 sizeof(ContentBuffer *)*RCB_SIZE);
  acquireMoreSignal = SEMAPHORE_NEW(RCB_SIZE);
  doneSignal = NULL;
  MUTEX_CREATE(&lock);
  if (0 != PTHREAD_CREATE(&gather_thread,
			  (PThreadMain)&rcbAcquire,
			  NULL,
			  64*1024))
    DIE_STRERROR("pthread_create");
}

void donePrefetch() {
  int i;
  void * unused;

  doneSignal = SEMAPHORE_NEW(0);
  SEMAPHORE_UP(acquireMoreSignal);
  SEMAPHORE_DOWN(doneSignal);
  SEMAPHORE_FREE(acquireMoreSignal);
  SEMAPHORE_FREE(doneSignal);
  PTHREAD_JOIN(&gather_thread, &unused);
  for (i=0;i<rCBPos;i++)
    FREENONNULL(randomContentBuffer[i].value);
  MUTEX_DESTROY(&lock);
}

/* end of prefetch.c */
