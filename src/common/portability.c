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

/*
 * This file contains function wrappers around OS specific services.
 */

#include "xpcommon.h"

#ifdef __linux__
#include <sys/random.h>
#endif
#ifdef _WIN32
#include <bcrypt.h>
#endif


int Get_process_id(void)
{
#if defined(_WIN32)
    return _getpid();
#else
    return getpid();
#endif
}

void Get_login_name(char *buf, size_t size)
{
#if defined(_WIN32)
    const char *name = getenv("USERNAME");

    strlcpy(buf, (name && name[0]) ? name : "nameless", size);
#else
    /* Unix */
    struct passwd *p;

    setpwent();
    if ((p = getpwuid(geteuid())) != NULL)
	strlcpy(buf, p->pw_name, size);
    else
	strlcpy(buf, "nameless", size);
    endpwent();
#endif
}

int xpprintf(const char* fmt, ...)
{
    int result;
    va_list argp;
    va_start(argp, fmt);
    result = vprintf(fmt, argp);
    va_end(argp);
#ifdef _WINDOWS
    fflush(stdout);
#endif
    return result;
}

bool is_this_windows(void)
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}


/*
 * Round to nearest integer.
 */
#ifdef _WINDOWS
double rint(double x)
{
    return floor((x < 0.0) ? (x - 0.5) : (x + 0.5));
}
#endif

#ifdef NEED_GETTIMEOFDAY
int gettimeofday(struct timeval *tv, struct timezone *tz)
{
    FILETIME        ft;
    LARGE_INTEGER   li;
    __int64         t;
    static int      tzflag;

    if (tv)
    {
        GetSystemTimeAsFileTime(&ft);
        li.LowPart  = ft.dwLowDateTime;
        li.HighPart = ft.dwHighDateTime;
        t  = li.QuadPart;       /* In 100-nanosecond intervals */
        t -= EPOCHFILETIME;     /* Offset to the Epoch time */
        t /= 10;                /* In microseconds */
        tv->tv_sec  = (long)(t / 1000000);
        tv->tv_usec = (long)(t % 1000000);
    }

    if (tz)
    {
        if (!tzflag)
        {
            _tzset();
            tzflag++;
        }
        tz->tz_minuteswest = _timezone / 60;
        tz->tz_dsttime = _daylight;
    }

    return 0;

}
#endif


int Get_random_bytes(void *buf, size_t n)
{
#ifdef _WIN32
    return BCRYPT_SUCCESS(BCryptGenRandom(NULL, buf, (ULONG)n,
					  BCRYPT_USE_SYSTEM_PREFERRED_RNG))
	? 0 : -1;
#endif
#ifdef __linux__
    if (getrandom(buf, n, 0) == (ssize_t)n)
	return 0;
#endif
    {
	FILE *f = fopen("/dev/urandom", "rb");
	size_t got = 0;

	if (f != NULL) {
	    got = fread(buf, 1, n, f);
	    fclose(f);
	}
	return (got == n) ? 0 : -1;
    }
}
