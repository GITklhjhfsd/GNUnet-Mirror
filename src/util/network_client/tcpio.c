/*
     This file is part of GNUnet.
     (C) 2001, 2002, 2006 Christian Grothoff (and other contributing authors)

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
 * @file util/network/tcpio.c
 * @brief code for synchronized access to TCP streams
 * @author Christian Grothoff
 *
 * Generic TCP code for reliable, mostly blocking, record-oriented TCP
 * connections. GNUnet uses the "tcpio" code for trusted client-server
 * (e.g. gnunet-gtk to gnunetd via loopback) communications.  Note
 * that an unblocking write is also provided since if both client and
 * server use blocking IO, both may block on a write and cause a
 * mutual inter-process deadlock.
 *
 * Since we do not want other peers (!) to be able to block a peer by
 * not reading from the TCP stream, the peer-to-peer TCP transport
 * uses unreliable, buffered, non-blocking, record-oriented TCP code
 * with a select call to reduce the number of threads which is
 * provided in transports/tcp.c.
 */

#include "gnunet_util_network.h"
#include "gnunet_util_os.h"
#include "gnunet_util_config.h"
#include "gnunet_protocols.h"
#include "platform.h"

#define DEBUG_TCPIO GNUNET_NO

/**
 * Struct to refer to a GNUnet TCP connection.
 * This is more than just a socket because if the server
 * drops the connection, the client automatically tries
 * to reconnect (and for that needs connection information).
 */
typedef struct GNUNET_ClientServerConnection
{

  /**
   * the socket handle, NULL if not life
   */
  struct GNUNET_SocketHandle *sock;

  struct GNUNET_Mutex *readlock;

  struct GNUNET_Mutex *writelock;

  struct GNUNET_Mutex *destroylock;

  struct GE_Context *ectx;

  struct GC_Configuration *cfg;

  int dead;

} ClientServerConnection;


/**
 * Return the port-number (in host byte order)
 * @return 0 on error
 */
static unsigned short
getGNUnetPort (struct GE_Context *ectx, struct GC_Configuration *cfg)
{
  char *res;
  char *pos;
  unsigned int port;

  res = NULL;
  if (-1 == GC_get_configuration_value_string (cfg,
                                               "NETWORK",
                                               "HOST",
                                               "localhost:2087", &res))
    {
      GE_LOG (ectx,
              GE_ERROR | GE_USER | GE_BULK,
              _("Could not find valid value for HOST in section NETWORK."));
      return 2087;
    }
  pos = strstr (res, ":");
  if (pos == NULL)
    {
      GNUNET_free (res);
      return 2087;
    }
  pos++;
  if (1 != SSCANF (pos, "%u", &port))
    {
      GE_LOG (ectx,
              GE_ERROR | GE_USER | GE_BULK,
              _
              ("Syntax error in configuration entry HOST in section NETWORK: `%s'"),
              pos);
      GNUNET_free (res);
      return 2087;
    }
  GNUNET_free (res);
  return (unsigned short) port;
}

/**
 * Configuration: get the GNUnetd host where the client
 * should connect to (via TCP)
 *
 * @return the name of the host, NULL on error
 */
static char *
getGNUnetdHost (struct GE_Context *ectx, struct GC_Configuration *cfg)
{
  char *res;
  char *pos;

  res = NULL;
  if (-1 == GC_get_configuration_value_string (cfg,
                                               "NETWORK",
                                               "HOST",
                                               "localhost:2087", &res))
    {
      GE_LOG (ectx,
              GE_ERROR | GE_USER | GE_BULK,
              _("Could not find valid value for HOST in section NETWORK."));
      return NULL;
    }
  pos = strstr (res, ":");
  if (pos != NULL)
    *pos = '\0';
  return res;
}

struct GNUNET_ClientServerConnection *
GNUNET_client_connection_create (struct GE_Context *ectx,
                                 struct GC_Configuration *cfg)
{
  ClientServerConnection *result;

  result = GNUNET_malloc (sizeof (ClientServerConnection));
  result->sock = NULL;
  result->readlock = GNUNET_mutex_create (GNUNET_NO);
  result->writelock = GNUNET_mutex_create (GNUNET_NO);
  result->destroylock = GNUNET_mutex_create (GNUNET_YES);
  result->ectx = ectx;
  result->cfg = cfg;
  return result;
}

