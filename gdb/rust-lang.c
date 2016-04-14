/* Rust language support routines for GDB, the GNU debugger.

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

#include "defs.h"

#include <ctype.h>

#include "c-lang.h"
#include "charset.h"
#include "cp-support.h"
#include "gdbarch.h"
#include "rust-lang.h"
#include "valprint.h"
#include "varobj.h"

extern initialize_file_ftype _initialize_rust_language;



/* Return true if the struct TYPE is a tuple type; otherwise
   false.  */

static int
rust_tuple_type_p (struct type *type)
{
  /* The current implementation is a bit of a hack, but there's
     nothing else in the debuginfo to distinguish a tuple from a
     struct.  */
  gdb_assert (TYPE_CODE (type) == TYPE_CODE_STRUCT);
  return TYPE_TAG_NAME (type) != NULL && TYPE_TAG_NAME (type)[0] == '(';
}

/* See rust-lang.h.  */

int
rust_tuple_struct_type_p (struct type *type)
{
  int i;

  gdb_assert (TYPE_CODE (type) == TYPE_CODE_STRUCT);
  for (i = 0; i < TYPE_NFIELDS (type); ++i)
    {
      if (!field_is_static (&TYPE_FIELD (type, i)))
	return strcmp (TYPE_FIELD_NAME (type, i), "__0") == 0;
    }
  return 0;
}



/* Return true if TYPE is a Rust character type.  */

static int
rust_chartype_p (struct type *type)
{
  return (TYPE_CODE (type) == TYPE_CODE_CHAR
	  && TYPE_LENGTH (type) == 4
	  && TYPE_UNSIGNED (type));
}

/* la_emitchar implementation for Rust.  */

static void
rust_emitchar (int c, struct type *type, struct ui_file *stream, int quoter)
{
  if (!rust_chartype_p (type))
    generic_emit_char (c, type, stream, quoter,
		       target_charset (get_type_arch (type)));
  else if (c == '\\' || c == quoter)
    fprintf_filtered (stream, "\\%c", c);
  else if (c == '\n')
    fputs_filtered ("\\n", stream);
  else if (c == '\r')
    fputs_filtered ("\\r", stream);
  else if (c == '\t')
    fputs_filtered ("\\t", stream);
  else if (c == '\0')
    fputs_filtered ("\\", stream);
  else if (c >= 32 && c <= 127 && isprint (c))
    fputc_filtered (c, stream);
  else if (c <= 255)
    fprintf_filtered (stream, "\\x%02x", c);
  else
    fprintf_filtered (stream, "\\u{%06x}", c);
}

/* la_printchar implementation for Rust.  */

static void
rust_printchar (int c, struct type *type, struct ui_file *stream)
{
  fputs_filtered ("'", stream);
  LA_EMIT_CHAR (c, type, stream, '\'');
  fputs_filtered ("'", stream);
}

/* la_printstr implementation for Rust.  */

static void
rust_printstr (struct ui_file *stream, struct type *type,
	       const gdb_byte *string, unsigned int length,
	       const char *user_encoding, int force_ellipses,
	       const struct value_print_options *options)
{
  /* Rust always uses UTF-8, but let the caller override this if need
     be.  */
  const char *encoding = user_encoding;
  if (user_encoding == NULL || !*user_encoding)
    {
      /* In Rust strings, characters are "u8".  */
      if (TYPE_CODE (type) == TYPE_CODE_INT
	  && TYPE_UNSIGNED (type)
	  && TYPE_LENGTH (type) == 1)
	encoding = "UTF-8";
      else
	{
	  /* This is probably some C string, so let's let C deal with
	     it.  */
	  c_printstr (stream, type, string, length, user_encoding,
		      force_ellipses, options);
	  return;
	}
    }

  /* FIXME this is not ideal as it doesn't use our character printer.  */
  generic_printstr (stream, type, string, length, encoding, force_ellipses,
		    '"', 0, options);
}



