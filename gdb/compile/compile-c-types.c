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
#include "compile-internal.h"
#include "gdb_assert.h"

struct type_map_instance
{
  struct type *type;
  gcc_type gcc_type;
};

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

  return insta->type == instb->type;
}



static void
insert_type (struct compile_c_instance *context, struct type *type,
	     gcc_type gcc_type)
{
  struct type_map_instance inst, *add;
  void **slot;

  inst.type = type;
  inst.gcc_type = gcc_type;
  slot = htab_find_slot (context->type_map, &inst, INSERT);

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
convert_pointer (struct compile_c_instance *context, struct type *type)
{
  gcc_type target = convert_type (context, TYPE_TARGET_TYPE (type));

  return C_CTX (context)->c_ops->build_pointer_type (C_CTX (context),
						     target);
}

static gcc_type
convert_array (struct compile_c_instance *context, struct type *type)
{
  gcc_type element_type;
  LONGEST low_bound, high_bound, count;

  element_type = convert_type (context, TYPE_TARGET_TYPE (type));

  if (get_array_bounds (type, &low_bound, &high_bound) == 0)
    count = -1;
  else if (low_bound != 0)
    return C_CTX (context)->c_ops->error (C_CTX (context),
					  _("cannot convert array type with "
					    "non-zero lower bound to C"));
  else
    count = high_bound + 1;

  /* Doesn't handle VLA yet.  */
  if (TYPE_VECTOR (type))
    return C_CTX (context)->c_ops->build_vector_type (C_CTX (context),
						      element_type,
						      count);
  return C_CTX (context)->c_ops->build_array_type (C_CTX (context),
						   element_type, count);
}

static gcc_type
convert_struct_or_union (struct compile_c_instance *context, struct type *type)
{
  int i;
  gcc_type result;

  /* First we create the resulting type and enter it into our hash
     table.  This lets recursive types work.  */
  if (TYPE_CODE (type) == TYPE_CODE_STRUCT)
    result = C_CTX (context)->c_ops->build_record_type (C_CTX (context));
  else
    {
      gdb_assert (TYPE_CODE (type) == TYPE_CODE_UNION);
      result = C_CTX (context)->c_ops->build_union_type (C_CTX (context));
    }
  insert_type (context, type, result);

  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      gcc_type field_type;
      unsigned long bitsize = TYPE_FIELD_BITSIZE (type, i);

      field_type = convert_type (context, TYPE_FIELD_TYPE (type, i));
      if (bitsize == 0)
	bitsize = 8 * TYPE_LENGTH (TYPE_FIELD_TYPE (type, i));
      C_CTX (context)->c_ops->build_add_field (C_CTX (context), result,
					       TYPE_FIELD_NAME (type, i),
					       field_type,
					       bitsize,
					       TYPE_FIELD_BITPOS (type, i));
    }

  C_CTX (context)->c_ops->finish_record_or_union (C_CTX (context), result,
						  TYPE_LENGTH (type));
  return result;
}

static gcc_type
convert_enum (struct compile_c_instance *context, struct type *type)
{
  gcc_type int_type, result;
  int i;
  struct gcc_c_context *ctx = C_CTX (context);

  int_type = ctx->c_ops->int_type (ctx,
				   TYPE_UNSIGNED (type),
				   TYPE_LENGTH (type));

  result = ctx->c_ops->build_enum_type (ctx, int_type);
  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      ctx->c_ops->build_add_enum_constant (ctx,
					   result,
					   TYPE_FIELD_NAME (type, i),
					   TYPE_FIELD_ENUMVAL (type, i));
    }

  ctx->c_ops->finish_enum_type (ctx, result);

  return result;
}

