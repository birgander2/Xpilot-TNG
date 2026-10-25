/* 
 * XPilot NG, a multiplayer space war game.
 *
 * Copyright (C) 1991-2001 by
 *
 *      Bjørn Stabell        <bjoern@xpilot.org>
 *      Ken Ronny Schouten   <ken@xpilot.org>
 *      Bert Gijsbers        <bert@xpilot.org>
 *      Dick Balaska         <dick@xpilot.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "xpcommon.h"

/* Debug macro */
#ifdef DEBUG
# define DEB(x) x
#else
# define DEB(x)
#endif

#ifndef timerclear
# define timerclear(tvp)   ((tvp)->tv_sec = (tvp)->tv_usec = 0)
#endif

static int sock_resolve_addr(const char *name, struct sockaddr_storage *out,
                             socklen_t *out_len, int port, int socktype);

#ifdef _WIN32
/*
 * Winsock reports errors through WSAGetLastError() and leaves errno alone,
 * but callers test errno (EWOULDBLOCK, EINTR, ECONNREFUSED, ...).
 * Translate the last Winsock error, store it in errno and return it.
 */
int sock_last_error(void)
{
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK:	errno = EWOULDBLOCK; break;
    case WSAEINPROGRESS:	errno = EINPROGRESS; break;
    case WSAEALREADY:		errno = EALREADY; break;
    case WSAEINTR:		errno = EINTR; break;
    case WSAECONNREFUSED:	errno = ECONNREFUSED; break;
    case WSAECONNRESET:		errno = ECONNRESET; break;
    case WSAECONNABORTED:	errno = ECONNABORTED; break;
    case WSAENOTCONN:		errno = ENOTCONN; break;
    case WSAEMSGSIZE:		errno = EMSGSIZE; break;
    case WSAENOBUFS:		errno = ENOBUFS; break;
    case WSAEINVAL:		errno = EINVAL; break;
    case WSAEBADF:
    case WSAENOTSOCK:		errno = EBADF; break;
    case WSAEADDRINUSE:		errno = EADDRINUSE; break;
    case WSAEADDRNOTAVAIL:	errno = EADDRNOTAVAIL; break;
    case WSAEAFNOSUPPORT:	errno = EAFNOSUPPORT; break;
    case WSAENETUNREACH:	errno = ENETUNREACH; break;
    case WSAEHOSTUNREACH:	errno = EHOSTUNREACH; break;
    case WSAETIMEDOUT:		errno = ETIMEDOUT; break;
    case WSAEACCES:		errno = EACCES; break;
    default:			errno = EIO; break;
    }
    return errno;
}

int sock_close_fd(int fd)
{
    return closesocket((SOCKET)fd);
}
#else
int sock_last_error(void)
{
    return errno;
}

int sock_close_fd(int fd)
{
    return close(fd);
}
#endif


static void sock_flags_add(sock_t *sock, unsigned bits)
{
    sock->flags |= bits;
}

static void sock_flags_set(sock_t *sock, unsigned bits)
{
    sock->flags = bits;
}

static void sock_flags_remove(sock_t *sock, unsigned bits)
{
    sock->flags &= ~bits;
}

static int sock_flags_test_all(sock_t *sock, unsigned bits)
{
    return (sock->flags & bits) == (unsigned)bits;
}

static int sock_flags_test_any(sock_t *sock, unsigned bits)
{
    return (sock->flags & bits) != 0;
}

static int sock_set_error(sock_t *sock, int err, sock_call_t call, int line)
{
    DEB(printf("set error %d, %d, %d.  \"%s\"\n",
	       err, call, line, strerror(err)));
    sock->error.error = err;
    sock->error.call = call;
    sock->error.line = line;

    return SOCK_IS_ERROR;
}

static int sock_check(sock_t *sock)
{
    if (!sock_flags_test_all(sock, SOCK_FLAG_INIT))
	return sock_set_error(sock, EINVAL, SOCK_CALL_ANY, __LINE__);

    return SOCK_IS_OK;
}