static const struct generic_val_print_decorations rust_decorations =
{
  /* Complex isn't used in Rust, but we provide C-ish values just in
     case.  */
  "",
  " + ",
  " * I",
  "true",
  "false",
  "void",
  "[",
  "]"
};

/* la_val_print implementation for Rust.  */

static void
rust_val_print (struct type *type, const gdb_byte *valaddr, int embedded_offset,
		CORE_ADDR address, struct ui_file *stream, int recurse,
		const struct value *val,
		const struct value_print_options *options)
{
  type = check_typedef (type);
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_METHODPTR:
    case TYPE_CODE_PTR:
    case TYPE_CODE_UNION:
    case TYPE_CODE_MEMBERPTR:
      c_val_print (type, valaddr, embedded_offset, address, stream,
		   recurse, val, options);
      break;

    case TYPE_CODE_INT:
      /* Recognize the unit type.  */
      if (TYPE_UNSIGNED (type) && TYPE_LENGTH (type) == 0
	  && TYPE_NAME (type) != NULL && strcmp (TYPE_NAME (type), "()") == 0)
	{
	  fputs_filtered ("()", stream);
	  break;
	}
      goto generic_print;

    case TYPE_CODE_STRING:
      {
	struct gdbarch *arch = get_type_arch (type);
	int unit_size = gdbarch_addressable_memory_unit_size (arch);
	LONGEST low_bound, high_bound;

	if (!get_array_bounds (type, &low_bound, &high_bound))
	  error (_("Could not determine the array bounds"));

	/* If we see a plain TYPE_CODE_STRING, then we're printing a
	   byte string, hence the choice of "ASCII" as the encoding.
	   FIXME perhaps we should print a "b" before the string.  */
	rust_printstr (stream, TYPE_TARGET_TYPE (type),
		       valaddr + embedded_offset * unit_size,
		       high_bound - low_bound + 1, "ASCII", 0, options);
      }
      break;

    case TYPE_CODE_ARRAY:
      {
	LONGEST low_bound, high_bound;

	if (get_array_bounds (type, &low_bound, &high_bound)
	    && high_bound - low_bound + 1 == 0)
	  fputs_filtered ("[]", stream);
	else
	  goto generic_print;
      }
      break;

    case TYPE_CODE_STRUCT:
      {
	int i;
	int first_field;
	int is_tuple = rust_tuple_type_p (type);
	int is_tuple_struct = !is_tuple && rust_tuple_struct_type_p (type);
	struct value_print_options opts;

	if (!is_tuple && TYPE_TAG_NAME (type))
	  fprintf_filtered (stream, "%s ", TYPE_TAG_NAME (type));

	if (is_tuple || is_tuple_struct)
	  fputs_filtered ("(", stream);
	else
	  fputs_filtered ("{", stream);

	opts = *options;
	opts.deref_ref = 0;

	first_field = 1;
	for (i = 0; i < TYPE_NFIELDS (type); ++i)
	  {
	    if (field_is_static (&TYPE_FIELD (type, i)))
	      continue;

	    if (!first_field)
	      fputs_filtered (",", stream);

	    if (options->prettyformat)
	      {
		fputs_filtered ("\n", stream);
		print_spaces_filtered (2 + 2 * recurse, stream);
	      }
	    else if (!first_field)
	      fputs_filtered (" ", stream);

	    first_field = 0;

	    if (!is_tuple && !is_tuple_struct)
	      {
		fputs_filtered (TYPE_FIELD_NAME (type, i), stream);
		fputs_filtered (": ", stream);
	      }

	    val_print (TYPE_FIELD_TYPE (type, i),
		       valaddr,
		       embedded_offset + TYPE_FIELD_BITPOS (type, i) / 8,
		       address,
		       stream, recurse + 1, val, &opts,
		       current_language);
	  }

	if (options->prettyformat)
	  {
	    fputs_filtered ("\n", stream);
	    print_spaces_filtered (2 * recurse, stream);
	  }

	if (is_tuple || is_tuple_struct)
	  fputs_filtered (")", stream);
	else
	  fputs_filtered ("}", stream);
      }
      break;

    default:
    generic_print:
      /* Nothing special yet.  */
      generic_val_print (type, valaddr, embedded_offset, address, stream,
			 recurse, val, options, &rust_decorations);
    }
}



