/* The block_symbol type

   Copyright (C) 2023, 2025 Free Software Foundation, Inc.

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

#ifndef GDB_BLOCK_SYMBOL_H
#define GDB_BLOCK_SYMBOL_H

#include "gdbsupport/unordered_dense.h"

struct block;
struct objfile;
struct symbol;

/* Several lookup functions return both a symbol and the block in which the
   symbol is found.  This structure is used in these cases.  */

struct block_symbol_for_parsers
{
  /* The symbol that was found, or NULL if no symbol was found.  */
  struct symbol *symbol;

  /* If SYMBOL is not NULL, then this is the block in which the symbol is
     defined.  */
  const struct block *block;

  /* The objfile.  If SYMBOL is non-NULL, then this must be as well.  */
  struct objfile *objfile;
};

struct block_symbol : block_symbol_for_parsers
{
  block_symbol (struct symbol *symbol, const struct block *block,
		struct objfile *objfile = nullptr)
  {
    this->symbol = symbol;
    this->block = block;
    this->objfile = objfile;
  }

  // fixme doc
  block_symbol (const block_symbol_for_parsers &bsp)
    : block_symbol (bsp.symbol, bsp.block, bsp.objfile)
  {
  }

  block_symbol () : block_symbol (nullptr, nullptr, nullptr)
  {
  }

  bool operator== (const block_symbol &other) const
  {
    /* We don't really need to compare the block, but it also doesn't
       hurt.  */
    /* FIXME: until the objfile is reliable, it shouldn't be
       compared.  */
    return symbol == other.symbol && block == other.block;
  }

  /* Return the address of the symbol.  */
  CORE_ADDR address () const;

  bool has_value () const
  {
    return symbol != nullptr;
  }
};

template <>
struct ankerl::unordered_dense::hash<block_symbol>
{
  using is_avalanching = void;

  uint64_t operator() (const block_symbol &sym) const noexcept
  {
    static_assert(std::has_unique_object_representations_v<block_symbol>);
    return ankerl::unordered_dense::detail::wyhash::hash (&sym, sizeof (sym));
  }
};

#endif /* GDB_BLOCK_SYMBOL_H */