void
GNUNET_client_connection_close_temporarily (struct
                                            GNUNET_ClientServerConnection
                                            *sock)
{
  GE_ASSERT (NULL, sock != NULL);
  GNUNET_mutex_lock (sock->destroylock);
  if (sock->sock != NULL)
    {
      GNUNET_socket_close (sock->sock);
      GNUNET_mutex_lock (sock->readlock);
      GNUNET_mutex_lock (sock->writelock);
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      GNUNET_mutex_unlock (sock->writelock);
      GNUNET_mutex_unlock (sock->readlock);
    }
  GNUNET_mutex_unlock (sock->destroylock);
}

void
GNUNET_client_connection_close_forever (struct GNUNET_ClientServerConnection
                                        *sock)
{
  GE_ASSERT (NULL, sock != NULL);
  GNUNET_mutex_lock (sock->destroylock);
  if (sock->sock != NULL)
    {
      GNUNET_socket_close (sock->sock);
      GNUNET_mutex_lock (sock->readlock);
      GNUNET_mutex_lock (sock->writelock);
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      sock->dead = GNUNET_YES;
      GNUNET_mutex_unlock (sock->writelock);
      GNUNET_mutex_unlock (sock->readlock);
    }
  else
    {
      sock->dead = GNUNET_YES;
    }
  GNUNET_mutex_unlock (sock->destroylock);
}

void
GNUNET_client_connection_destroy (struct GNUNET_ClientServerConnection *sock)
{
  GE_ASSERT (NULL, sock != NULL);
  GNUNET_client_connection_close_forever (sock);
  GNUNET_mutex_destroy (sock->readlock);
  GNUNET_mutex_destroy (sock->writelock);
  GNUNET_mutex_destroy (sock->destroylock);
  GNUNET_free (sock);
}

int
GNUNET_client_connection_test_connected (struct GNUNET_ClientServerConnection
                                         *sock)
{
  return (sock->sock != NULL);
}

/**
 * Check a socket, open and connect if it is closed and it is a client-socket.
 */