/* la_print_typedef implementation for Rust.  */

static void
rust_print_typedef (struct type *type,
		    struct symbol *new_symbol,
		    struct ui_file *stream)
{
  type = check_typedef (type);
  fprintf_filtered (stream, "type %s = ", SYMBOL_PRINT_NAME (new_symbol));
  type_print (type, "", stream, 0);
  fprintf_filtered (stream, ";\n");
}

/* la_print_type implementation for Rust.  */

static void
rust_print_type (struct type *type, const char *varstring,
		 struct ui_file *stream, int show, int level,
		 const struct type_print_options *flags)
{
  int i;

  QUIT;
  if (show <= 0
      && TYPE_NAME (type) != NULL)
    {
      fputs_filtered (TYPE_NAME (type), stream);
      return;
    }

  type = check_typedef (type);
  switch (TYPE_CODE (type))
    {
    case TYPE_CODE_FUNC:
      /* Delegate varargs to the C printer.  Not totally sure this
	 is correct.  */
      if (TYPE_VARARGS (type))
	goto c_printer;

      fputs_filtered ("fn (", stream);
      for (i = 0; i < TYPE_NFIELDS (type); ++i)
	{
	  QUIT;
	  if (i > 0)
	    fputs_filtered (", ", stream);
	  rust_print_type (TYPE_FIELD_TYPE (type, i), "", stream, -1, 0,
			   flags);
	}
      fputs_filtered (") -> ", stream);
      rust_print_type (TYPE_TARGET_TYPE (type), "", stream, -1, 0, flags);
      break;

    case TYPE_CODE_ARRAY:
      {
	LONGEST low_bound, high_bound;

	fputs_filtered ("[", stream);
	rust_print_type (TYPE_TARGET_TYPE (type), NULL,
			 stream, show - 1, level, flags);
	fputs_filtered ("; ", stream);

	if (TYPE_HIGH_BOUND_KIND (TYPE_INDEX_TYPE (type)) == PROP_LOCEXPR
	    || TYPE_HIGH_BOUND_KIND (TYPE_INDEX_TYPE (type)) == PROP_LOCLIST)
	  fprintf_filtered (stream, "variable length");
	else if (get_array_bounds (type, &low_bound, &high_bound))
	  fprintf_filtered (stream, "%s", 
			    plongest (high_bound - low_bound + 1));
	fputs_filtered ("]", stream);
      }
      break;

    case TYPE_CODE_STRUCT:
      {
	int is_tuple_struct;

	/* Print a tuple type simply.  */
	if (rust_tuple_type_p (type))
	  {
	    fputs_filtered (TYPE_TAG_NAME (type), stream);
	    break;
	  }

	/* If we see a base class, delegate to C.  */
	if (TYPE_N_BASECLASSES (type) > 0)
	  goto c_printer;

	fputs_filtered ("struct ", stream);
	if (TYPE_TAG_NAME (type) != NULL)
	  fputs_filtered (TYPE_TAG_NAME (type), stream);

	is_tuple_struct = rust_tuple_struct_type_p (type);
	fputs_filtered (is_tuple_struct ? " (\n" : " {\n", stream);

	for (i = 0; i < TYPE_NFIELDS (type); ++i)
	  {
	    const char *name;

	    QUIT;
	    if (field_is_static (&TYPE_FIELD (type, i)))
	      continue;

	    /* FIXME could try to print "pub", but (1) rustc doesn't
	       emit the debuginfo, and (2) it relies on c++-specific
	       stuff anyway.  */

	    /* For a tuple struct we print the type but nothing
	       else.  */
	    print_spaces_filtered (level + 2, stream);
	    if (!is_tuple_struct)
	      fprintf_filtered (stream, "%s: ", TYPE_FIELD_NAME (type, i));

	    rust_print_type (TYPE_FIELD_TYPE (type, i), NULL,
			     stream, show - 1, level + 2,
			     flags);
	    fputs_filtered (",\n", stream);
	  }

	fprintfi_filtered (level, stream, is_tuple_struct ? ")" : "}");
      }
      break;

    case TYPE_CODE_ENUM:
      {
	int i, len = 0;

	fputs_filtered ("enum ", stream);
	if (TYPE_TAG_NAME (type) != NULL)
	  {
	    fputs_filtered (TYPE_TAG_NAME (type), stream);
	    fputs_filtered (" ", stream);
	    len = strlen (TYPE_TAG_NAME (type));
	  }
	fputs_filtered ("{\n", stream);      

	for (i = 0; i < TYPE_NFIELDS (type); ++i)
	  {
	    const char *name = TYPE_FIELD_NAME (type, i);

	    QUIT;

	    if (len > 0
		&& strncmp (name, TYPE_TAG_NAME (type), len) == 0
		&& name[len] == ':'
		&& name[len + 1] == ':')
	      name += len + 2;
	    fprintfi_filtered (level + 2, stream, "%s,\n", name);
	  }

	fputs_filtered ("}", stream);
      }
      break;

    default:
    c_printer:
      c_print_type (type, varstring, stream, show, level, flags);
    }
}



