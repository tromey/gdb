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

/* Define header and footers for different scopes.  */

/* A simple scope just declares a function named "_gdb_expr", takes no
   arguments and returns no value.  */

#define GCCJIT_I_SIMPLE_REGISTER_STRUCT_TAG "__gdb_regs"
#define GCCJIT_I_SIMPLE_REGISTER_ARG_NAME "__regs"
#define GCCJIT_I_SIMPLE_REGISTER_DUMMY "_dummy"

/* Call gdbarch_register_name (GDBARCH, REGNUM) and convert its result to
   a form suitable for the JIT source.  The register names should not clash
   with inferior defined macros.  Returned pointer is never NULL.
   Returned pointer needs to be deallocated by xfree.  */

extern char *gdbjit_register_name_mangled (struct gdbarch *gdbarch, int regnum);

/* Convert JIT source register name to register number of GDBARCH.
   Returned value is always >= 0, function throws an error for non-matching
   REG_NAME.  */

extern int gdbjit_register_name_demangle (struct gdbarch *gdbarch,
					  const char *reg_name);

/* Convert a gdb type, TYPE, to a GCC type.  CONTEXT is used to do the
   actual conversion.  The new GCC type is returned.  */

struct type;
extern gcc_type convert_type (struct gdb_gcc_instance *context,
			      struct type *type);

/* A callback suitable for use as the GCC C symbol oracle.  */

extern void gcc_convert_symbol (void *datum, struct gcc_context *gcc_context,
				enum gcc_c_oracle_request request,
				const char *identifier);

/* A callback suitable for use as the GCC C address oracle.  */

extern gcc_c_symbol_address_function gcc_symbol_address;

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

extern unsigned char *generate_c_for_variable_locations
     (struct ui_file *stream,
      struct gdbarch *gdbarch,
      const struct block *block,
      CORE_ADDR pc);

/* Get the GCC mode attribute value for a given type size.  */

extern const char *c_get_mode_for_size (int size);

#endif /* GDB_GCCJIT_GCCJIT_INTERNAL_H */