static int sock_alloc_hostname(sock_t *sock)
{
    if (!sock->hostname) {
	sock->hostname = (char *) malloc(SOCK_HOSTNAME_LENGTH);
	if (!sock->hostname)
	    sock_set_error(sock, errno, SOCK_CALL_ANY, __LINE__);
	else
	    sock->hostname[0] = '\0';
    }

    return (sock->hostname) ? SOCK_IS_OK : SOCK_IS_ERROR;
}

static void sock_free_hostname(sock_t *sock)
{
    if (sock->hostname) {
	free(sock->hostname);
	sock->hostname = NULL;
    }
}

static int sock_alloc_lastaddr(sock_t *sock)
{
    if (!sock->lastaddr) {
	sock->lastaddr = (void *) calloc(1, sizeof(struct sockaddr_storage));
	if (!sock->lastaddr)
	    sock_set_error(sock, errno, SOCK_CALL_ANY, __LINE__);
    }

    return (sock->lastaddr) ? SOCK_IS_OK : SOCK_IS_ERROR;
}

static void sock_free_lastaddr(sock_t *sock)
{
    if (sock->lastaddr) {
	free(sock->lastaddr);
	sock->lastaddr = NULL;
    }
}

#ifdef _WIN32
static int sock_started;
#endif

/* Idempotent; the sock_open_*() functions call it on demand. */
int sock_startup(void)
{
#ifdef _WIN32
    WSADATA wsaData;

    if (sock_started)
	return 0;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
	return -1;
    sock_started = 1;
#endif
    return 0; /* socket initialization only needed for windows */
}

void sock_cleanup(void)
{
#ifdef _WIN32
    if (sock_started) {
	WSACleanup();
	sock_started = 0;
    }
#endif
}

int sock_init(sock_t *sock)
{
    memset(sock, 0, sizeof(*sock));

    sock_flags_set(sock, SOCK_FLAG_INIT);
    sock->fd = SOCK_FD_INVALID;
    sock->hostname = (char *) NULL;
    sock->lastaddr = (void *) NULL;
    sock->timeout.seconds = SOCK_TIMEOUT_SECONDS;

    return sock_check(sock);
}

static int sock_close_tcp(sock_t *sock)
{
    int			status = SOCK_IS_OK;

    if (sock_close_fd(sock->fd) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_CLOSE, __LINE__);
	status = SOCK_IS_ERROR;
    }
    sock_flags_remove(sock, SOCK_FLAG_TCP);
    sock->fd = SOCK_FD_INVALID;

    return status;
}

static int sock_close_udp(sock_t *sock)
{
    int			status = SOCK_IS_OK;

    if (sock_close_fd(sock->fd) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_CLOSE, __LINE__);
	status = SOCK_IS_ERROR;
    }
    sock_flags_remove(sock, SOCK_FLAG_UDP);
    sock->fd = SOCK_FD_INVALID;

    return status;
}

int sock_close(sock_t *sock)
{
    sock_free_hostname(sock);
    sock_free_lastaddr(sock);
    if (sock_flags_test_any(sock, SOCK_FLAG_UDP))
	return sock_close_udp(sock);
    if (sock_flags_test_any(sock, SOCK_FLAG_TCP))
	return sock_close_tcp(sock);
    return sock_set_error(sock, EINVAL, SOCK_CALL_ANY, __LINE__);
}

int sock_open_tcp(sock_t *sock)
{
    if (sock_init(sock) || sock_startup())
	return SOCK_IS_ERROR;

    if ((sock->fd = (int)socket(AF_INET, SOCK_STREAM, 0)) < 0)
	return sock_set_error(sock, sock_last_error(), SOCK_CALL_SOCKET, __LINE__);

    sock_flags_add(sock, SOCK_FLAG_TCP);

    return SOCK_IS_OK;
}

