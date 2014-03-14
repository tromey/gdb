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

typedef fixme gcc_type;

struct gcc_context;

enum gcc_qualifiers
{
  GCC_QUALIFIER_CONST = 1,
  GCC_QUALIFIER_VOLATILE = 2,
  GCC_QUALIFIER_RESTRICT = 4
};

/* The C front end exports a structure of this type, full of
   callbacks.  This lets us limit the C front end interface to a
   single function, and it lets us dlopen the compiler and fail
   gracefully.  */

struct gcc_c_fe_interface
{
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

  gcc_type (*build_enum_type) (struct gcc_context *self);

  void (*build_add_enum_constant) (struct gcc_context *self,
				   gcc_type enum_type,
				   const char *name,
				   unsigned long value);

  gcc_type (*build_function_type) (struct gcc_context *self, int is_varargs);

  void (*set_function_return_type) (struct gcc_context *self,
				    gcc_type function_type,
				    gcc_type return_type);

  void (*build_add_function_argument) (struct gcc_context *self,
				       gcc_context function_type,
				       const char *arg_name,
				       gcc_type arg_type);

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
}

/* The GCC object.  */

struct gcc_context
{
  /* The virtual table.  */

  const struct gcc_c_fe_interface *ops;
};

#endif /* GDB_GCC_INTERFACE */
