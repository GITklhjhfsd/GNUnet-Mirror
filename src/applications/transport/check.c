
/**
 * Transport Session handle.
 */
typedef struct TCPSession
{

  struct TCPSession *next;

  /**
   * the tcp socket (used to identify this connection with selector)
   */
  struct GNUNET_SocketHandle *sock;

  /**
   * Our tsession.
   */
  GNUNET_TSession *tsession;

  /**
   * mutex for synchronized access to 'users'
   */
  struct GNUNET_Mutex *lock;

  /**
   * To whom are we talking to (set to our identity
   * if we are still waiting for the welcome message)
   */
  GNUNET_PeerIdentity sender;

  /**
   * Are we still expecting the welcome? (GNUNET_YES/GNUNET_NO)
   */
  int expectingWelcome;

  /**
   * number of users of this session (reference count)
   */
  int users;

  /**
   * Is this session active with select?
   */
  int in_select;

  void *accept_addr;

  unsigned int addr_len;

} TCPSession;

static void
check (GNUNET_TSession * session)
{
  TCPSession *tcp;

  if (session->ttype != GNUNET_TRANSPORT_PROTOCOL_NUMBER_TCP)
    return;
  tcp = session->internal;
  GNUNET_GE_ASSERT (NULL, tcp->users >= session->token_count);
}

#define CHECK(s) check(s)