int sock_set_non_blocking(sock_t *sock, int flag)
{
/*
 * There are some problems on some particular systems (suns) with
 * getting sockets to be non-blocking.  Just try all possible ways
 * until one of them succeeds.  Please keep us informed by e-mail
 * to xpilot@xpilot.org.
 */

#ifndef USE_FCNTL_O_NONBLOCK
# ifndef USE_FCNTL_O_NDELAY
#  ifndef USE_FCNTL_FNDELAY
#   ifndef USE_IOCTL_FIONBIO

#    if defined(_SEQUENT_) || defined(__svr4__) || defined(SVR4)
#     define USE_FCNTL_O_NDELAY
#    elif defined(__sun__) && defined(FNDELAY)
#     define USE_FCNTL_FNDELAY
#    elif defined(FIONBIO)
#     define USE_IOCTL_FIONBIO
#    elif defined(FNDELAY)
#     define USE_FCNTL_FNDELAY
#    elif defined(O_NONBLOCK)
#     define USE_FCNTL_O_NONBLOCK
#    else
#     define USE_FCNTL_O_NDELAY
#    endif

#    if 0
#     if defined(FNDELAY) && defined(F_SETFL)
#      define USE_FCNTL_FNDELAY
#     endif
#     if defined(O_NONBLOCK) && defined(F_SETFL)
#      define USE_FCNTL_O_NONBLOCK
#     endif
#     if defined(FIONBIO)
#      define USE_IOCTL_FIONBIO
#     endif
#     if defined(O_NDELAY) && defined(F_SETFL)
#      define USE_FCNTL_O_NDELAY
#     endif
#    endif

#   endif
#  endif
# endif
#endif

    char buf[128];

#ifdef _WIN32
    {
	u_long arg = (flag != 0);

	if (ioctlsocket((SOCKET)sock->fd, FIONBIO, &arg) == 0)
	    return SOCK_IS_OK;
	sock_set_error(sock, sock_last_error(), SOCK_CALL_FCNTL, __LINE__);
	sprintf(buf, "ioctlsocket FIONBIO failed in socklib.c line %d", __LINE__);
	perror(buf);
	return SOCK_IS_ERROR;
    }
#else /* !_WIN32 */

#ifdef USE_FCNTL_FNDELAY
    if (fcntl(sock->fd, F_SETFL, (flag != 0) ? FNDELAY : 0) != -1)
	return SOCK_IS_OK;
    sock_set_error(sock, errno, SOCK_CALL_FCNTL, __LINE__);
    sprintf(buf, "fcntl FNDELAY failed in socklib.c line %d", __LINE__);
    perror(buf);
#endif

#ifdef USE_IOCTL_FIONBIO
    if (ioctl(sock->fd, FIONBIO, &flag) == 0)
	return SOCK_IS_OK;
    sock_set_error(sock, errno, SOCK_CALL_FCNTL, __LINE__);
    sprintf(buf, "ioctl FIONBIO failed in socklib.c line %d", __LINE__);
    perror(buf);
#endif

#ifdef USE_FCNTL_O_NONBLOCK
    if (fcntl(sock->fd, F_SETFL, (flag != 0) ? O_NONBLOCK : 0) != -1)
	return SOCK_IS_OK;
    sock_set_error(sock, errno, SOCK_CALL_FCNTL, __LINE__);
    sprintf(buf, "fcntl O_NONBLOCK failed in socklib.c line %d", __LINE__);
    perror(buf);
#endif

#ifdef USE_FCNTL_O_NDELAY
    if (fcntl(sock->fd, F_SETFL, (flag != 0) ? O_NDELAY : 0) != -1)
	return SOCK_IS_OK;
    sock_set_error(sock, errno, SOCK_CALL_FCNTL, __LINE__);
    sprintf(buf, "fcntl O_NDELAY failed in socklib.c line %d", __LINE__);
    perror(buf);
#endif

#endif /* _WIN32 */

    return SOCK_IS_ERROR;
}

