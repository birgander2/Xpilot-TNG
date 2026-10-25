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

#ifndef XPCOMMON_H
#define XPCOMMON_H

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#ifdef _WINDOWS
# define HAVE_ASSERT_H 1
# define HAVE_CTYPE_H 1
# define HAVE_ERRNO_H 1
# define HAVE_MATH_H 1
# define HAVE_FLOAT_H 1
# define HAVE_SIGNAL_H 1
# define HAVE_STDARG_H 1
# define HAVE_LIMITS_H 1
# define HAVE_SETJMP_H 1
# define HAVE_STDINT_H 1
# define STDC_HEADERS 1
# define HAVE_STRING_H 1
# define HAVE_FCNTL_H 1
# define HAVE_SYS_STAT_H 1
# define HAVE_SYS_TYPES_H 1
# define HAVE_STRCASECMP 1
# define HAVE_STRNCASECMP 1
# define HAVE_LIBZ 1
#endif

#include <stdio.h>
#ifdef HAVE_STDARG_H
# include <stdarg.h>
#else
# error "ANSI C stdarg.h is needed to compile."
#endif

#ifdef HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif

#ifdef HAVE_SYS_STAT_H
# include <sys/stat.h>
#endif

#ifdef STDC_HEADERS
# include <stdlib.h>
# include <stddef.h>
#else
# ifdef HAVE_STDLIB_H
#  include <stdlib.h>
# endif
#endif

#ifdef HAVE_STRING_H
# if ! defined STDC_HEADERS && defined HAVE_MEMORY_H
#  include <memory.h>
# endif
# include <string.h>
#endif

#ifdef HAVE_STRINGS_H
# include <strings.h>
#endif

#ifdef HAVE_INTTYPES_H
# include <inttypes.h>
#else
# ifdef HAVE_STDINT_H
#  include <stdint.h>
# endif
#endif

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_ASSERT_H
# include <assert.h>
#endif

#ifdef HAVE_CTYPE_H
# include <ctype.h>
#endif

#ifdef HAVE_ERRNO_H
# include <errno.h>
#endif

#ifdef HAVE_MATH_H
# include <math.h>
#endif

#ifdef HAVE_SIGNAL_H
# include <signal.h>
#endif

#ifdef TIME_WITH_SYS_TIME
# include <sys/time.h>
# include <time.h>
#else
# ifdef HAVE_SYS_TIME_H
#  include <sys/time.h>
# else
#  include <time.h>
# endif
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif

#ifdef HAVE_FLOAT_H
# include <float.h>
#endif

#ifdef HAVE_LIMITS_H
# include <limits.h>
#endif

#ifdef HAVE_PWD_H
# include <pwd.h>
#endif

#ifdef HAVE_SETJMP_H
# include <setjmp.h>
#endif

#ifdef HAVE_SYS_FILE_H
# include <sys/file.h>
#endif

#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif

#ifdef HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#ifdef HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif

#ifdef HAVE_SYS_INTTYPES_H
# include <sys/inttypes.h>
#endif

#ifdef HAVE_NETINET_IN_H
# include <netinet/in.h>
#endif

#ifdef HAVE_ARPA_INET_H
# include <arpa/inet.h>
#endif

#ifdef HAVE_NET_IF_H
# include <net/if.h>
#endif

#ifdef HAVE_NETINET_TCP_H
# include <netinet/tcp.h>
#endif

#ifdef HAVE_NETDB_H
# include <netdb.h>
#endif

/* Platform specific hacks. */

/* SGI hack. */
#ifdef HAVE_BSTRING_H
# include <bstring.h>
#endif

/* System V R4 hacks. */
#ifdef HAVE_SYS_FILIO_H
# include <sys/filio.h>
#endif

#ifdef HAVE_SYS_SOCKIO_H
# include <sys/sockio.h>
#endif

/* Sequent hack. */
#ifdef HAVE_SYS_FCNTL_H
# include <sys/fcntl.h>
#endif

/* Sun hacks. */
#ifdef HAVE_ARPA_NAMESER_H
# include <arpa/nameser.h>
#endif

#ifdef HAVE_RESOLV_H
# include <resolv.h>
#endif

#ifdef HAVE_VALUES_H
# include <values.h>
#endif

/*
 * Windows (MinGW-w64): Winsock instead of BSD sockets.  Socket handles
 * are kept in plain ints like on Unix; closing and error reporting go
 * through socklib (sock_close_fd(), sock_last_error()).
 */
#ifdef _WIN32
  /* rpcndr.h has its own unsigned "byte"; keep ours (types.h) signed */
# define byte win32_byte
# include <winsock2.h>
# include <ws2tcpip.h>
# include <mswsock.h>
# include <iphlpapi.h>
# include <timeapi.h>
# undef byte
  /* minwindef.h leaves these 16-bit relics as empty macros */
# undef near
# undef far
# undef NEAR
# undef FAR
# define NEAR
# define FAR
# include <io.h>
# include <direct.h>
# include <process.h>
# define mkdir(path, mode)	_mkdir(path)
# define realpath(name, resolved) _fullpath((resolved), (name), PATH_MAX)
# define popen			_popen
# define pclose			_pclose
/* POSIX rename() replaces an existing target, the CRT's fails instead */
static inline int xp_rename(const char *from, const char *to)
{
    return MoveFileExA(from, to, MOVEFILE_REPLACE_EXISTING) ? 0 : -1;
}
# define rename(from, to)	xp_rename((from), (to))
#endif

/* Common XPilot header files. */

#include "version.h"
#include "xpconfig.h"
#include "arraylist.h"
#include "astershape.h"
#include "bit.h"
#include "checknames.h"
#include "click.h"
#include "commonproto.h"
#include "const.h"
#include "error.h"
#include "item.h"
#include "list.h"
#include "metaserver.h"
#include "net.h"
#include "pack.h"
#include "packet.h"
#include "portability.h"
#include "rules.h"
#include "setup.h"
#include "shipshape.h"
#include "socklib.h"
#include "types.h"
#include "voice.h"
#include "wreckshape.h"
#include "xpmap.h"

#ifdef	SOUND
# include "audio.h"
#endif

static inline double timeval_to_seconds(struct timeval *tvp)
{
    return (double)tvp->tv_sec + tvp->tv_usec * 1e-6;
}

static inline struct timeval seconds_to_timeval(double t)
{
    struct timeval tv;

    tv.tv_sec = (unsigned)t;
    tv.tv_usec = (unsigned)(((t - (double)tv.tv_sec) * 1e6) + 0.5);

    return tv;
}

/* returns 'tv2 - tv1' */
static inline int timeval_sub(struct timeval *tv2,
			      struct timeval *tv1)
{
    int s, us;

    s = tv2->tv_sec - tv1->tv_sec;
    us = tv2->tv_usec - tv1->tv_usec;

    return 1000000 * s + us;
}

#endif /* XPCOMMON_H */
