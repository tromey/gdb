/* Run gdb, but first check if libpython is available.

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

#include <dlfcn.h>
#include <stdio.h>
#include <unistd.h>

#define LIBRARY "libpython2.7.so"

int
main (int argc, char **argv)
{
  void *handle = dlopen (LIBRARY, RTLD_LAZY);
  if (handle == NULL)
    {
      fprintf (stderr, "*** Error: " LIBRARY
	       " is required for gdb to function.\n");
      fprintf (stderr, "    Please install it and try again.\n");
      return 1;
    }

  /* The rust-gdb wrapper script passes the path to real-gdb as the
     first argument.  This avoids having to modify PATH in
     rust-gdb.  */
  ++argv;
  execvp (argv[0], argv);

  fprintf (stderr, "*** Error: could not exec %s.\n", argv[0]);
  return 1;
}
