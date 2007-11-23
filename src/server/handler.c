/*
     This file is part of GNUnet
     (C) 2001, 2002, 2003, 2004, 2005, 2006 Christian Grothoff (and other contributing authors)

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
 * @file server/handler.c
 * @brief demultiplexer for incoming peer-to-peer packets.
 * @author Christian Grothoff
 */

#include "platform.h"
#include "gnunet_util.h"
#include "gnunet_protocols.h"
#include "gnunet_transport_service.h"
#include "gnunet_identity_service.h"

#include "core.h"
#include "handler.h"
#include "connection.h"
#include "tcpserver.h"

#define DEBUG_HANDLER GNUNET_NO

/**
 * Track how many messages we are discarding?
 */
#define TRACK_DISCARD GNUNET_NO

/**
 * Track how much time was spent on each
 * type of message?
 */
#define MEASURE_TIME GNUNET_NO

/**
 * Should we validate that handlers do not
 * modify the messages that they are given?
 * (expensive!)
 */
#define VALIDATE_CLIENT GNUNET_NO

/**
 * How many incoming packages do we have in the buffer
 * (max.). Must be >= THREAD_COUNT to make sense.
 */
#define QUEUE_LENGTH 64

/**
 * How many threads do we start?
 */
#define THREAD_COUNT 2

/**
 * Transport service
 */
static Transport_ServiceAPI *transport;

/**
 * Identity service
 */
static Identity_ServiceAPI *identity;


static P2P_PACKET *bufferQueue_[QUEUE_LENGTH];

static int bq_firstFree_;

static int bq_firstFull_;

static int threads_running = GNUNET_NO;

static struct GNUNET_Semaphore *bufferQueueRead_;

static struct GNUNET_Semaphore *bufferQueueWrite_;

static struct GNUNET_Mutex *globalLock_;

static struct GNUNET_Semaphore *mainShutdownSignal;

static struct GNUNET_ThreadHandle *threads_[THREAD_COUNT];

#if TRACK_DISCARD
static unsigned int discarded;
static unsigned int blacklisted;
static unsigned int accepted;
#endif

/**
 * Array of arrays of message handlers.
 */
static MessagePartHandler **handlers = NULL;

/**
 * Number of handlers in the array (max, there
 * may be NULL pointers in it!)
 */
static unsigned int max_registeredType = 0;

/**
 * Array of arrays of the message handlers for plaintext messages.
 */
static PlaintextMessagePartHandler **plaintextHandlers = NULL;

/**
 * Number of handlers in the plaintextHandlers array (max, there
 * may be NULL pointers in it!)
 */
static unsigned int plaintextmax_registeredType = 0;

/**
 * Mutex to guard access to the handler array.
 */
static struct GNUNET_Mutex *handlerLock;

static struct GE_Context *ectx;

#if MEASURE_TIME
static GNUNET_CronTime time_by_type[P2P_PROTO_MAX_USED];
static unsigned int count_by_type[P2P_PROTO_MAX_USED];
#endif


/**
 * Register a method as a handler for specific message types.  Note
 * that it IS possible to register multiple handlers for the same
 * message.  In that case, they will ALL be executed in the order of
 * registration, unless one of them returns GNUNET_SYSERR in which case the
 * remaining handlers and the rest of the message are ignored.
 *
 * @param type the message type
 * @param callback the method to call if a message of
 *        that type is received
 * @return GNUNET_OK on success, GNUNET_SYSERR if core threads are running
 *        and updates to the handler list are illegal!
 */