enum rust_primitive_types
{
  rust_primitive_bool,
  rust_primitive_char,
  rust_primitive_i8,
  rust_primitive_u8,
  rust_primitive_i16,
  rust_primitive_u16,
  rust_primitive_i32,
  rust_primitive_u32,
  rust_primitive_i64,
  rust_primitive_u64,
  rust_primitive_isize,
  rust_primitive_usize,
  rust_primitive_f32,
  rust_primitive_f64,
  rust_primitive_unit,
  rust_primitive_str,
  nr_rust_primitive_types
};

/* la_language_arch_info implementation for Rust.  */

static void
rust_language_arch_info (struct gdbarch *gdbarch,
			 struct language_arch_info *lai)
{
  const struct builtin_type *builtin = builtin_type (gdbarch);
  struct type *str;
  struct type **types;
  unsigned int length;

  types = GDBARCH_OBSTACK_CALLOC (gdbarch, nr_rust_primitive_types + 1,
				  struct type *);

  types[rust_primitive_bool] = arch_boolean_type (gdbarch, 8, 1, "bool");
  types[rust_primitive_char] = arch_character_type (gdbarch, 32, 1, "char");
  types[rust_primitive_i8] = arch_integer_type (gdbarch, 8, 0, "i8");
  types[rust_primitive_u8] = arch_integer_type (gdbarch, 8, 1, "u8");
  types[rust_primitive_i16] = arch_integer_type (gdbarch, 16, 0, "i16");
  types[rust_primitive_u16] = arch_integer_type (gdbarch, 16, 1, "u16");
  types[rust_primitive_i32] = arch_integer_type (gdbarch, 32, 0, "i32");
  types[rust_primitive_u32] = arch_integer_type (gdbarch, 32, 1, "u32");
  types[rust_primitive_i64] = arch_integer_type (gdbarch, 64, 0, "i64");
  types[rust_primitive_u64] = arch_integer_type (gdbarch, 64, 1, "u64");

  length = 8 * TYPE_LENGTH (builtin->builtin_data_ptr);
  types[rust_primitive_isize] = arch_integer_type (gdbarch, length, 0, "isize");
  types[rust_primitive_usize] = arch_integer_type (gdbarch, length, 1, "usize");

