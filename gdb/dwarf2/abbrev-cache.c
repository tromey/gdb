/* DWARF 2 abbrev table cache

   Copyright (C) 2020 Free Software Foundation, Inc.

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
#include "dwarf2/read.h"
#include "dwarf2/abbrev-cache.h"
#include "gdbsupport/parallel-for.h"
#include <utility>

/* Hash function for an abbrev table.  */

static hashval_t
hash_table (const void *item)
{
  const struct abbrev_table *table = (const struct abbrev_table *) item;
  return to_underlying (table->sect_off);
}

/* Comparison function for abbrev table.  */

static int
eq_table (const void *lhs, const void *rhs)
{
  const struct abbrev_table *l_table = (const struct abbrev_table *) lhs;
  const sect_offset *off = (const sect_offset *) rhs;
  return l_table->sect_off == *off;
}

/* Destroy an abbrev table.  */

static void
destroy_table (void *item)
{
  struct abbrev_table *table = (struct abbrev_table *) item;
  delete table;
}

abbrev_cache::abbrev_cache (struct dwarf2_section_info *section,
			    const std::unordered_set<sect_offset> &offsets)
  : m_tables (htab_create_alloc (20, hash_table, eq_table,
				 destroy_table, xcalloc, xfree))
{
  std::vector<std::pair<sect_offset, abbrev_table_up>> v_offsets;
  for (sect_offset off : offsets)
    v_offsets.emplace_back (off, abbrev_table_up ());

  gdb::parallel_for_each
    (v_offsets.begin (), v_offsets.end (),
     [&] (auto start, auto end)
     {
       // crazliy wrong fixme
       for (; start != end; ++start)
	 start->second = abbrev_table::read (section, start->first);
     });

  for (auto &item : v_offsets)
    {
      void **slot
	= htab_find_slot_with_hash (m_tables.get (),
				    &item.second->sect_off,
				    to_underlying (item.second->sect_off),
				    INSERT);
      *slot = item.second.release ();
    }
}
