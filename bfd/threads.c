/* BFD library -- threading

   Copyright (C) 2023 Free Software Foundation, Inc.

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston,
   MA 02110-1301, USA.  */

/*
SECTION

	Threading

	BFD has limited support for thread-safety.  Most BFD globals
	are protected by locks, while the error-related globals are
	thread-local.  A given BFD cannot safely be used from two
	threads at the same time; it is up to the application to do
	any needed locking.  However, it is ok for different threads
	to work on different BFD objects at the same time.

SUBSECTION

	Thread functions.
*/

#include "sysdep.h"
#include "bfd.h"
#include "libbfd.h"

#ifdef USE_PTHREADS
#include <pthread.h>
static pthread_mutex_t lock;
#elif defined (_WIN32)
#include <windows.h>
static HANDLE lock;
#endif

/*
INTERNAL_FUNCTION

	_bfd_thread_init

SYNOPSIS

	void _bfd_thread_init (void);

DESCRIPTION

	Initialize BFD thread support.
*/

void
_bfd_thread_init (void)
{
#ifdef USE_PTHREADS
  pthread_mutex_init (&lock, NULL);
#elif defined (_WIN32)
  lock = CreateMutex (NULL, FALSE, NULL);
#endif
}

/*
FUNCTION
	bfd_thread_cleanup

SYNOPSIS
	void bfd_thread_cleanup (void);

DESCRIPTION

	Clean up any thread-local state.  This should be called by a
	thread that uses any BFD functions, before the thread exits.
	It is fine to call this multiple times, or to call it and then
	later call BFD functions on the same thread again.
*/

void
bfd_thread_cleanup (void)
{
  _bfd_clear_error_data ();
}

/*
INTERNAL_FUNCTION

	bfd_lock

SYNOPSIS

	void bfd_lock (void);

DESCRIPTION

	Acquire the global BFD lock, if needed.
*/

void
bfd_lock (void)
{
#ifdef USE_PTHREADS
  pthread_mutex_lock (&lock);
#elif defined (_WIN32)
  WaitForSingleObject (lock, INFINITE);
#endif
}

/*
INTERNAL_FUNCTION

	bfd_unlock

SYNOPSIS

	void bfd_unlock (void);

DESCRIPTION

	Release the global BFD lock, if needed.
*/

void
bfd_unlock (void)
{
#ifdef USE_PTHREADS
  pthread_mutex_unlock (&lock);
#elif defined (_WIN32)
  ReleaseMutex (lock);
#endif
}