  types[rust_primitive_f32] = arch_float_type (gdbarch, 32, "f32", NULL);
  types[rust_primitive_f64] = arch_float_type (gdbarch, 64, "f64", NULL);

  types[rust_primitive_unit] = arch_integer_type (gdbarch, 0, 1, "()");

  str = arch_composite_type (gdbarch, "&str", TYPE_CODE_STRUCT);
  /* For some reason gdb doesn't set this, but then later does require
     it.  */
  TYPE_NAME (str) = "&str";
  /* The type here isn't strictly correct; instead we want
     "*const u8".  */
  append_composite_type_field_aligned
    (str, "data_ptr",
     lookup_pointer_type (types[rust_primitive_u8]),
     0);
  append_composite_type_field_aligned (str, "length",
				       types[rust_primitive_usize],
				       length);
  types[rust_primitive_str] = str;

  lai->primitive_type_vector = types;
  lai->bool_type_default = types[rust_primitive_bool];
  lai->string_char_type = types[rust_primitive_u8];
}



/* evaluate_exp implementation for Rust.  */

static struct value *
rust_evaluate_subexp (struct type *expect_type, struct expression *exp,
		      int *pos, enum noside noside)
{
  struct value *result;

  switch (exp->elts[*pos].opcode)
    {
    case UNOP_COMPLEMENT:
      {
	struct value *value;

	++*pos;
	value = evaluate_subexp (NULL_TYPE, exp, pos, noside);
	if (noside == EVAL_SKIP)
	  {
	    /* Preserving the type is enough.  */
	    return value;
	  }
	if (TYPE_CODE (value_type (value)) == TYPE_CODE_BOOL)
	  result = value_from_longest (value_type (value),
				       value_logical_not (value));
	else
	  result = value_complement (value);
      }
      break;

    case OP_AGGREGATE:
      {
	int pc = (*pos)++;
	struct type *type = exp->elts[pc + 1].type;
	int arglen = longest_to_int (exp->elts[pc + 2].longconst);
	int i;
	CORE_ADDR addr = 0;
	struct value *addrval = NULL;

	*pos += 3;

	if (noside == EVAL_NORMAL)
	  {
	    addrval = value_allocate_space_in_inferior (TYPE_LENGTH (type));
	    addr = value_as_long (addrval);
	    result = value_at_lazy (type, addr);
	  }

	if (arglen > 0 && exp->elts[*pos].opcode == OP_OTHERS)
	  {
	    struct value *init;

	    ++*pos;
	    init = rust_evaluate_subexp (NULL, exp, pos, noside);
	    if (noside == EVAL_NORMAL)
	      {
		/* FIXME compare types */
		/* FIXME this is bogus */
		value_assign (result, init);
	      }

	    --arglen;
	  }

	gdb_assert (arglen % 2 == 0);
	for (i = 0; i < arglen; i += 2)
	  {
	    int len;
	    const char *fieldname;
	    struct value *value, *field;

	    gdb_assert (exp->elts[*pos].opcode == OP_NAME);
	    ++*pos;
	    len = longest_to_int (exp->elts[*pos].longconst);
	    ++*pos;
	    fieldname = &exp->elts[*pos].string;
	    *pos += 2 + BYTES_TO_EXP_ELEM (len + 1);

	    value = rust_evaluate_subexp (NULL, exp, pos, noside);
	    if (noside == EVAL_NORMAL)
	      {
		field = value_struct_elt (&result, NULL, fieldname, NULL,
					  "structure");
		value_assign (field, value);
	      }
	  }

	if (noside == EVAL_SKIP)
	  return value_from_longest (builtin_type (exp->gdbarch)->builtin_int,
				     1);
	else if (noside == EVAL_AVOID_SIDE_EFFECTS)
	  result = allocate_value (type);
	else
	  result = value_at_lazy (type, addr);
      }
      break;

    case OP_RUST_ARRAY:
      {
	int pc = (*pos)++;
	int copies;
	struct value *elt;
	struct value *ncopies;

	elt = rust_evaluate_subexp (NULL, exp, pos, noside);
	ncopies = rust_evaluate_subexp (NULL, exp, pos, noside);
	copies = value_as_long (ncopies);
	if (copies < 0)
	  error (_("array with negative number of elements"));

	if (noside == EVAL_NORMAL)
	  {
	    CORE_ADDR addr;
	    int i;
	    struct value **eltvec = XNEWVEC (struct value *, copies);
	    struct cleanup *cleanup = make_cleanup (xfree, eltvec);

	    for (i = 0; i < copies; ++i)
	      eltvec[i] = elt;
	    result = value_array (0, copies - 1, eltvec);

	    do_cleanups (cleanup);
	  }
	else
	  {
	    struct type *arraytype
	      = lookup_array_range_type (value_type (elt), 0, copies - 1);
	    result = allocate_value (arraytype);
	  }
      }
      break;

    default:
      result = evaluate_subexp_standard (expect_type, exp, pos, noside);
      break;
    }

  return result;
}

