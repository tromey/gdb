/* String-map functions

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

#ifndef GDBSUPPORT_STRING_MAP_H
#define GDBSUPPORT_STRING_MAP_H

#include "gdbsupport/unordered_map.h"
#include <string_view>

namespace gdb
{

// FIXME DOCUMENT

/* Equality object for use by string maps.  */
struct string_equal
{
  using is_transparent = void;

  bool operator() (std::string_view lhs, std::string_view rhs)
    const noexcept
  {
    return lhs == rhs;
  }

  bool operator() (std::string_view lhs,
		   const gdb::unique_xmalloc_ptr<char> &rhs)
    const noexcept
  {
    return lhs == rhs.get ();
  }

  bool operator() (std::string_view lhs, const char *rhs)
    const noexcept
  {
    return lhs == rhs;
  }
};

/* Hash object for use by string maps.  */
struct string_hash
{
  using is_transparent = void;
  using is_avalanching = void;

  uint64_t operator() (std::string_view rhs)
    const noexcept
  {
    return ankerl::unordered_dense::hash<std::string_view> () (rhs);
  }

  uint64_t operator() (const char *rhs)
    const noexcept
  {
    return (*this) (std::string_view (rhs));
  }

  uint64_t operator() (const gdb::unique_xmalloc_ptr<char> &rhs)
    const noexcept
  {
    return (*this) (std::string_view (rhs.get ()));
  }
};

}

#endif /* GDBSUPPORT_STRING_MAP_H */