int
registerp2pHandler (unsigned short type, MessagePartHandler callback)
{
  unsigned int last;

  if (threads_running == GNUNET_YES)
    {
      GE_BREAK (ectx, NULL);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_lock (handlerLock);
  if (type >= max_registeredType)
    {
      unsigned int ort = max_registeredType;
      GNUNET_array_grow (handlers, max_registeredType, type + 32);
      while (ort < max_registeredType)
        {
          unsigned int zero = 0;
          GNUNET_array_grow (handlers[ort], zero, 1);
          ort++;
        }
    }
  last = 0;
  while (handlers[type][last] != NULL)
    last++;
  last++;
  GNUNET_array_grow (handlers[type], last, last + 1);
  handlers[type][last - 2] = callback;
  GNUNET_mutex_unlock (handlerLock);
  return GNUNET_OK;
}

/**
 * Unregister a method as a handler for specific message types. Only
 * for encrypted messages!
 *
 * @param type the message type
 * @param callback the method to call if a message of
 *        that type is received
 * @return GNUNET_OK on success, GNUNET_SYSERR if there is a different
 *        handler for that type or if core threads are running
 *        and updates to the handler list are illegal!
 */
int
unregisterp2pHandler (unsigned short type, MessagePartHandler callback)
{
  unsigned int pos;
  unsigned int last;

  if (threads_running == GNUNET_YES)
    {
      GE_BREAK (ectx, 0);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_lock (handlerLock);
  if (type < max_registeredType)
    {
      pos = 0;
      while ((handlers[type][pos] != NULL) &&
             (handlers[type][pos] != callback))
        pos++;
      last = pos;
      while (handlers[type][last] != NULL)
        last++;
      if (last == pos)
        {
          GNUNET_mutex_unlock (handlerLock);
          return GNUNET_SYSERR;
        }
      else
        {
          handlers[type][pos] = handlers[type][last - 1];
          handlers[type][last - 1] = NULL;
          last++;
          GNUNET_array_grow (handlers[type], last, last - 1);
          GNUNET_mutex_unlock (handlerLock);
          return GNUNET_OK;
        }
    }
  GNUNET_mutex_unlock (handlerLock);
  return GNUNET_SYSERR;
}

/**
 * Register a method as a handler for specific message types.  Note
 * that it IS possible to register multiple handlers for the same
 * message.  In that case, they will ALL be executed in the order of
 * registration, unless one of them returns GNUNET_SYSERR in which case the
 * remaining handlers and the rest of the message are ignored.
 *
 * @param type the message type
 * @param callback the method to call if a message of
 *        that type is received
 * @return GNUNET_OK on success, GNUNET_SYSERR if core threads are running
 *        and updates to the handler list are illegal!
 */
int
registerPlaintextHandler (unsigned short type,
                          PlaintextMessagePartHandler callback)
{
  unsigned int last;

  if (threads_running == GNUNET_YES)
    {
      GE_BREAK (ectx, 0);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_lock (handlerLock);
  if (type >= plaintextmax_registeredType)
    {
      unsigned int ort = plaintextmax_registeredType;
      GNUNET_array_grow (plaintextHandlers, plaintextmax_registeredType,
                         type + 32);
      while (ort < plaintextmax_registeredType)
        {
          unsigned int zero = 0;
          GNUNET_array_grow (plaintextHandlers[ort], zero, 1);
          ort++;
        }
    }
  last = 0;
  while (plaintextHandlers[type][last] != NULL)
    last++;
  last++;
  GNUNET_array_grow (plaintextHandlers[type], last, last + 1);
  plaintextHandlers[type][last - 2] = callback;
  GNUNET_mutex_unlock (handlerLock);
  return GNUNET_OK;
}

/**
 * Unregister a method as a handler for specific message types. Only
 * for plaintext messages!
 *
 * @param type the message type
 * @param callback the method to call if a message of
 *        that type is received
 * @return GNUNET_OK on success, GNUNET_SYSERR if there is a different
 *        handler for that type or if core threads are running
 *        and updates to the handler list are illegal!
 */
int
unregisterPlaintextHandler (unsigned short type,
                            PlaintextMessagePartHandler callback)
{
  unsigned int pos;
  unsigned int last;

  if (threads_running == GNUNET_YES)
    {
      GE_BREAK (ectx, 0);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_lock (handlerLock);
  if (type < plaintextmax_registeredType)
    {
      pos = 0;
      while ((plaintextHandlers[type][pos] != NULL) &&
             (plaintextHandlers[type][pos] != callback))
        pos++;
      last = pos;
      while (plaintextHandlers[type][last] != NULL)
        last++;
      if (last == pos)
        {
          GNUNET_mutex_unlock (handlerLock);
          return GNUNET_SYSERR;
        }
      else
        {
          plaintextHandlers[type][pos] = plaintextHandlers[type][last - 1];
          plaintextHandlers[type][last - 1] = NULL;
          last++;
          GNUNET_array_grow (plaintextHandlers[type], last, last - 1);
          GNUNET_mutex_unlock (handlerLock);
          return GNUNET_OK;
        }
    }
  GNUNET_mutex_unlock (handlerLock);
  return GNUNET_SYSERR;
}



/**
 * Unregister a method as a handler for specific message types. Only
 * for plaintext messages!
 *
 * @param type the message type
 * @param callback the method to call if a message of
 *        that type is received
 * @return GNUNET_OK on success, GNUNET_SYSERR if there is a different
 *        handler for that type or if core threads are running
 *        and updates to the handler list are illegal!
 */
int
isHandlerRegistered (unsigned short type, unsigned short handlerType)
{
  int pos;
  int ret;

  if (handlerType == 3)
    return isCSHandlerRegistered (type);
  if (handlerType > 3)
    {
      GE_BREAK (ectx, 0);
      return GNUNET_SYSERR;
    }
  ret = 0;
  GNUNET_mutex_lock (handlerLock);
  if (type < plaintextmax_registeredType)
    {
      pos = 0;
      while (plaintextHandlers[type][pos] != NULL)
        pos++;
      if ((handlerType == 0) || (handlerType == 2))
        ret += pos;
    }
  if (type < max_registeredType)
    {
      pos = 0;
      while (handlers[type][pos] != NULL)
        pos++;
      if ((handlerType == 1) || (handlerType == 2))
        ret += pos;
    }
  GNUNET_mutex_unlock (handlerLock);
  return ret;
}


/**
 * Handle a message (that was decrypted if needed).
 * Processes the message by calling the registered
 * handler for each message part.
 *
 * @param encrypted GNUNET_YES if it was encrypted,
 *    GNUNET_NO if plaintext,
 * @param session NULL if not available
 */
void
injectMessage (const GNUNET_PeerIdentity * sender,
               const char *msg,
               unsigned int size, int wasEncrypted, TSession * session)
{
  unsigned int pos;
  const GNUNET_MessageHeader *part;
  GNUNET_MessageHeader cpart;
  GNUNET_MessageHeader *copy;
  int last;
  GNUNET_EncName enc;
#if MEASURE_TIME
  GNUNET_CronTime now;
#endif
#if VALIDATE_CLIENT
  void *old_value;
#endif

  pos = 0;
  copy = NULL;
  while (pos < size)
    {
      unsigned short plen;
      unsigned short ptyp;

      GNUNET_free_non_null (copy);
      copy = NULL;
      memcpy (&cpart, &msg[pos], sizeof (GNUNET_MessageHeader));
      plen = htons (cpart.size);
      if (pos + plen > size)
        {
          if (sender != NULL)
            {
              IF_GELOG (ectx,
                        GE_WARNING | GE_USER | GE_BULK,
                        GNUNET_hash_to_enc (&sender->hashPubKey, &enc));
              GE_LOG (ectx,
                      GE_WARNING | GE_USER | GE_BULK,
                      _("Received corrupt message from peer `%s'in %s:%d.\n"),
                      &enc, __FILE__, __LINE__);
            }
          else
            {
              GE_BREAK (ectx, 0);
            }
          return;
        }
      if ((pos % sizeof (long)) != 0)
        {
          /* correct misalignment; we allow messages to _not_ be a
             multiple of sizeof(long) bytes (if absolutely necessary; it should be
             avoided where the cost for doing so is not prohibitive);
             however we also (need to) guaranteed word-alignment for the
             handlers; so we must re-align the message if it is
             misaligned. */
          copy = GNUNET_malloc (plen);
          memcpy (copy, &msg[pos], plen);
          part = copy;
        }
      else
        {
          part = (const GNUNET_MessageHeader *) &msg[pos];
        }
      pos += plen;

      ptyp = htons (part->type);
#if DEBUG_HANDLER
      if (sender != NULL)
        {
          IF_GELOG (ectx, GE_DEBUG,
                    GNUNET_hash_to_enc (&sender->hashPubKey, &enc));
          GE_LOG (ectx, GE_DEBUG,
                  "Received %s message of type %u from peer `%s'\n",
                  wasEncrypted ? "encrypted" : "plaintext", ptyp, &enc);
        }
#endif
      if (GNUNET_YES == wasEncrypted)
        {
          MessagePartHandler callback;

          if ((ptyp >= max_registeredType) || (NULL == handlers[ptyp][0]))
            {
              GE_LOG (ectx,
                      GE_DEBUG | GE_USER | GE_REQUEST,
                      "Encrypted message of type '%d' not understood (no handler registered).\n",
                      ptyp);
              continue;         /* no handler registered, go to next part */
            }
#if MEASURE_TIME
          now = GNUNET_get_time ();
#endif
          last = 0;
          while (NULL != (callback = handlers[ptyp][last]))
            {
#if VALIDATE_CLIENT
              old_value = GNUNET_malloc (plen);
              memcpy (old_value, part, plen);
#endif
              if (GNUNET_SYSERR == callback (sender, part))
                {
#if DEBUG_HANDLER
                  GE_LOG (ectx,
                          GE_DEBUG | GE_USER | GE_BULK,
                          "Handler aborted message processing after receiving message of type '%d'.\n",
                          ptyp);
#endif
                  GNUNET_free_non_null (copy);
                  copy = NULL;
#if VALIDATE_CLIENT
                  GNUNET_free (old_value);
#endif
                  return;       /* handler says: do not process the rest of the message */
                }
#if VALIDATE_CLIENT
              if (0 != memcmp (old_value, part, plen))
                GE_LOG (ectx,
                        GE_ERROR | GE_DEVELOPER | GE_IMMEDIATE,
                        "Handler %d at %p violated const!\n", ptyp, callback);
              GNUNET_free (old_value);
#endif

              last++;
            }
#if MEASURE_TIME
          if (ptyp < P2P_PROTO_MAX_USED)
            {
              time_by_type[ptyp] += GNUNET_get_time () - now;
              count_by_type[ptyp]++;
            }
#endif
        }
      else
        {                       /* isEncrypted == GNUNET_NO */
          PlaintextMessagePartHandler callback;

          if ((ptyp >= plaintextmax_registeredType) ||
              (NULL == plaintextHandlers[ptyp][0]))
            {
              GE_LOG (ectx,
                      GE_REQUEST | GE_DEBUG | GE_USER,
                      "Plaintext message of type '%d' not understood (no handler registered).\n",
                      ptyp);
              continue;         /* no handler registered, go to next part */
            }
#if MEASURE_TIME
          now = GNUNET_get_time ();
#endif
          last = 0;
          while (NULL != (callback = plaintextHandlers[ptyp][last]))
            {
              if (GNUNET_SYSERR == callback (sender, part, session))
                {
#if DEBUG_HANDLER
                  GE_LOG (ectx,
                          GE_DEBUG | GE_USER | GE_BULK,
                          "Handler aborted message processing after receiving message of type '%d'.\n",
                          ptyp);
#endif
                  GNUNET_free_non_null (copy);
                  copy = NULL;
                  return;       /* handler says: do not process the rest of the message */
                }
              last++;
            }
#if MEASURE_TIME
          if (ptyp < P2P_PROTO_MAX_USED)
            {
              time_by_type[ptyp] += GNUNET_get_time () - now;
              count_by_type[ptyp]++;
            }
#endif

        }                       /* if plaintext */
    }                           /* while loop */
  GNUNET_free_non_null (copy);
  copy = NULL;
}

/**
 * Message dispatch/handling.
 *
 * @param tsession transport session that received the message (maybe NULL)
 * @param sender the sender of the message
 * @param msg the message that was received. caller frees it on return
 * @param size the size of the message
 */
static void
handleMessage (TSession * tsession,
               const GNUNET_PeerIdentity * sender,
               const char *msg, unsigned int size)
{
  int ret;

  if ((tsession != NULL) &&
      (sender != NULL) &&
      (0 != memcmp (sender, &tsession->peer, sizeof (GNUNET_PeerIdentity))))
    {
      GE_BREAK (NULL, 0);
      return;
    }
  ret = checkHeader (sender, (P2P_PACKET_HEADER *) msg, size);
  if (ret == GNUNET_SYSERR)
    return;                     /* message malformed or failed to decrypt */
  if ((ret == GNUNET_YES) && (tsession != NULL) && (sender != NULL))
    considerTakeover (sender, tsession);
  injectMessage (sender,
                 &msg[sizeof (P2P_PACKET_HEADER)],
                 size - sizeof (P2P_PACKET_HEADER), ret, tsession);
}

/**
 * This is the main loop of each thread.  It loops *forever* waiting
 * for incomming packets in the packet queue. Then it calls "handle"
 * (defined in handler.c) on the packet.
 */
static void *
threadMain (void *cls)
{
  P2P_PACKET *mp;

  while (mainShutdownSignal == NULL)
    {
      GNUNET_semaphore_down (bufferQueueRead_, GNUNET_YES);
      /* handle buffer entry */
      /* sync with other handlers to get buffer */
      if (mainShutdownSignal != NULL)
        break;
      GNUNET_mutex_lock (globalLock_);
      mp = bufferQueue_[bq_firstFull_];
      bufferQueue_[bq_firstFull_++] = NULL;
      if (bq_firstFull_ == QUEUE_LENGTH)
        bq_firstFull_ = 0;
      GNUNET_mutex_unlock (globalLock_);
      /* end of sync */
      GNUNET_semaphore_up (bufferQueueWrite_);
      /* handle buffer - now out of sync */
      handleMessage (mp->tsession, &mp->sender, mp->msg, mp->size);
      if (mp->tsession != NULL)
        transport->disconnect (mp->tsession, __FILE__);
      GNUNET_free (mp->msg);
      GNUNET_free (mp);
    }
  GNUNET_semaphore_up (mainShutdownSignal);
  return NULL;
}                               /* end of threadMain */

/**
 * Processing of a message from the transport layer
 * (receive implementation).
 */
void
core_receive (P2P_PACKET * mp)
{
  if ((mp->tsession != NULL) &&
      (0 !=
       memcmp (&mp->sender, &mp->tsession->peer,
               sizeof (GNUNET_PeerIdentity))))
    {
      GE_BREAK (NULL, 0);
      GNUNET_free (mp->msg);
      GNUNET_free (mp);
      return;
    }
  if ((threads_running == GNUNET_NO) || (mainShutdownSignal != NULL))
    {
#if TRACK_DISCARD
      if (globalLock_ != NULL)
        GNUNET_mutex_lock (globalLock_);
      discarded++;
      if (0 == discarded % 64)
        GE_LOG (ectx,
                GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
                "Accepted: %u discarded: %u blacklisted: %u, ratio: %f\n",
                accepted,
                discarded,
                blacklisted, 1.0 * accepted / (blacklisted + discarded + 1));
      if (globalLock_ != NULL)
        GNUNET_mutex_unlock (globalLock_);
#endif
    }
  /* check for blacklisting */
  if (GNUNET_YES == identity->isBlacklisted (&mp->sender, GNUNET_YES))
    {
#if DEBUG_HANDLER
      GNUNET_EncName enc;
      IF_GELOG (ectx,
                GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
                GNUNET_hash_to_enc (&mp->sender.hashPubKey, &enc));
      GE_LOG (ectx,
              GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
              "Strictly blacklisted peer `%s' sent message, dropping for now.\n",
              (char *) &enc);
#endif
#if TRACK_DISCARD
      GNUNET_mutex_lock (globalLock_);
      blacklisted++;
      if (0 == blacklisted % 64)
        GE_LOG (ectx,
                GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
                "Accepted: %u discarded: %u blacklisted: %u, ratio: %f\n",
                accepted,
                discarded,
                blacklisted, 1.0 * accepted / (blacklisted + discarded + 1));
      GNUNET_mutex_unlock (globalLock_);
#endif
      GNUNET_free (mp->msg);
      GNUNET_free (mp);
      return;
    }
  if ((threads_running == GNUNET_NO) ||
      (mainShutdownSignal != NULL) ||
      (GNUNET_SYSERR == GNUNET_semaphore_down (bufferQueueWrite_, GNUNET_NO)))
    {
      /* discard message, buffer is full or
         we're shut down! */
#if 0
      GE_LOG (ectx,
              GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
              "Discarding message of size %u -- buffer full!\n", mp->size);
#endif
      GNUNET_free (mp->msg);
      GNUNET_free (mp);
#if TRACK_DISCARD
      if (globalLock_ != NULL)
        GNUNET_mutex_lock (globalLock_);
      discarded++;
      if (0 == discarded % 64)
        GE_LOG (ectx,
                GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
                "Accepted: %u discarded: %u blacklisted: %u, ratio: %f\n",
                accepted,
                discarded,
                blacklisted, 1.0 * accepted / (blacklisted + discarded + 1));
      if (globalLock_ != NULL)
        GNUNET_mutex_unlock (globalLock_);
#endif
      return;
    }
  /* try to increment session reference count */
  if ((mp->tsession != NULL) &&
      (GNUNET_SYSERR == transport->associate (mp->tsession, __FILE__)))
    mp->tsession = NULL;

  GNUNET_mutex_lock (globalLock_);
  if (bq_firstFree_ == QUEUE_LENGTH)
    bq_firstFree_ = 0;
  bufferQueue_[bq_firstFree_++] = mp;
#if TRACK_DISCARD
  accepted++;
  if (0 == accepted % 64)
    GE_LOG (ectx,
            GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
            "Accepted: %u discarded: %u blacklisted: %u, ratio: %f\n",
            accepted,
            discarded,
            blacklisted, 1.0 * accepted / (blacklisted + discarded + 1));
#endif
  GNUNET_mutex_unlock (globalLock_);
  GNUNET_semaphore_up (bufferQueueRead_);
}

/**
 * Start processing p2p messages.
 */
void
enableCoreProcessing ()
{
  int i;

  globalLock_ = GNUNET_mutex_create (GNUNET_NO);
  for (i = 0; i < QUEUE_LENGTH; i++)
    bufferQueue_[i] = NULL;
  bq_firstFree_ = 0;
  bq_firstFull_ = 0;

  /* create message handling threads */
  threads_running = GNUNET_YES;
  for (i = 0; i < THREAD_COUNT; i++)
    {
      threads_[i] = GNUNET_thread_create (&threadMain, &i, 128 * 1024);
      if (threads_[i] == NULL)
        GE_LOG_STRERROR (ectx, GE_ERROR, "pthread_create");
    }
}

/**
 * Stop processing (p2p) messages.
 */
void
disableCoreProcessing ()
{
  int i;
  void *unused;

  /* shutdown processing of inbound messages... */
  threads_running = GNUNET_NO;
  mainShutdownSignal = GNUNET_semaphore_create (0);
  for (i = 0; i < THREAD_COUNT; i++)
    {
      GNUNET_semaphore_up (bufferQueueRead_);
      GNUNET_semaphore_down (mainShutdownSignal, GNUNET_YES);
    }
  for (i = 0; i < THREAD_COUNT; i++)
    {
      GNUNET_thread_join (threads_[i], &unused);
      threads_[i] = NULL;
    }
  GNUNET_semaphore_destroy (mainShutdownSignal);
  mainShutdownSignal = NULL;
  GNUNET_mutex_destroy (globalLock_);
  globalLock_ = NULL;
}

/**
 * Initialize message handling module.
 */
void
initHandler (struct GE_Context *e)
{
  ectx = e;
  handlerLock = GNUNET_mutex_create (GNUNET_NO);
  transport = requestService ("transport");
  GE_ASSERT (ectx, transport != NULL);
  identity = requestService ("identity");
  GE_ASSERT (ectx, identity != NULL);
  /* initialize sync mechanisms for message handling threads */
  bufferQueueRead_ = GNUNET_semaphore_create (0);
  bufferQueueWrite_ = GNUNET_semaphore_create (QUEUE_LENGTH);
}

/**
 * Shutdown message handling module.
 */
void
doneHandler ()
{
  unsigned int i;

  /* free datastructures */
  GNUNET_semaphore_destroy (bufferQueueRead_);
  bufferQueueRead_ = NULL;
  GNUNET_semaphore_destroy (bufferQueueWrite_);
  bufferQueueWrite_ = NULL;
  for (i = 0; i < QUEUE_LENGTH; i++)
    {
      if (bufferQueue_[i] != NULL)
        GNUNET_free_non_null (bufferQueue_[i]->msg);
      GNUNET_free_non_null (bufferQueue_[i]);
    }

  GNUNET_mutex_destroy (handlerLock);
  handlerLock = NULL;
  for (i = 0; i < max_registeredType; i++)
    {
      unsigned int last = 0;
      while (handlers[i][last] != NULL)
        last++;
      last++;
      GNUNET_array_grow (handlers[i], last, 0);
    }
  GNUNET_array_grow (handlers, max_registeredType, 0);
  for (i = 0; i < plaintextmax_registeredType; i++)
    {
      unsigned int last = 0;
      while (plaintextHandlers[i][last] != NULL)
        last++;
      GNUNET_array_grow (plaintextHandlers[i], last, 0);
    }
  GNUNET_array_grow (plaintextHandlers, plaintextmax_registeredType, 0);
  releaseService (transport);
  transport = NULL;
  releaseService (identity);
  identity = NULL;
#if MEASURE_TIME
  for (i = 0; i < P2P_PROTO_MAX_USED; i++)
    {
      if (count_by_type[i] == 0)
        continue;
      GE_LOG (ectx,
              GE_DEBUG | GE_DEVELOPER | GE_REQUEST,
              "%10u msgs of type %2u took %16llu ms (%llu on average)\n",
              count_by_type[i],
              i, time_by_type[i], time_by_type[i] / count_by_type[i]);
    }
#endif
}


/* end of handler.c */
