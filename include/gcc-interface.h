/* Interface between GCC C FE and GDB

   Copyright (C) 2014 Free Software Foundation, Inc.

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

#ifndef GDB_GCC_INTERFACE
#define GDB_GCC_INTERFACE

/* This header defines the interface to the GCC API.  It must be both
   valid C and valid C++, because it is included by both programs.  */

/* One bit of GCC internals leaks through here.  */

union tree_node;
typedef union tree_node *gcc_type;

struct gcc_context;

enum gcc_qualifiers
{
  GCC_QUALIFIER_CONST = 1,
  GCC_QUALIFIER_VOLATILE = 2,
  GCC_QUALIFIER_RESTRICT = 4
};

/* The operations defined by the GCC API.  This is the vtable for the
   real context structure which is passed around.  */

struct gcc_c_fe_interface
{
  /* The actual version implemented in this interface.  */

  unsigned int version;

  gcc_type (*build_pointer_type) (struct gcc_context *self,
				  gcc_type base_type);

  gcc_type (*build_record_type) (struct gcc_context *self);

  gcc_type (*build_union_type) (struct gcc_context *self);

  void (*build_add_field) (struct gcc_context *self,
			   gcc_type record_or_union_type,
			   const char *field_name,
			   gcc_type field_type,
			   unsigned long bitsize,
			   unsigned long bitpos);

  void (*finish_record_or_union) (struct gcc_context *self,
				  gcc_type record_or_union_type);

  gcc_type (*build_enum_type) (struct gcc_context *self);

  void (*build_add_enum_constant) (struct gcc_context *self,
				   gcc_type enum_type,
				   const char *name,
				   unsigned long value);

  gcc_type (*build_function_type) (struct gcc_context *self,
				   gcc_type return_type,
				   int nargs,
				   gcc_type *argument_types,
				   int is_varargs);

  gcc_type (*int_type) (struct gcc_context *self,
			int is_unsigned, unsigned long size_in_bytes);

  gcc_type (*float_type) (struct gcc_context *self,
			  unsigned long size_in_bytes);

  gcc_type (*void_type) (struct gcc_context *self);

  gcc_type (*bool_type) (struct gcc_context *self);

  gcc_type (*build_array_type) (struct gcc_context *self,
				gcc_type element_type, int num_elements);

  gcc_type (*build_qualified_type) (struct gcc_context *self,
				    gcc_type unqualified_type,
				    int /* enum gcc_qualifiers */ qualifiers);
};

/* The GCC object.  */

struct gcc_context
{
  /* The virtual table.  */

  const struct gcc_c_fe_interface *ops;
};

/* Currently only a single version is defined.  */

#define GCC_C_FE_VERSION 0

/* The compiler exports a single initialization function.  This macro
   holds its name as a symbol.  */

#define GCC_C_FE_CONTEXT gcc_c_fe_context

/* The type of the initialization function.  The caller passes in the
   desired version.  If the request can be satisfied, a compatible
   gcc_context object will be returned.  Otherwise, the function
   returns NULL.  */

typedef struct gcc_context *gcc_c_fe_context_function (unsigned int);

#endif /* GDB_GCC_INTERFACE */