/* operator_length implementation for Rust.  */

static void
rust_operator_length (const struct expression *exp, int pc, int *oplenp,
		      int *argsp)
{
  int oplen = 1;
  int args = 0;

  switch (exp->elts[pc - 1].opcode)
    {
    case OP_AGGREGATE:
      /* We handle aggregate as a type and argument count.  The first
	 argument might be OP_OTHERS.  After that the arguments
	 alternate: first an OP_NAME, then an expression.  */
      oplen = 4;
      args = longest_to_int (exp->elts[pc - 2].longconst);
      break;

    case OP_OTHERS:
      oplen = 1;
      args = 1;
      break;

    case OP_RUST_ARRAY:
      oplen = 1;
      args = 2;
      break;

    default:
      operator_length_standard (exp, pc, oplenp, argsp);
      return;
    }

  *oplenp = oplen;
  *argsp = args;
}

/* op_name implementation for Rust.  */

static char *
rust_op_name (enum exp_opcode opcode)
{
  switch (opcode)
    {
    case OP_AGGREGATE:
      return "OP_AGGREGATE";
    case OP_NAME:
      return "OP_NAME";
    case OP_OTHERS:
      return "OP_OTHERS";
    case OP_RUST_ARRAY:
      return "OP_RUST_ARRAY";
    default:
      return op_name_standard (opcode);
    }
}

/* dump_subexp_body implementation for Rust.  */

static int
rust_dump_subexp_body (struct expression *exp, struct ui_file *stream,
		       int elt)
{
  switch (exp->elts[elt].opcode)
    {
    case OP_AGGREGATE:
      {
	int length = longest_to_int (exp->elts[elt + 2].longconst);
	int i;

	fprintf_filtered (stream, "Type @");
	gdb_print_host_address (exp->elts[elt + 1].type, stream);
	fprintf_filtered (stream, " (");
	type_print (exp->elts[elt + 1].type, NULL, stream, 0);
	fprintf_filtered (stream, "), length %d", length);

	elt += 4;
	for (i = 0; i < length; ++i)
	  elt = dump_subexp (exp, stream, elt);
      }
      break;

    case OP_STRING:
    case OP_NAME:
      {
	LONGEST len = exp->elts[elt + 1].longconst;

	fprintf_filtered (stream, "%s: %s",
			  (exp->elts[elt].opcode == OP_STRING
			   ? "string" : "name"),
			  &exp->elts[elt + 2].string);
	elt += 4 + BYTES_TO_EXP_ELEM (len + 1);
      }
      break;

    case OP_OTHERS:
      elt = dump_subexp (exp, stream, elt + 1);
      break;

    case OP_RUST_ARRAY:
      break;

    default:
      elt = dump_subexp_body_standard (exp, stream, elt);
      break;
    }

  return elt;
}

