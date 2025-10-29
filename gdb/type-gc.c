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

#include <deque>
#include <vector>

#include "gdbtypes.h"
#include "type-gc.h"

static gc_color current_color = PURPLE;

template<typename T>
struct manager
{
  T *allocate ()
  {
    T *result = new T;
    result->color = current_color;
    storage.emplace_back (result);
    return result;
  }

  void sweep ()
  {
    auto iter = std::remove_if (storage.begin (), storage.end (),
				[] (std::unique_ptr<T> &item)
				{
				  item->mark != current_color;
				});
    storage.erase (iter, storage.end ());
  }

  std::deque<std::unique_ptr<T>> storage;
};

struct string_manager
{
  const char *intern (const char *str)
  {
  }

  gdb::array_view<const gdb_byte> intern (gdb::array_view<const gdb_byte> data)
  {
  }

  void sweep ()
  {
    auto iter = std::remove_if (storage.begin (), storage.end (),
				[] (std::unique_ptr<char> &item)
				{
				  /* The mark is stored in the first
				     byte.  */
				  *item != current_color;
				});
    storage.erase (iter, storage.end ());
  }

  std::deque<std::unique_ptr<char>> storage;
  gdb::unordered_hash<gdb::array_view<const byte>> by_contents;
};

static manager<main_type> main_type_manager;
static manager<type> type_manager;
static manager<dynamic_prop_list> prop_list_manager;
static string_manager data_manager;

static std::vector<mark_types_fn *> roots;

main_type *
new_main_type ()
{
  return main_type_manager.allocate ();
}

type *
new_type ()
{
  return type_manager.allocate ();
}

template<typename T>
bool
check_color (T *ptr)
{
  if (ptr->color == current_color)
    return true;
  ptr->color = current_color;
  return false;
}

static void
mark (dynamic_prop_list *plist)
{
  while (true)
    {
      if (plist == nullptr || check_color (plist))
	return;

      plist = plist->next;
    }
}

static void
mark (gdb::array_view<field> fields)
{
  for (field &field : fields)
    {
      // physname - string
      // or dwarf_block -> fixme

      if (field.m_type != nullptr)
	field.m_type->mark ();
      // m_name - string
    }
}

static void
mark (main_type *mt)
{
  if (mt == nullptr || check_color (mt))
    return;

  // FIXME
  // mark (mt->name);  (string)

  if (mt->m_target_type != nullptr)
    mt->m_target_type->mark ();

  switch (mt->code)
    {
    case TYPE_CODE_INT:
    case TYPE_CODE_FLT:
      if (mt->flds_bnds.complex_type != nullptr)
	mt->flds_bnds.complex_type->mark ();
      break;

    case TYPE_CODE_RANGE:
      mark (mt->flds_bnds.bounds);
      break;

    default:
      mark (mt->m_nfields, mt->flds_bnds.fields);
      break;
    }

  // fields

  switch (mt->type_specific_field)
    {
    case TYPE_SPECIFIC_NONE:
    case TYPE_SPECIFIC_FLOATFORMAT:
    case TYPE_SPECIFIC_INT:
      break;

    case TYPE_SPECIFIC_CPLUS_STUFF:
      mark (mt->type_specific.cplus_stuff);
      break;

    case TYPE_SPECIFIC_GNAT_STUFF:
      mark (mt->type_specific.gnat_stuff);
      break;

    case TYPE_SPECIFIC_FUNC:
      mark (mt->type_specific.func_stuff);
      break;

    case TYPE_SPECIFIC_SELF_TYPE:
      mt->type_specific.self_type->mark ();

    case TYPE_SPECIFIC_FIXED_POINT:
      mark (mt->type_specific.fixed_point_info);
      break;

    default:
      gdb_assert_not_reached ();
    }

  mark (mt->dyn_prop_list);
}

void
type::mark ()
{
  /* Mark the entire chain, stopping when we reach this type
     again.  */
  type *iter = this;
  do
    {
      if (!check_color (iter))
	{
	  iter->color = current_color;

	  if (iter->pointer_type != nullptr)
	    iter->pointer_type->mark ();
	  if (iter->reference_type != nullptr)
	    iter->reference_type->mark ();
	  if (iter->rvalue_reference_type != nullptr)
	    iter->rvalue_reference_type->mark ();
	}

      iter = iter->chain;
    }
  while (iter != this);

  mark (this->main_type);
}

void
type_gc ()
{
  current_color = current_color == PURPLE ? ORANGE : PURPLE;

  for (auto mark : roots)
    mark ();

  main_type_manager.sweep ();
  type_manager.sweep ();
  prop_list_manager.sweep ();
  field_manager.sweep ();
}

void
register_type_root (mark_types_fn *fn)
{
  roots.push_back (fn);
}
