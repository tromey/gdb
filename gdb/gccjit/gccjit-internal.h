/* Header file for GDB GCC JIT.
   Copyright (C) 2014 Free Software Foundation, Inc.

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

#ifndef GDB_GCCJIT_GCCJIT_INTERNAL_H
#define GDB_GCCJIT_GCCJIT_INTERNAL_H

#include "hashtab.h"
#include "gcc-interface.h"

struct block;

/* An object of this type holds state associated with a given
   compilation job.  */

struct gdb_gcc_instance
{
  /* The GCC front end.  */

  struct gcc_context *fe;

  /* The block in which an expression is being parsed.  */

  const struct block *block;

  /* Map from gdb types to gcc types.  */

  htab_t type_map;
};

/* Scope types enum.  List the types of scopes the compiler will
   accept.  */
enum gccjit_i_scope_types
  {
    /* A simple scope.  Just wrap an expression into an ordinary scope.  */
    GCCJIT_I_SIMPLE_SCOPE = 1
  };

/* Define header and footers for different scopes.  */
#define GCCJIT_I_SIMPLE_FUNCNAME "_gdb_expr"
#define GCCJIT_I_SIMPLE_HEADER  "void " GCCJIT_I_SIMPLE_FUNCNAME " (void) {"
#define GCCJIT_I_SIMPLE_FOOTER  "}"

/* Convert a gdb type, TYPE, to a GCC type.  CONTEXT is used to do the
   actual conversion.  The new GCC type is returned.  */

extern gcc_type convert_type (struct gdb_gcc_instance *context,
			      struct type *type);

/* A callback suitable for use as the GCC C symbol oracle.  */

extern void gcc_convert_symbol (void *datum, struct gcc_context *gcc_context,
				enum gcc_c_oracle_request request,
				const char *identifier);

/* Instantiate a GDB object holding state for the GCC context FE.  The
   expression will be compiled as it appeared in the block B.  The new
   object is returned.  */

extern struct gdb_gcc_instance *new_gdb_gcc_instance (struct gcc_context *fe,
						      const struct block *b);

/* Delete an object created by new_gdb_gcc_instance.  */

extern void delete_gdb_gcc_instance (struct gdb_gcc_instance *context);

/* Make a cleanup that calls delete_gdb_gcc_instance.  */

extern struct cleanup *make_cleanup_delete_gdb_gcc_instance
     (struct gdb_gcc_instance *context);

extern struct gcc_context *get_gcc_jit_context (void);

#endif /* GDB_GCCJIT_GCCJIT_INTERNAL_H */