/* print_subexp implementation for Rust.  */

static void
rust_print_subexp (struct expression *exp, int *pos, struct ui_file *stream,
		   enum precedence prec)
{
  switch (exp->elts[*pos].opcode)
    {
    case OP_AGGREGATE:
      {
	int length = longest_to_int (exp->elts[*pos + 2].longconst);
	int i;

	type_print (exp->elts[*pos + 1].type, "", stream, 0);
	fputs_filtered (" { ", stream);

	*pos += 4;
	for (i = 0; i < length; ++i)
	  {
	    rust_print_subexp (exp, pos, stream, prec);
	    fputs_filtered (", ", stream);
	  }
	fputs_filtered (" }", stream);
      }
      break;

    case OP_NAME:
      {
	LONGEST len = exp->elts[*pos + 1].longconst;

	fputs_filtered (&exp->elts[*pos + 2].string, stream);
	*pos += 4 + BYTES_TO_EXP_ELEM (len + 1);
      }
      break;

    case OP_OTHERS:
      {
	fputs_filtered ("<<others>> (", stream);
	++*pos;
	rust_print_subexp (exp, pos, stream, prec);
	fputs_filtered (")", stream);
      }
      break;

    case OP_RUST_ARRAY:
      ++*pos;
      fprintf_filtered (stream, "[");
      rust_print_subexp (exp, pos, stream, prec);
      fprintf_filtered (stream, "; ");
      rust_print_subexp (exp, pos, stream, prec);
      fprintf_filtered (stream, "]");
      break;

    default:
      print_subexp_standard (exp, pos, stream, prec);
      break;
    }
}

/* operator_check implementation for Rust.  */

static int
rust_operator_check (struct expression *exp, int pos,
		     int (*objfile_func) (struct objfile *objfile,
					  void *data),
		     void *data)
{
  switch (exp->elts[pos].opcode)
    {
    case OP_AGGREGATE:
      {
	struct type *type = exp->elts[pos + 1].type;
	struct objfile *objfile = TYPE_OBJFILE (type);

	if (objfile != NULL && (*objfile_func) (objfile, data))
	  return 1;
      }
      break;

    case OP_OTHERS:
    case OP_NAME:
    case OP_RUST_ARRAY:
      break;

    default:
      return operator_check_standard (exp, pos, objfile_func, data);
    }

  return 0;
}



static const struct exp_descriptor exp_descriptor_rust = 
{
  rust_print_subexp,
  rust_operator_length,
  rust_operator_check,
  rust_op_name,
  rust_dump_subexp_body,
  rust_evaluate_subexp
};

static const struct language_defn rust_language_defn =
{
  "rust",
  "Rust",
  language_rust,
  range_check_on,
  case_sensitive_on,
  array_row_major,
  macro_expansion_no,
  &exp_descriptor_rust,
  rust_parse,
  rusterror,
  null_post_parser,
  rust_printchar,		/* Print a character constant */
  rust_printstr,		/* Function to print string constant */
  rust_emitchar,		/* Print a single char */
  rust_print_type,		/* Print a type using appropriate syntax */
  rust_print_typedef,		/* Print a typedef using appropriate syntax */
  rust_val_print,		/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  default_read_var_value,	/* la_read_var_value */
  NULL,				/* Language specific skip_trampoline */
  NULL,				/* name_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  gdb_demangle,			/* Language specific symbol demangler */
  NULL,				/* Language specific
				   class_name_from_physname */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  default_word_break_characters,
  default_make_symbol_completion_list,
  rust_language_arch_info,
  default_print_array_index,
  default_pass_by_reference,
  c_get_string,
  NULL,				/* la_get_symbol_name_cmp */
  iterate_over_symbols,
  &default_varobj_ops,
  NULL,
  NULL,
  LANG_MAGIC
};

void
_initialize_rust_language (void)
{
  add_language (&rust_language_defn);
}