int
GNUNET_client_connection_ensure_connected (struct
                                           GNUNET_ClientServerConnection
                                           *sock)
{
  struct sockaddr_in soaddr;
  fd_set rset;
  fd_set wset;
  fd_set eset;
  struct timeval timeout;
  int ret;
  int osock;
  unsigned short port;
  char *host;
  GNUNET_IPv4Address ip;

  GE_ASSERT (NULL, sock != NULL);
  if (sock->sock != NULL)
    return GNUNET_OK;
  if (sock->dead == GNUNET_YES)
    return GNUNET_SYSERR;
  port = getGNUnetPort (sock->ectx, sock->cfg);
  if (port == 0)
    return GNUNET_SYSERR;
  host = getGNUnetdHost (sock->ectx, sock->cfg);
  if (host == NULL)
    return GNUNET_SYSERR;
  if (GNUNET_SYSERR == GNUNET_get_host_by_name (sock->ectx, host, &ip))
    {
      GNUNET_free (host);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_lock (sock->destroylock);
  if (sock->sock != NULL)
    {
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_OK;
    }
  if (sock->dead == GNUNET_YES)
    {
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  osock = SOCKET (PF_INET, SOCK_STREAM, 6);     /* 6: TCP */
  if (osock == -1)
    {
      GE_LOG_STRERROR (sock->ectx,
                       GE_ERROR | GE_USER | GE_ADMIN | GE_BULK, "socket");
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  sock->sock = socket_create (sock->ectx, NULL, osock);
  GNUNET_socket_set_blocking (sock->sock, GNUNET_NO);
  memset (&soaddr, 0, sizeof (soaddr));
  soaddr.sin_family = AF_INET;
  GE_ASSERT (sock->ectx,
             sizeof (struct in_addr) == sizeof (GNUNET_IPv4Address));
  memcpy (&soaddr.sin_addr, &ip, sizeof (struct in_addr));
  soaddr.sin_port = htons (port);
  ret = CONNECT (osock, (struct sockaddr *) &soaddr, sizeof (soaddr));
  if ((ret != 0) && (errno != EINPROGRESS) && (errno != EWOULDBLOCK))
    {
      GE_LOG (sock->ectx,
              GE_WARNING | GE_USER | GE_BULK,
              _("Cannot connect to %s:%u: %s\n"),
              host, port, STRERROR (errno));
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  /* we call select() first with a timeout of WAIT_SECONDS to
     avoid blocking on a later write indefinitely;
     Important if a local firewall decides to just drop
     the TCP handshake... */
  FD_ZERO (&rset);
  FD_ZERO (&wset);
  FD_ZERO (&eset);
  FD_SET (osock, &wset);
  FD_SET (osock, &eset);
#define WAIT_SECONDS 1
  timeout.tv_sec = WAIT_SECONDS;
  timeout.tv_usec = 0;
  errno = 0;
  ret = SELECT (osock + 1, &rset, &wset, &eset, &timeout);
  if (ret == -1)
    {
      if (errno != EINTR)
        GE_LOG_STRERROR (sock->ectx,
                         GE_WARNING | GE_USER | GE_BULK, "select");
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  if (FD_ISSET (osock, &eset))
    {
      GE_LOG (sock->ectx,
              GE_WARNING | GE_USER | GE_BULK,
              _("Error connecting to %s:%u\n"), host, port);
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  if (!FD_ISSET (osock, &wset))
    {
      GE_LOG (sock->ectx,
              GE_WARNING | GE_USER | GE_BULK,
              _("Failed to connect to %s:%u in %ds\n"),
              host, port, WAIT_SECONDS);
      GNUNET_socket_destroy (sock->sock);
      sock->sock = NULL;
      GNUNET_free (host);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  GNUNET_free (host);
  GNUNET_socket_set_blocking (sock->sock, GNUNET_YES);
  GNUNET_mutex_unlock (sock->destroylock);
  return GNUNET_OK;
}

/**
 * Write to a GNUnet TCP socket.  Will also potentially complete the
 * sending of a previous non-blocking GNUNET_client_connection_write call.
 *
 * @param sock the socket to write to
 * @param buffer the buffer to write
 * @return GNUNET_OK if the write was sucessful, otherwise GNUNET_SYSERR.
 */
int
GNUNET_client_connection_write (struct GNUNET_ClientServerConnection *sock,
                                const GNUNET_MessageHeader * buffer)
{
  size_t size;
  size_t sent;
  int res;

  GNUNET_mutex_lock (sock->destroylock);
  GNUNET_mutex_lock (sock->writelock);
  if (GNUNET_SYSERR == GNUNET_client_connection_ensure_connected (sock))
    {
      GNUNET_mutex_unlock (sock->writelock);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_unlock (sock->destroylock);
  GE_ASSERT (NULL, sock->sock != NULL);
  size = ntohs (buffer->size);
  res =
    GNUNET_socket_send (sock->sock, GNUNET_NC_COMPLETE_TRANSFER, buffer, size,
                        &sent);
  if ((res != GNUNET_YES) || (sent != size))
    {
      GNUNET_mutex_unlock (sock->writelock);
      GNUNET_client_connection_close_temporarily (sock);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_unlock (sock->writelock);
  return GNUNET_OK;
}

int
GNUNET_client_connection_read (struct GNUNET_ClientServerConnection *sock,
                               GNUNET_MessageHeader ** buffer)
{
  int res;
  size_t pos;
  char *buf;
  unsigned short size;
  GNUNET_MessageReturnErrorMessage *rem;

  GNUNET_mutex_lock (sock->destroylock);
  GNUNET_mutex_lock (sock->readlock);
  if (GNUNET_OK != GNUNET_client_connection_ensure_connected (sock))
    {
      GNUNET_mutex_unlock (sock->readlock);
      GNUNET_mutex_unlock (sock->destroylock);
      return GNUNET_SYSERR;
    }
  GNUNET_mutex_unlock (sock->destroylock);
  GE_ASSERT (NULL, sock->sock != NULL);
  while (1)
    {
      pos = 0;
      res = 0;
      if ((GNUNET_OK != GNUNET_socket_recv (sock->sock,
                                            GNUNET_NC_COMPLETE_TRANSFER,
                                            &size,
                                            sizeof (unsigned short),
                                            &pos))
          || (pos != sizeof (unsigned short)))
        {
          GNUNET_mutex_unlock (sock->readlock);
          GNUNET_client_connection_close_temporarily (sock);
          return GNUNET_SYSERR;
        }
      size = ntohs (size);
      if (size < sizeof (GNUNET_MessageHeader))
        {
          GE_BREAK (sock->ectx, 0);
          GNUNET_mutex_unlock (sock->readlock);
          GNUNET_client_connection_close_temporarily (sock);
          return GNUNET_SYSERR; /* invalid header */
        }

      buf = GNUNET_malloc (size);
      if ((GNUNET_OK != GNUNET_socket_recv (sock->sock,
                                            GNUNET_NC_COMPLETE_TRANSFER,
                                            &buf[pos],
                                            size - pos,
                                            &pos)) ||
          (pos + sizeof (unsigned short) != size))
        {
          GNUNET_free (buf);
          GNUNET_mutex_unlock (sock->readlock);
          GNUNET_client_connection_close_temporarily (sock);
          return GNUNET_SYSERR;
        }
#if DEBUG_TCPIO
      GE_LOG (sock->ectx,
              GE_DEBUG | GE_REQUEST | GE_USER,
              "Successfully received %d bytes from TCP socket.\n", size);
#endif
      *buffer = (GNUNET_MessageHeader *) buf;
      (*buffer)->size = htons (size);

      if (ntohs ((*buffer)->type) != CS_PROTO_RETURN_ERROR)
        break;                  /* got actual message! */
      rem = (GNUNET_MessageReturnErrorMessage *) * buffer;
      if (ntohs (rem->header.size) <
          sizeof (GNUNET_MessageReturnErrorMessage))
        {
          GE_BREAK (sock->ectx, 0);
          GNUNET_mutex_unlock (sock->readlock);
          GNUNET_client_connection_close_temporarily (sock);
          GNUNET_free (buf);
          return GNUNET_SYSERR;
        }
      size =
        ntohs (rem->header.size) - sizeof (GNUNET_MessageReturnErrorMessage);
      GE_LOG (sock->ectx, ntohl (rem->kind), "%.*s", (int) size, &rem[1]);
      GNUNET_free (rem);
    }                           /* while (1) */
  GNUNET_mutex_unlock (sock->readlock);
  return GNUNET_OK;             /* success */
}

/**
 * Obtain a return value from a remote call from TCP.
 *
 * @param sock the TCP socket
 * @param ret the return value from TCP
 * @return GNUNET_SYSERR on error, GNUNET_OK if the return value was read
 * successfully
 */
int
GNUNET_client_connection_read_result (struct GNUNET_ClientServerConnection
                                      *sock, int *ret)
{
  GNUNET_MessageReturnValue *rv;

  rv = NULL;
  if (GNUNET_SYSERR ==
      GNUNET_client_connection_read (sock, (GNUNET_MessageHeader **) & rv))
    return GNUNET_SYSERR;
  if ((ntohs (rv->header.size) != sizeof (GNUNET_MessageReturnValue)) ||
      (ntohs (rv->header.type) != CS_PROTO_RETURN_VALUE))
    {
      GE_LOG (sock->ectx,
              GE_WARNING | GE_DEVELOPER | GE_BULK,
              _("`%s' failed, reply invalid!\n"), __FUNCTION__);
      GNUNET_free (rv);
      return GNUNET_SYSERR;
    }
  *ret = ntohl (rv->return_value);
  GNUNET_free (rv);
  return GNUNET_OK;
}

/**
 * Send a return value to the caller of a remote call via
 * TCP.
 * @param sock the TCP socket
 * @param ret the return value to send via TCP
 * @return GNUNET_SYSERR on error, GNUNET_OK if the return value was
 *         send successfully
 */
int
GNUNET_client_connection_write_result (struct GNUNET_ClientServerConnection
                                       *sock, int ret)
{
  GNUNET_MessageReturnValue rv;

  rv.header.size = htons (sizeof (GNUNET_MessageReturnValue));
  rv.header.type = htons (CS_PROTO_RETURN_VALUE);
  rv.return_value = htonl (ret);
  return GNUNET_client_connection_write (sock, &rv.header);
}

/*  end of tcpio.c */
