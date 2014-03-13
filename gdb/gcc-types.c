/* Convert types from GDB to GCC

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


#include "defs.h"
#include "gdbtypes.h"
#include "gcc-interface.h"

struct gdb_gcc_instance
{
  /* The GCC front end.  */

  struct gcc_context *fe;

  /* Map from gdb types to gcc types.  */

  htab_t type_map;
};



struct type_map_instance
{
  struct type *type;
  gcc_type gcc_type;
}

static hashval_t
hash_type_map_instance (const void *p)
{
  const struct type_map_instance *inst = p;

  return htab_hash_pointer (inst->type);
}

static int
eq_type_map_instance (const void *a, const void *b)
{
  const struct type_map_instance *insta = a;
  const struct type_map_instance *instb = b;

  return a->type == b->type;
}



static void
insert_type (struct gdb_gcc_instance *context, struct type *type,
	     gcc_type gcc_type)
{
  struct type_map_instance inst, *add;
  void **slot;

  inst->type = type;
  inst->gcc_type = gcc_type;
  slot = htab_find_slot (context->type_map, inst, INSERT);

  add = *slot;
  /* The type might have already been inserted in order to handle
     recursive types.  */
  gdb_assert (add == NULL || add->gcc_type == gcc_type);

  if (add == NULL)
    {
      add = XNEW (struct type_map_instance);
      *add = inst;
      *slot = add;
    }
}

static gcc_type
convert_pointer (struct gdb_gcc_instance *context, struct type *type)
{
  gcc_type target = convert_type (context, TYPE_TARGET_TYPE (type));

  return context->fe->ops->build_pointer_type (context->fe, target);
}

static gcc_type
convert_array (struct gdb_gcc_instance *context, struct type *type)
{
  gcc_type element_type;
  LONGEST low_bound, high_bound;

  element_type = convert_array (context, TYPE_TARGET_TYPE (type));

  if (get_array_bounds (type, &low_bound, &high_bound) == 0)
    high_bound = -1;

  /* Doesn't handle VLA yet.  */
  return context->fe->ops->build_array_type (context->fe, high_bound);
}

static gcc_type
convert_struct_or_union (struct gdb_gcc_instance *context, struct type *type)
{
  int i;
  gcc_type result;

  /* First we create the resulting type and enter it into our hash
     table.  This lets recursive types work.  */
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    result = context->fe->ops->build_record_type (context->fe,
						  TYPE_TAG_NAME (type));
  else
    {
      gdb_assert (TYPE_CODE (type) == TYPE_CODE_UNION);
      result = context->fe->ops->build_union_type (context->fe,
						   TYPE_TAG_NAME (type));
    }
  insert_type (context, type, result);

  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      gcc_type field_type;

      field_type = convert_type (context, TYPE_FIELD_TYPE (type, i));
      context->fe->ops->build_add_field (context->fe, result,
					 TYPE_FIELD_NAME (type, i), field_type,
					 TYPE_FIELD_BITSIZE (type, i),
					 TYPE_FIELD_BITPOS (type, i));
    }

  return result;
}

static gcc_type
convert_enum (struct gdb_gcc_instance *context, struct type *type)
{
  gcc_type result;
  int i;

  result = context->fe->ops->build_enum_type (context->fe,
					      TYPE_TAG_NAME (type));
  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      context->fe->ops->build_add_enum_constant (context->fe,
						 result,
						 TYPE_FIELD_NAME (type, i),
						 TYPE_FIELD_ENUMVAL (type, i));
    }

  return result;
}

static gcc_type
convert_func (struct gdb_gcc_instance *context, struct type *type)
{
  int i;
  gcc_type result, return_type;

  /* First we create the resulting type and enter it into our hash
     table.  This lets recursive types work -- probably can't happen
     in C but it doesn't hurt to be defensive.  */
  result = context->fe->ops->build_function_type (context->fe,
						  TYPE_VARARGS (type));
  insert_type (context, type, result);

  return_type = convert_type (context, TYPE_TARGET_TYPE (type));
  context->fe->ops->set_function_return_type (context->fe, result, return_type);

  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      gcc_type arg_type;

      arg_type = convert_type (context, TYPE_FIELD_TYPE (type, i));
      context->fe->ops->build_add_function_argument (context->fe, result,
						     TYPE_FIELD_NAME (type, i),
						     arg_type);
    }

  return result;
}

