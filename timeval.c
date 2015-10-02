
#include "timeval.h"

/*!
\brief A Windows gettimeofday implementation.
*/

int gettimeofday(struct timeval *tv, struct timezone *tz)
{
#if __WIN32__ || WIN32
	FILETIME        ft;
	LARGE_INTEGER   li;
	__int64         t;
	static int      tzflag;

	if (tv) {
		GetSystemTimeAsFileTime(&ft);
		li.LowPart = ft.dwLowDateTime;
		li.HighPart = ft.dwHighDateTime;
		t = li.QuadPart;       /* In 100-nanosecond intervals */
		t -= EPOCHFILETIME;     /* Offset to the Epoch time */
		t /= 10;                /* In microseconds */
		tv->tv_sec = (long)(t / 1000000);
		tv->tv_usec = (long)(t % 1000000);
	}

	if (tz) {
		if (!tzflag) {
			_tzset();
			tzflag++;
		}
		tz->tz_minuteswest = _timezone / 60;
		tz->tz_dsttime = _daylight;
	}

	return 0;
#else
	errno = ENOSYS;
	return -1;
#endif
}