static int sock_resolve_addr(const char *name, struct sockaddr_storage *out,
                             socklen_t *out_len, int port, int socktype)
{
    struct addrinfo hints, *res = NULL;
    char port_str[16];

    memset(&hints, 0, sizeof(hints));
    /*
     * All our sockets are AF_INET.  With AF_UNSPEC "localhost" may resolve
     * to ::1 first (e.g. Raspberry Pi OS), which fails with EAFNOSUPPORT.
     */
    hints.ai_family = AF_INET;
    hints.ai_socktype = socktype;

    if (port > 0)
        snprintf(port_str, sizeof(port_str), "%d", port);

    if (getaddrinfo(name, (port > 0) ? port_str : NULL, &hints, &res) != 0 || !res)
        return -1;

    memcpy(out, res->ai_addr, res->ai_addrlen);
    if (out_len)
        *out_len = res->ai_addrlen;

    freeaddrinfo(res);
    return 0;
}

int sock_open_tcp_connected_non_blocking(sock_t *sock, char *host, int port)
{
    struct sockaddr_storage dest;
    socklen_t dest_len = sizeof(dest);

    if (sock_open_tcp(sock))
	return SOCK_IS_ERROR;

    sock_set_non_blocking(sock, 1);

    if (sock_resolve_addr(host, &dest, &dest_len, port, SOCK_STREAM) < 0) {
	sock_set_error(sock, errno, SOCK_CALL_GETHOSTBYNAME, __LINE__);
	sock_close(sock);
	return SOCK_IS_ERROR;
    }

    if (connect(sock->fd, (struct sockaddr *)&dest, dest_len) < 0) {
	int err = sock_last_error();
#ifdef _WIN32
	/* Winsock signals a pending non-blocking connect with WSAEWOULDBLOCK */
	if (err != EINPROGRESS && err != EWOULDBLOCK) {
#else
	if (err != EINPROGRESS) {
#endif
		sock_set_error(sock, err, SOCK_CALL_CONNECT, __LINE__);
		sock_close(sock);
		return SOCK_IS_ERROR;
	}
    }
    sock_flags_add(sock, SOCK_FLAG_CONNECT);

    return SOCK_IS_OK;
}

int sock_open_udp(sock_t *sock, char *dotaddr, int port)
{
    struct sockaddr_in	addr;

    if (sock_init(sock) || sock_startup())
	return SOCK_IS_ERROR;

    if ((sock->fd = (int)socket(AF_INET, SOCK_DGRAM, 0)) < 0)
	return sock_set_error(sock, sock_last_error(), SOCK_CALL_SOCKET, __LINE__);

    sock_flags_add(sock, SOCK_FLAG_UDP);

#ifdef _WIN32
    {
	/*
	 * An ICMP "port unreachable" for an earlier datagram makes Winsock
	 * fail the next recvfrom() with WSAECONNRESET, which would read as a
	 * broken connection.  Unix only reports this on connected sockets.
	 */
	BOOL report = FALSE;
	DWORD bytes = 0;

	WSAIoctl((SOCKET)sock->fd, SIO_UDP_CONNRESET, &report, sizeof(report),
		 NULL, 0, &bytes, NULL, NULL);
    }
#endif

    memset(&addr, 0, sizeof(addr));
    addr.sin_family	 = AF_INET;
    addr.sin_port	 = htons((unsigned short)port);
    addr.sin_addr.s_addr = (dotaddr) ? inet_addr(dotaddr) : INADDR_ANY;
    if (bind(sock->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_BIND, __LINE__);
	sock_close(sock);
	return SOCK_IS_ERROR;
    }

    return SOCK_IS_OK;
}

int sock_connect(sock_t *sock, char *host, int port)
{
    struct sockaddr_storage	dest;
    socklen_t			dest_len = sizeof(dest);

    if (sock_resolve_addr(host, &dest, &dest_len, port, SOCK_STREAM) < 0) {
	return sock_set_error(sock, errno, SOCK_CALL_GETHOSTBYNAME, __LINE__);
    }

    if (connect(sock->fd, (struct sockaddr *)&dest, dest_len) < 0)
	return sock_set_error(sock, sock_last_error(), SOCK_CALL_CONNECT, __LINE__);

    sock_flags_add(sock, SOCK_FLAG_CONNECT);

    return SOCK_IS_OK;
}

int sock_get_last_port(sock_t *sock)
{
    if (sock->lastaddr) {
	struct sockaddr_storage *ss = (struct sockaddr_storage *)(sock->lastaddr);
	if (ss->ss_family == AF_INET6)
	    return ntohs(((struct sockaddr_in6 *)ss)->sin6_port);
	else
	    return ntohs(((struct sockaddr_in *)ss)->sin_port);
    }

    sock_set_error(sock, EINVAL, SOCK_CALL_ANY, __LINE__);

    return 0;
}

char * sock_get_last_addr(sock_t *sock)
{
    static char			addr_str[INET6_ADDRSTRLEN];

    if (sock->lastaddr) {
	struct sockaddr_storage *ss = (struct sockaddr_storage *)(sock->lastaddr);
	if (ss->ss_family == AF_INET6)
	    inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ss)->sin6_addr,
		      addr_str, sizeof(addr_str));
	else
	    inet_ntop(AF_INET, &((struct sockaddr_in *)ss)->sin_addr,
		      addr_str, sizeof(addr_str));

	if (sock_alloc_hostname(sock))
	    return addr_str;
	strlcpy(sock->hostname, addr_str, SOCK_HOSTNAME_LENGTH);
	return sock->hostname;
    }

    sock_set_error(sock, EINVAL, SOCK_CALL_ANY, __LINE__);

    return "255.255.255.255";
}

