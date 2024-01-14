/* DIE indexing 

   Copyright (C) 2024 Free Software Foundation, Inc.

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

#ifndef GDB_DWARF2_PARENT_MAP_H
#define GDB_DWARF2_PARENT_MAP_H

#include <algorithm>

class cooked_index_entry;

/* A class that handles mapping from a DIE range to a parent
   entry.  */
class parent_map
{
public:

  parent_map () = default;
  ~parent_map () = default;

  /* Move-only.  */
  DISABLE_COPY_AND_ASSIGN (parent_map);
  parent_map (parent_map &&) = default;
  parent_map &operator= (parent_map &&) = default;

  /* A reasonably opaque type that is used here to combine a section
     offset and the 'dwz' flag into a single value.  */
  enum addr_type : CORE_ADDR { };

  /* Turn a section offset into a value that can be used in a parent
     map.  */
  static addr_type form_addr (sect_offset offset, bool is_dwz)
  {
    CORE_ADDR value = to_underlying (offset);
    if (is_dwz)
      value |= ((CORE_ADDR) 1) << (8 * sizeof (CORE_ADDR) - 1);
    return addr_type (value);
  }

  size_t add_entry (addr_type start, const cooked_index_entry *parent)
  {
    /* Ensure ordering constraint.  */
    gdb_assert (m_map.empty () || start > std::get<0> (m_map.back ()));
    gdb_assert (parent != nullptr);
    m_map.emplace_back (start, addr_type (), parent);
    return m_map.size () - 1;
  }

  void set_end (size_t index, addr_type end)
  {
    gdb_assert (std::get<0> (m_map[index]) < end);
    std::get<1> (m_map[index]) = end;
  }

  bool empty () const
  {
    return m_map.empty ();
  }

  addr_type lowest () const
  {
    return std::get<0> (m_map[0]);
  }

  addr_type highest () const
  {
    return std::get<1> (m_map.back ());
  }

  const cooked_index_entry *find (addr_type search) const
  {
    auto end = m_map.cend ();
    auto iter = std::lower_bound (m_map.cbegin (), end, search,
				  [] (const one_entry &entry,
				      addr_type value)
      {
	return value < std::get<0> (entry);
      });
    if (iter == end)
      return nullptr;

    /* Search forward for the last entry that contains SEARCH.  */
    while (iter != end)
      {
	auto next_iter = iter;
	++next_iter;

	if (search < std::get<0> (*next_iter)
	    || search > std::get<1> (*next_iter))
	  break;

	iter = next_iter;
      }

    if (iter == end
	|| search < std::get<0> (*iter)
	|| search > std::get<1> (*iter))
      return nullptr;
    return std::get<2> (*iter);
  }

private:

  /* The underlying map is a vector of tuples, where each tuple is of
     the form { START, END, PARENT }.  START and END are values as
     constructed by "form_addr".  */
  using one_entry = std::tuple<addr_type, addr_type,
			       const cooked_index_entry *>;

  std::vector<one_entry> m_map;
};

/* A few places refer to vectors of parent maps, so we use a
   convenience typedef.  */
using parent_map_vector = std::vector<parent_map>;

/* Keep a collection of parent_map objects, and allow for lookups
   across all of them.  */
class parent_map_map
{
public:

  parent_map_map () = default;
  ~parent_map_map () = default;

  DISABLE_COPY_AND_ASSIGN (parent_map_map);

  void add_maps (parent_map_vector &&mapv)
  {
    for (auto &item : mapv)
      if (!item.empty ())
	m_maps.push_back (std::move (item));
  }

  void done_adding ()
  {
    std::sort (m_maps.begin (), m_maps.end (),
	       [] (const parent_map &lhs, const parent_map &rhs)
      {
	return lhs.lowest () < rhs.lowest ();
      });
  }

  const cooked_index_entry *find (parent_map::addr_type search) const
  {
    auto iter = std::lower_bound (m_maps.cbegin (), m_maps.cend (), search,
				  [] (const parent_map &entry,
				      parent_map::addr_type value)
      {
	return value > entry.highest ();
      });

    if (iter == m_maps.cend ())
      return nullptr;

    return iter->find (search);
  }

private:

  parent_map_vector m_maps;
};

#endif /* GDB_DWARF2_PARENT_MAP_H */
