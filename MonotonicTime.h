/////////////////////////////////////////////////////////////////////////////////
//
// (C) Copyright 2008 LANGE GmbH
// Permission is granted to use, distribute, or modify this source,
// provided that this copyright notice remains intact.
//
// Project:	SC1000
// 		Controllerboard SC1000-Display with Hitachi SH3
//
// File: 	MonotonicTime.h
// Desc: 	Redefinitions of time() gettimeofday() glibc functions.
//
// Revision History:
// 2008-05-13	Jochen Sparbier
//
/////////////////////////////////////////////////////////////////////////////////

#ifndef  __MONOTONICTIME_H
#define  __MONOTONICTIME_H  1


#include <sys/time.h>
#include <time.h>

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
time_t time_monotonic( time_t *result );


/**
 *  In order to use *only* clock_gettime(CLOCK_MONOTONIC, struct timespec*)
 *  the libc function  gettimeofday() is redefined accordingly.
 *   ATTENTION: struct timezone *tzp is ignored completely.
 */
int gettimeofday_monotonic( struct timeval *tp, struct timezone *tzp );


#endif /* #ifndef  __MONOTONICTIME_H */