char * sock_get_last_name(sock_t *sock)
{
    static char			host_buf[NI_MAXHOST];

    if (sock->lastaddr) {
	struct sockaddr *sa = (struct sockaddr *)(sock->lastaddr);
	socklen_t salen = (sa->sa_family == AF_INET6)
	    ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in);

	if (getnameinfo(sa, salen, host_buf, sizeof(host_buf), NULL, 0, NI_NAMEREQD) == 0) {
	    if (sock_alloc_hostname(sock))
		return host_buf;
	    strlcpy(sock->hostname, host_buf, SOCK_HOSTNAME_LENGTH);
	    return sock->hostname;
	}
	return sock_get_last_addr(sock);
    }

    sock_set_error(sock, EINVAL, SOCK_CALL_ANY, __LINE__);

    return "255.255.255.255";
}

int sock_read(sock_t *sock, char *buf, int len)
{
    int			count;

    count = recv(sock->fd, buf, len, 0);
    if (count < 0)
	sock_set_error(sock, sock_last_error(), SOCK_CALL_IO, __LINE__);

    return count;
}

int sock_receive_any(sock_t *sock, char *buf, int len)
{
    int			count;
    socklen_t		addrlen;

    if (sock_alloc_lastaddr(sock) == SOCK_IS_ERROR)
	return SOCK_IS_ERROR;
    addrlen = sizeof(struct sockaddr_storage);
    count = recvfrom(sock->fd, buf, len, 0,
		     (struct sockaddr *)(sock->lastaddr), &addrlen);
    if (count < 0)
	sock_set_error(sock, sock_last_error(), SOCK_CALL_IO, __LINE__);

    return count;
}

int sock_send_dest(sock_t *sock, char *host, int port, char *buf, int len)
{
    struct sockaddr_storage	dest;
    socklen_t			dest_len = sizeof(dest);
    int				count;

    if (sock_resolve_addr(host, &dest, &dest_len, port, SOCK_DGRAM) < 0) {
	return sock_set_error(sock, errno, SOCK_CALL_GETHOSTBYNAME, __LINE__);
    }

    count = sendto(sock->fd, buf, len, 0,
		   (struct sockaddr *)&dest, dest_len);
    if (count < 0)
	sock_set_error(sock, sock_last_error(), SOCK_CALL_IO, __LINE__);

    return count;
}

int sock_write(sock_t *sock, char *buf, int len)
{
    int			count;

    count = send(sock->fd, buf, len, 0);
    if (count < 0)
	sock_set_error(sock, sock_last_error(), SOCK_CALL_IO, __LINE__);

    return count;
}

