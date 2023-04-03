/* Self tests for the hash table.

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

#include "gdbsupport/common-defs.h"
#include "gdbsupport/selftest.h"
#include "gdbsupport/hash-table.h"

namespace selftests {

/* Traits for unsigned integers.  Note that the precise details here
   are relied upon, because some of the tests are carefully crafted to
   test details of the implementation.  */
struct unsigned_traits
{
  typedef unsigned value_type;

  /* You can't insert 0 into this hash table.  */
  static bool is_empty (const unsigned &val)
  { return val == 0; }

  static bool equals (const unsigned &v1, const unsigned &v2)
  { return v1 == v2; }

  static size_t hash (const unsigned &val)
  { return val; }
};

static void
test_hash_table ()
{
  gdb::traited_hash_table<unsigned_traits> table;

  SELF_CHECK (table.empty ());
  SELF_CHECK (table.size () == 0);

  table.insert (3);
  SELF_CHECK (!table.empty ());
  SELF_CHECK (table.size () == 1);
  SELF_CHECK (table.contains (3));
  auto iter = table.find (3);
  SELF_CHECK (iter != table.end ());
  SELF_CHECK (*iter == 3);
  SELF_CHECK (++iter == table.end ());

  /* Some of the following tests depend on this.  */
  SELF_CHECK (table.capacity () == 7);

  table.insert (4);
  /* This insertion has a hash collision with 3 and displaces the
     4.  */
  table.insert (7 + 3);
  SELF_CHECK (table.size () == 3);

  /* This test relies on Robin Hood probing and the "reverse"
     iteration to compute the expected elements.  */
  std::vector<unsigned> expected { 4, 10, 3 };
  std::vector<unsigned> actual (table.begin (), table.end ());
  SELF_CHECK (expected == actual);

  /* Deleting the 3 should move the 10, though we can't really test
     for that.  */
  table.erase (3);
  SELF_CHECK (table.size () == 2);
  expected = std::vector<unsigned> { 4, 10 };
  actual = std::vector<unsigned> (table.begin (), table.end ());
  SELF_CHECK (expected == actual);

  /* Deleting the 10 should stop iteration before moving the 4.  We
     can't test for that directly but we can make sure the 4 is still
     found -- if it moved, it can't be found.  */
  table.erase (10);
  SELF_CHECK (table.size () == 1);
  SELF_CHECK (table.contains (4));
  /* Nothing should have changed the size.  */
  SELF_CHECK (table.capacity () == 7);

  table.erase (4);
  SELF_CHECK (table.empty ());

  /* Test that wrap-around works properly.  */
  table.insert (6);
  table.insert (7);
  table.insert (13);
  expected = std::vector<unsigned> { 6, 7, 13 };
  actual = std::vector<unsigned> (table.begin (), table.end ());
  SELF_CHECK (expected == actual);

  table.erase (6);
  expected = std::vector<unsigned> { 13, 7 };
  actual = std::vector<unsigned> (table.begin (), table.end ());
  SELF_CHECK (expected == actual);

  auto insert_pair = table.insert (7);
  SELF_CHECK (*insert_pair.first == 7);
  SELF_CHECK (!insert_pair.second);
  SELF_CHECK (table.size () == 2);

  auto insert_2 = table.insert (8);
  SELF_CHECK (*insert_2.first == 8);
  SELF_CHECK (insert_2.second);

  table.clear ();
  SELF_CHECK (table.empty ());
}

} /* namespace selftests */

void _initialize_hash_table_selftests ();
void
_initialize_hash_table_selftests ()
{
  selftests::register_test ("hash-table", selftests::test_hash_table);
}
