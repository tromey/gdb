/* DWARF abbrev table cache

   Copyright (C) 2022-2023 Free Software Foundation, Inc.

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

#ifndef GDB_DWARF2_ABBREV_CACHE_H
#define GDB_DWARF2_ABBREV_CACHE_H

#include "dwarf2/abbrev.h"
#include "gdbsupport/hash-table.h"

/* An abbrev cache holds abbrev tables for easier reuse.  */
class abbrev_cache
{
public:
  abbrev_cache () = default;
  DISABLE_COPY_AND_ASSIGN (abbrev_cache);

  /* Find an abbrev table coming from the abbrev section SECTION at
     offset OFFSET.  Return the table, or nullptr if it has not yet
     been registered.  */
  abbrev_table *find (struct dwarf2_section_info *section, sect_offset offset)
  {
    search_key key = { section, offset };

    auto iter = m_tables.find (key, to_underlying (offset));
    if (iter == m_tables.end ())
      return nullptr;
    return iter->get ();
  }

  /* Add TABLE to this cache.  Ownership of TABLE is transferred to
     the cache.  Note that a table at a given section+offset may only
     be registered once -- a violation of this will cause an assert.
     To avoid this, call the 'find' method first, to see if the table
     has already been read.  */
  void add (abbrev_table_up table);

private:

  struct search_key
  {
    struct dwarf2_section_info *section;
    sect_offset offset;
  };

  struct abbrev_traits
  {
    typedef abbrev_table_up value_type;

    static bool is_empty (const value_type &val)
    { return val == nullptr; }

    static bool equals (const value_type &lhs, const value_type &rhs)
    {
      return lhs->section == rhs->section && lhs->sect_off == rhs->sect_off;
    }

    static bool equals (const value_type &lhs, const search_key &rhs)
    {
      return lhs->section == rhs.section && lhs->sect_off == rhs.offset;
    }

    static size_t hash (const value_type &val)
    {
      return to_underlying (val->sect_off);
    }
  };

  /* Hash table of abbrev tables.  */
  gdb::traited_hash_table<abbrev_traits> m_tables;
};

#endif /* GDB_DWARF2_ABBREV_CACHE_H */
