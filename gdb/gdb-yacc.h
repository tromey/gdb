/* Definitions for Yacc wrappers used by GDB.

   Copyright (C) 2013 Free Software Foundation, Inc.

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

#ifndef GDB_YACC_H
#define GDB_YACC_H

/* This file should only be included from .y files.  It ensures that
   the yacc-generated code calls into gdb's malloc wrappers.  */

#undef free
#define free xfree

#undef malloc
#define malloc xmalloc

#undef realloc
#define realloc xrealloc

#endif /* GDB_YACC_H */
