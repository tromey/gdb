/* DWARF abbrev table cache

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

#ifndef GDB_DWARF2_ABBREV_CACHE_H
#define GDB_DWARF2_ABBREV_CACHE_H

#include "dwarf2/abbrev.h"
#include <vector>
#include <unordered_set>
#include "gdbtypes.h"

class abbrev_cache
{
public:
  abbrev_cache (struct dwarf2_section_info *section,
		const std::vector<sect_offset> offsets);

  abbrev_table *find (sect_offset offset)
  {
    return (abbrev_table *) htab_find_with_hash (m_tables.get (),
						 &offset,
						 to_underlying (offset));
  }

private:

  /* Hash table of abbrev tables.  */
  htab_up m_tables;
};

#endif /* GDB_DWARF2_ABBREV_CACHE_H */