static gcc_type
convert_func (struct compile_c_instance *context, struct type *type)
{
  int i;
  gcc_type result, return_type;
  struct gcc_type_array array;

  /* This approach means we can't make self-referential function
     types.  Those are impossible in C, though.  */
  return_type = convert_type (context, TYPE_TARGET_TYPE (type));

  array.n_elements = TYPE_NFIELDS (type);
  array.elements = XNEWVEC (gcc_type, TYPE_NFIELDS (type));
  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    array.elements[i] = convert_type (context, TYPE_FIELD_TYPE (type, i));

  result = C_CTX (context)->c_ops->build_function_type (C_CTX (context),
							return_type,
							&array,
							TYPE_VARARGS (type));
  xfree (array.elements);

  return result;
}

static gcc_type
convert_int (struct compile_c_instance *context, struct type *type)
{
  return C_CTX (context)->c_ops->int_type (C_CTX (context),
					   TYPE_UNSIGNED (type),
					   TYPE_LENGTH (type));
}

static gcc_type
convert_float (struct compile_c_instance *context, struct type *type)
{
  return C_CTX (context)->c_ops->float_type (C_CTX (context),
					     TYPE_LENGTH (type));
}

static gcc_type
convert_void (struct compile_c_instance *context, struct type *type)
{
  return C_CTX (context)->c_ops->void_type (C_CTX (context));
}

static gcc_type
convert_bool (struct compile_c_instance *context, struct type *type)
{
  return C_CTX (context)->c_ops->bool_type (C_CTX (context));
}

static gcc_type
convert_qualified (struct compile_c_instance *context, struct type *type)
{
  struct type *unqual = make_unqualified_type (type);
  gcc_type unqual_converted;
  int quals = 0;

  unqual_converted = convert_type (context, unqual);

  if (TYPE_CONST (type))
    quals |= GCC_QUALIFIER_CONST;
  if (TYPE_VOLATILE (type))
    quals |= GCC_QUALIFIER_VOLATILE;
  if (TYPE_RESTRICT (type))
    quals |= GCC_QUALIFIER_RESTRICT;

  return C_CTX (context)->c_ops->build_qualified_type (C_CTX (context),
						       unqual_converted,
						       quals);
}

static gcc_type
convert_complex (struct compile_c_instance *context, struct type *type)
{
  gcc_type base = convert_type (context, TYPE_TARGET_TYPE (type));

  return C_CTX (context)->c_ops->build_complex_type (C_CTX (context), base);
}

static gcc_type
convert_type_basic (struct compile_c_instance *context, struct type *type)
{
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

    case TYPE_CODE_COMPLEX:
      return convert_complex (context, type);
    }

  return C_CTX (context)->c_ops->error (C_CTX (context),
					_("cannot convert gdb type "
					  "to gcc type"));
}

gcc_type
convert_type (struct compile_c_instance *context, struct type *type)
{
  struct type_map_instance inst, *found;
  gcc_type result;

  /* We don't ever have to deal with typedefs in this code, because
     those are only needed as symbols by the C compiler.  */
  CHECK_TYPEDEF (type);

  inst.type = type;
  found = htab_find (context->type_map, &inst);
  if (found != NULL)
    return found->gcc_type;

  result = convert_type_basic (context, type);
  insert_type (context, type, result);
  return result;
}



static void
delete_instance (struct compile_instance *c)
{
  struct compile_c_instance *context = (struct compile_c_instance *) c;

  context->base.fe->ops->destroy (context->base.fe);
  htab_delete (context->type_map);
  xfree (context);
}

struct compile_instance *
new_compile_instance (struct gcc_c_context *fe)
{
  struct compile_c_instance *result = XCNEW (struct compile_c_instance);

  result->base.fe = &fe->base;
  result->base.destroy = delete_instance;
  result->base.gcc_target_options = "-std=gnu11"
  // Otherwise the .o file may need "_Unwind_Resume" and "__gcc_personality_v0".
				   " -fno-exceptions";

  result->type_map = htab_create_alloc (10, hash_type_map_instance,
					eq_type_map_instance,
					xfree, xcalloc, xfree);

  fe->c_ops->set_callbacks (fe, gcc_convert_symbol,
			    gcc_symbol_address, result);

  return &result->base;
}
