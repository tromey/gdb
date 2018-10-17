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

#include "defs.h"
#include "gdb_thread.h"

/* True for the main thread, false otherwise.  */

static thread_local bool main_thread;

/* See gdb_thread.h.  */

void
this_is_the_main_thread ()
{
  main_thread = true;
}

/* See gdb_thread.h.  */

bool
main_thread_p ()
{
  return main_thread;
}
