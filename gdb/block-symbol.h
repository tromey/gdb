/* The block_symbol type

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

#ifndef GDB_BLOCK_SYMBOL_H
#define GDB_BLOCK_SYMBOL_H

struct block;
struct objfile;
struct symbol;

/* Several lookup functions return both a symbol and the block in which the
   symbol is found.  This structure is used in these cases.  */

struct block_symbol
{
  /* The symbol that was found, or NULL if no symbol was found.  */
  struct symbol *symbol;

  /* If SYMBOL is not NULL, then this is the block in which the symbol is
     defined.  */
  const struct block *block;

  /* Return the address of the symbol.  */
  CORE_ADDR address () const;

  struct objfile *objfile () const;
};

#endif /* GDB_BLOCK_SYMBOL_H */
