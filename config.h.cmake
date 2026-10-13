#ifndef CONFIG_H
#define CONFIG_H

#define PACKAGE "xpilot-tng"
#define PACKAGE_NAME "xpilot-tng"
#define PACKAGE_TARNAME "xpilot-tng"
#define PACKAGE_VERSION "@PROJECT_VERSION@"
#define PACKAGE_STRING "XPilot TNG @PROJECT_VERSION@"
#define PACKAGE_BUGREPORT "https://github.com/xpilot"
#define VERSION "@PROJECT_VERSION@"

#cmakedefine HAVE_CONFIG_H 1
#cmakedefine _GNU_SOURCE 1

#define HAVE_LIBZ 1
#define HAVE_LIBEXPAT 1

#cmakedefine HAVE_UNISTD_H 1
#cmakedefine HAVE_SYS_TIME_H 1
#cmakedefine HAVE_SYS_TYPES_H 1
#cmakedefine HAVE_SYS_STAT_H 1
#cmakedefine HAVE_SYS_SOCKET_H 1
#cmakedefine HAVE_SYS_SELECT_H 1
#cmakedefine HAVE_SYS_IOCTL_H 1
#cmakedefine HAVE_SYS_PARAM_H 1
#cmakedefine HAVE_SYS_FILE_H 1
#cmakedefine HAVE_SYS_MMAN_H 1
#cmakedefine HAVE_NET_IF_H 1
#cmakedefine HAVE_NETINET_IN_H 1
#cmakedefine HAVE_NETINET_TCP_H 1
#cmakedefine HAVE_ARPA_INET_H 1
#cmakedefine HAVE_NETDB_H 1
#cmakedefine HAVE_STRING_H 1
#cmakedefine HAVE_STRINGS_H 1
#cmakedefine HAVE_STDLIB_H 1
#cmakedefine HAVE_STDINT_H 1
#cmakedefine HAVE_STDDEF_H 1
#cmakedefine HAVE_STDARG_H 1
#cmakedefine HAVE_FCNTL_H 1
#cmakedefine HAVE_SIGNAL_H 1
#cmakedefine HAVE_MATH_H 1
#cmakedefine HAVE_ERRNO_H 1
#cmakedefine HAVE_CTYPE_H 1
#cmakedefine HAVE_ASSERT_H 1
#cmakedefine HAVE_LIMITS_H 1
#cmakedefine HAVE_TIME_H 1
#cmakedefine HAVE_PWD_H 1
#cmakedefine HAVE_SETJMP_H 1

#cmakedefine HAVE_SELECT 1
#cmakedefine HAVE_SOCKET 1
#cmakedefine HAVE_STRCASECMP 1
#cmakedefine HAVE_STRNCASECMP 1
#cmakedefine HAVE_STRDUP 1
#cmakedefine HAVE_STRERROR 1
#cmakedefine HAVE_STRLCPY 1
#cmakedefine HAVE_STRLCAT 1
#cmakedefine HAVE_MEMMOVE 1
#cmakedefine HAVE_MEMSET 1
#cmakedefine HAVE_GETTIMEOFDAY 1
#cmakedefine HAVE_ALARM 1
#cmakedefine HAVE_POW 1
#cmakedefine HAVE_SQRT 1
#cmakedefine HAVE_RINT 1
#cmakedefine HAVE_FLOOR 1

#define SELECT_TYPE_ARG1 int
#define SELECT_TYPE_ARG234 fd_set *
#define SELECT_TYPE_ARG5 struct timeval *
#define RETSIGTYPE void
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1

#cmakedefine SELECT_SCHED 1
#cmakedefine PLOCKSERVER 1
#define SOUND 1

#endif /* CONFIG_H */
