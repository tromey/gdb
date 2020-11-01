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

hashval_t
abbrev_cache::hash_table (const void *item)
{
  const struct abbrev_table *table = (const struct abbrev_table *) item;
  return to_underlying (table->sect_off);
}

/* Comparison function for abbrev table.  */

int
abbrev_cache::eq_table (const void *lhs, const void *rhs)
{
  const struct abbrev_table *l_table = (const struct abbrev_table *) lhs;
  const search_key *key = (const search_key *) rhs;
  return (l_table->section == key->section
	  && l_table->sect_off == key->offset);
}

/* Destroy an abbrev table.  */

static void
destroy_table (void *item)
{
  struct abbrev_table *table = (struct abbrev_table *) item;
  delete table;
}

abbrev_cache::abbrev_cache ()
  : m_tables (htab_create_alloc (20, hash_table, eq_table,
				 destroy_table, xcalloc, xfree))
{
}

void
abbrev_cache::populate (struct dwarf2_section_info *section,
			const std::unordered_set<sect_offset> &offsets)
{
  std::vector<std::pair<sect_offset, abbrev_table_up>> v_offsets;
  v_offsets.reserve (offsets.size ());
  for (sect_offset off : offsets)
    v_offsets.emplace_back (off, abbrev_table_up ());

  gdb::parallel_for_each
    (v_offsets.begin (), v_offsets.end (),
     [&] (auto start, auto end)
     {
       for (; start != end; ++start)
	 start->second = abbrev_table::read (section, start->first);
     });

  for (auto &item : v_offsets)
    {
      search_key key = { section, item.second->sect_off };

      void **slot
	= htab_find_slot_with_hash (m_tables.get (), &key,
				    to_underlying (item.second->sect_off),
				    INSERT);
      *slot = item.second.release ();
    }
}
