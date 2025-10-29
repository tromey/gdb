/* Type garbage collector

   Copyright (C) 2025 Free Software Foundation, Inc.

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

#ifndef GDB_TYPE_GC_H
#define GDB_TYPE_GC_H

struct dynamic_prop_list;
struct main_type;
struct type;

// FIXME
enum gc_color
{
  PURPLE = 0,
  ORANGE = 1,
};

extern main_type *new_main_type ();

extern type *new_type ();

extern dynamic_prop_list *new_prop_list ();

extern void type_gc ();

typedef void mark_types_fn ();

extern void register_type_root (mark_types_fn *fn);

#endif /* GDB_TYPE_GC_H */
