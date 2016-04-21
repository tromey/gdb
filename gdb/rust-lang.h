/* Rust language support definitions for GDB, the GNU debugger.

   Copyright (C) 2016 Free Software Foundation, Inc.

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

#ifndef RUST_LANG_H
#define RUST_LANG_H

struct parser_state;
struct type;

extern int rust_parse (struct parser_state *);

extern void rusterror (char *);

/* Return true if TYPE is a tuple struct type; otherwise false.  */
extern int rust_tuple_struct_type_p (struct type *type);

/* Given a block, find the name of the block's crate.  The name must
   be freed by the caller.  Returns NULL if no crate name can be
   found.  */
extern char *rust_crate_for_block (const struct block *block);

#endif /* RUST_LANG_H */
