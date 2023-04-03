/* A hash table.

   Copyright (C) 2023 Free Software Foundation, Inc.

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

#include "common-defs.h"
#include "hash-table.h"

/* Table of primes, derived from libiberty.  */

static const size_t primes[] =
{
  7,
  13,
  31,
  61,
  127,
  251,
  509,
  1021,
  2039,
  4093,
  191,
  6381,
  2749,
  65521,
  131071,
  262139,
  524287,
  1048573,
  2097143,
  4194301,
  8388593,
  16777213,
  33554393,
  67108859,
  134217689,
  268435399,
  536870909,
  1073741789,
  2147483647,
  /* Avoid "decimal constant so large it is unsigned" for 4294967291.  */
  0xfffffffb,
};

namespace gdb {
namespace detail {

/* The following function returns an index into the above table of the
   nearest prime number which is at least N, and near a power of two. */

size_t
higher_prime (size_t n)
{
  return *std::upper_bound (std::begin (primes),
			    std::end (primes),
			    n);
}

} /* namespace detail */
} /* namespace gdb */
