/////////////////////////////////////////////////////////////////////////////////
//
// (C) Copyright 2008 LANGE GmbH
// Permission is granted to use, distribute, or modify this source,
// provided that this copyright notice remains intact.
//
// Project:	SC1000
// 		Controllerboard SC1000-Display with Hitachi SH3
//
// File: 	MonotonicTime.c
// Desc: 	Redefinitions of time() gettimeofday() glibc functions.
//
// Revision History:
// 2008-05-13	Jochen Sparbier
//
/////////////////////////////////////////////////////////////////////////////////


#include <sys/time.h>
#include <time.h>

#include "MonotonicTime.h"


// ---------------------------------------------------------------------
// time retrieval functions:
// ---------------------------------------------------------------------

/**
 *  Like  time_t time (time_t *result) function from glibc
 *  but returns a monotonic time which is not affected by setting
 *  system time.
 *   ATTENTION: This time starts with arbitrary value (0) - it is
 *              useful only for watching time differences/timeouts.
 */
time_t time_monotonic( time_t *result )
{
	struct timespec st_tms;
	clock_gettime(CLOCK_MONOTONIC, &st_tms);

	if (result)
		*result =  st_tms.tv_sec;

	return st_tms.tv_sec;
}


/**
 *  In order to use *only* clock_gettime(CLOCK_MONOTONIC, struct timespec*)
 *  the libc function  gettimeofday() is redefined accordingly.
 *   ATTENTION: struct timezone *tzp is ignored completely.
 */
int gettimeofday_monotonic( struct timeval *tp, struct timezone *tzp )
{
	struct timespec ts_now;

	clock_gettime(CLOCK_MONOTONIC, &ts_now);
	tp->tv_sec = ts_now.tv_sec;  // historically we used struct timeval - convert from struct timespec
	tp->tv_usec = ts_now.tv_nsec / 1000;

	return 0;
}