char *sock_get_addr_by_name(const char *name)
{
    static char		ip_buf[INET6_ADDRSTRLEN];
    struct addrinfo	hints, *res = NULL;

    if (sock_startup())
	return NULL;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if (getaddrinfo(name, NULL, &hints, &res) != 0 || !res)
	return NULL;

    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sin->sin_addr, ip_buf, sizeof(ip_buf));
    freeaddrinfo(res);

    return ip_buf;
}

unsigned long sock_get_inet_by_addr(char *dotaddr)
{
    return inet_addr(dotaddr);
}

void sock_get_local_hostname(char *name, unsigned size,
			     int search_domain_for_xpilot)
{
    (void)search_domain_for_xpilot;
    struct addrinfo hints, *res = NULL;

    sock_startup();
    gethostname(name, size);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_flags = AI_CANONNAME;

    if (getaddrinfo(name, NULL, &hints, &res) == 0 && res) {
	if (res->ai_canonname && res->ai_canonname[0] != '\0') {
	    strlcpy(name, res->ai_canonname, size);
	}
	freeaddrinfo(res);
    }
}

int sock_get_port(sock_t *sock)
{
    struct sockaddr_in	addr;
    socklen_t		len = sizeof(addr);
    unsigned short	port;

    if (getsockname(sock->fd, (struct sockaddr *)&addr, &len) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_GETSOCKNAME, __LINE__);
	return SOCK_IS_ERROR;
    }

    port = ntohs(addr.sin_port);

    return port;
}

int sock_get_error(sock_t *sock)
{
    int			err;
    socklen_t		size = sizeof(err);

    if (getsockopt(sock->fd, SOL_SOCKET, SO_ERROR,
		   (void *)&err, &size) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_GETSOCKOPT, __LINE__);
	return SOCK_IS_ERROR;
    }
#ifdef _WIN32
    /* SO_ERROR holds a Winsock code; translate it like any other */
    WSASetLastError(err);
    if (err != 0)
	sock_last_error();
    else
	errno = 0;
#else
    errno = err;
#endif
    return SOCK_IS_OK;
}

int sock_set_broadcast(sock_t *sock, int flag)
{
    if (setsockopt(sock->fd, SOL_SOCKET, SO_BROADCAST,
		   (void *)&flag, sizeof(flag)) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_SETSOCKOPT, __LINE__);
	return SOCK_IS_ERROR;
    }
    return SOCK_IS_OK;
}

int sock_set_receive_buffer_size(sock_t *sock, int size)
{
    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF,
		   (void *)&size, sizeof(size)) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_SETSOCKOPT, __LINE__);
	return SOCK_IS_ERROR;
    }
    return SOCK_IS_OK;
}

int sock_set_send_buffer_size(sock_t *sock, int size)
{
    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF,
		   (void *)&size, sizeof(size)) < 0) {
	sock_set_error(sock, sock_last_error(), SOCK_CALL_SETSOCKOPT, __LINE__);
	return SOCK_IS_ERROR;
    }
    return SOCK_IS_OK;
}

int sock_set_timeout(sock_t *sock, int seconds, int useconds)
{
    sock->timeout.seconds = seconds;
    sock->timeout.useconds = useconds;

    return SOCK_IS_OK;
}

int sock_readable(sock_t *sock)
{
    int			n;
    fd_set		readfds;
    struct timeval	timeout;

    timerclear(&timeout); /* macro */
    timeout.tv_sec = sock->timeout.seconds;
    timeout.tv_usec = sock->timeout.useconds;

    FD_ZERO(&readfds);
    FD_SET(sock->fd, &readfds);

    n = select(sock->fd + 1, &readfds, NULL, NULL, &timeout);
    if (n == -1) {
	if (sock_last_error() != EINTR) {
	    sock_set_error(sock, errno, SOCK_CALL_SELECT, __LINE__);
	    return SOCK_IS_ERROR;
	}
	return SOCK_IS_OK;
    }

    if ((n > 0) && FD_ISSET(sock->fd, &readfds))
	return 1;

    return SOCK_IS_OK;
}


