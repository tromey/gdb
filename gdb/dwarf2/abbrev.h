/* DWARF abbrev table

   Copyright (C) 1994-2023 Free Software Foundation, Inc.

   Adapted by Gary Funck (gary@intrepid.com), Intrepid Technology,
   Inc.  with support from Florida State University (under contract
   with the Ada Joint Program Office), and Silicon Graphics, Inc.
   Initial contribution by Brent Benson, Harris Computer Systems, Inc.,
   based on Fred Fish's (Cygnus Support) implementation of DWARF 1
   support.

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

#ifndef GDB_DWARF2_ABBREV_H
#define GDB_DWARF2_ABBREV_H

#include "hashtab.h"

struct attr_abbrev
{
  ENUM_BITFIELD(dwarf_attribute) name : 16;
  ENUM_BITFIELD(dwarf_form) form : 16;

  /* It is valid only if FORM is DW_FORM_implicit_const.  */
  LONGEST implicit_const;
};

/* This data structure holds the information of an abbrev.  */
struct abbrev_info
{
  /* Number identifying abbrev.  */
  unsigned int number;
  /* DWARF tag.  */
  ENUM_BITFIELD (dwarf_tag) tag : 16;
  /* True if the DIE has children.  */
  bool has_children;
  bool interesting;
  unsigned short size_if_constant;
  unsigned short sibling_offset;
  /* Number of attributes.  */
  unsigned short num_attrs;
  /* An array of attribute descriptions, allocated using the struct
     hack.  */
  struct attr_abbrev attrs[1];
};

struct abbrev_table;
typedef std::unique_ptr<struct abbrev_table> abbrev_table_up;

/* Top level data structure to contain an abbreviation table.

   In DWARF version 2, the description of the debugging information is
   stored in a separate .debug_abbrev section.  Before we read any
   dies from a section we read in all abbreviations and install them
   in a hash table.  */

struct abbrev_table
{
  /* Read an abbrev table from the indicated section, at the given
     offset.  The caller is responsible for ensuring that the section
     has already been read.  */

  static abbrev_table_up read (struct dwarf2_section_info *section,
			       sect_offset sect_off);

  /* Look up an abbrev in the table.
     Returns NULL if the abbrev is not found.  */

  const struct abbrev_info *lookup_abbrev (unsigned int abbrev_number) const
  {
    auto iter = m_abbrevs.find (abbrev_number, abbrev_number);
    if (iter == m_abbrevs.end ())
      return nullptr;
    return *iter;
  }

  /* Where the abbrev table came from.
     This is used as a sanity check when the table is used.  */
  const sect_offset sect_off;

  struct dwarf2_section_info *section;

private:

  abbrev_table (sect_offset off, struct dwarf2_section_info *sect)
    : sect_off (off),
      section (sect)
  {
  }

  DISABLE_COPY_AND_ASSIGN (abbrev_table);

  /* Add an abbreviation to the table.  */
  void add_abbrev (struct abbrev_info *abbrev)
  { m_abbrevs.insert (abbrev); }

  struct abbrev_traits
  {
    typedef abbrev_info *value_type;

    static bool is_empty (const value_type &val)
    { return val == nullptr; }

    static bool equals (const value_type &lhs, const value_type &rhs)
    {
      return lhs->number == rhs->number;
    }

    static bool equals (const value_type &lhs, unsigned int rhs)
    {
      return lhs->number == rhs;
    }

    static size_t hash (const value_type &val)
    {
      /* Warning: if you change this next line, you must also update
	 the other code in this class that computes the hash by
	 hand..  */
      return val->number;
    }
  };

  /* Hash table of abbrevs.  */
  gdb::traited_hash_table<abbrev_traits> m_abbrevs;

  /* Storage for the abbrev table.  */
  auto_obstack m_abbrev_obstack;
};

#endif /* GDB_DWARF2_ABBREV_H */