static gcc_type
convert_int (struct gdb_gcc_instance *context, struct type *type)
{
  return context->fe->ops->int_type (context->fe, TYPE_NAME (type),
				     TYPE_UNSIGNED (type), TYPE_LENGTH (type));
}

static gcc_type
convert_float (struct gdb_gcc_instance *context, struct type *type)
{
  return context->fe->ops->float_type (context->fe,
				       TYPE_NAME (type), TYPE_LENGTH (type));
}

static gcc_type
convert_void (struct gdb_gcc_instance *context, struct type *type)
{
  return context->fe->ops->void_type (context->fe);
}

static gcc_type
convert_bool (struct gdb_gcc_instance *context, struct type *type)
{
  return context->fe->ops->bool_type (context->fe);
}

static gcc_type
convert_typedef (struct gdb_gcc_instance *context, struct type *type)
{
  gcc_type real = convert_type (context, check_typedef (type));

  return context->fe->ops->build_typedef (context->fe, TYPE_NAME (type), real);
}

static gcc_type
convert_qualified (struct gdb_gcc_instance *context, struct type *type)
{
  struct type *unqual = make_unqualified_type (type);
  gcc_type unqual_converted;
  int quals = 0;

  unqual_converted = convert_qualified (context, unqual);

  if (TYPE_CONST (type))
    quals |= GCC_QUALIFIER_CONST;
  if (TYPE_VOLATILE (type))
    quals |= GCC_QUALIFIER_VOLATILE;
  if (TYPE_RESTRICT (type))
    quals |= GCC_QUALIFIER_RESTRICT;

  return context->fe->ops->build_qualified_type (context->fe, unqual_converted,
						 quals);
}

static gcc_type
convert_type_basic (struct gdb_gcc_instance *context, struct type *type)
{
  /* If we are converting a typedef, just let it go through even if it
     is just a stub -- this will be handled during conversion.
     Otherwise, resolve stubs now so that most of the code doesn't
     need to use check_typedef.  */
  if (TYPE_STUB (type) && TYPE_CODE (type) != TYPE_CODE_TYPEDEF)
    type = check_typedef (type);

  /* If we are converting a qualified type, first convert the
     unqualified type and then apply the qualifiers.  */
  if ((TYPE_INSTANCE_FLAGS (type) & (TYPE_INSTANCE_FLAG_CONST
				     | TYPE_INSTANCE_FLAG_VOLATILE
				     | TYPE_INSTANCE_FLAG_RESTRICT)) != 0)
    return convert_qualified (context, type);

  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_PTR:
      return convert_pointer (context, type);

    case TYPE_CODE_ARRAY:
      return convert_array (context, type);

    case TYPE_CODE_STRUCT:
    case TYPE_CODE_UNION:
      return convert_struct_or_union (context, type);

    case TYPE_CODE_ENUM:
      return convert_enum (context, type);

    case TYPE_CODE_FUNC:
      return convert_func (context, type);

    case TYPE_CODE_INT:
      return convert_int (context, type);

    case TYPE_CODE_FLT:
      return convert_float (context, type);

    case TYPE_CODE_VOID:
      return convert_void (context, type);

    case TYPE_CODE_BOOL:
      return convert_bool (context, type);

    case TYPE_CODE_TYPEDEF:
      return convert_typedef (context, type);
    }

  error (_("cannot convert gdb type to gcc type"));
}

gcc_type
convert_type (struct gdb_gcc_instance *context, struct type *type)
{
  struct type_map_instance inst, *found;
  gcc_type result;

  inst.type = type;
  found = htab_find (context->type_map, &inst);
  if (found != NULL)
    return found->gcc_type;

  result = convert_type_basic (context, type);
  insert_type (context, type, result);
  return result;
}



struct gdb_gcc_instance *
new_gdb_gcc_instance (struct gcc_context *fe)
{
  struct gdb_gcc_instance *result = XNEW (struct gdb_gcc_instance);

  result->fe = fe;
  result->type_map = htab_create_alloc (10, hash_type_map_instance,
					eq_type_map_instance,
					xfree, xcalloc, xfree);
  return result;
}

void
delete_gdb_gcc_instance (struct gdb_gcc_instance *context)
{
  htab_delete (context->type_map);
  xfree (context);
}
