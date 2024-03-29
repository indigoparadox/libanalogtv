/*
* timeval.h    1.0 01/12/19
*
* Defines gettimeofday, timeval, etc. for Win32
*
* By Wu Yongwei
*
*/

#ifndef _TIMEVAL_H
#define _TIMEVAL_H

#if TIME_WITH_SYS_TIME
#include <sys/time.h>
#include <time.h>
#elif HAVE_SYS_TIME_H
#include <sys/time.h>
#else
#include <time.h>
#endif

#if ! HAVE_GETTIMEOFDAY || MINGW

#if __WIN32__ || WIN32
#include <windows.h>
#else
#include <errno.h>
#endif

#ifndef __GNUC__
#define EPOCHFILETIME (116444736000000000i64)
#else
#define EPOCHFILETIME (116444736000000000LL)
#endif

struct timezone {
	int tz_minuteswest; /* minutes W of Greenwich */
	int tz_dsttime;     /* type of dst correction */
};

#endif
#endif