/* Thread-related for GDB.

   Copyright (C) 2018 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef COMMON_HOST_THREAD_H
#define COMMON_HOST_THREAD_H

/* Called once, by the main thread, to record which thread is the main
   thread.  */

extern void this_is_the_main_thread ();

/* Return true if this is the main thread, false otherwise.  */

extern bool main_thread_p ();

#endif /* COMMON_HOST_THREAD_H */
