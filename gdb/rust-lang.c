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
		    '"', 1, options);
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
  "void"
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
    case TYPE_CODE_ARRAY:
    case TYPE_CODE_METHODPTR:
    case TYPE_CODE_PTR:
    case TYPE_CODE_UNION:
    case TYPE_CODE_STRUCT:
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
      /* Fallthrough.  */

    default:
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
	int first_field, is_tuple_struct;

	/* Print a tuple type simply.  Tuples currently are recognized
	   by name.  */
	if (TYPE_TAG_NAME (type) != NULL && TYPE_TAG_NAME (type)[0] == '(')
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

	first_field = 1;
	is_tuple_struct = 0;
	for (i = 0; i < TYPE_NFIELDS (type); ++i)
	  {
	    const char *name;

	    if (field_is_static (&TYPE_FIELD (type, i)))
	      continue;

	    if (first_field)
	      {
		/* See if this looks like a tuple struct.  It would be
		   nice if there were a better way.  */
		if (strcmp (TYPE_FIELD_NAME (type, i), "__0") == 0)
		  {
		    is_tuple_struct = 1;
		    fputs_filtered (" (\n", stream);
		  }
		else
		  fputs_filtered (" {\n", stream);
		first_field = 0;
	      }

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
  nr_rust_primitive_types
};

/* la_language_arch_info implementation for Rust.  */

static void
rust_language_arch_info (struct gdbarch *gdbarch,
			 struct language_arch_info *lai)
{
  const struct builtin_type *builtin = builtin_type (gdbarch);
  struct type **types;
  unsigned int length;

  types = GDBARCH_OBSTACK_CALLOC (gdbarch, nr_rust_primitive_types + 1,
				  struct type *);
  lai->primitive_type_vector = types;

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

  lai->bool_type_default = types[rust_primitive_bool];
}



/* evaluate_exp implementation for Rust.  */
static struct value *
evaluate_subexp_rust (struct type *expect_type, struct expression *exp,
		      int *pos, enum noside noside)
{
  if (exp->elts[*pos].opcode == UNOP_COMPLEMENT)
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
	return value_from_longest (value_type (value),
				   value_logical_not (value));
      return value_complement (value);
    }

  return evaluate_subexp_standard (expect_type, exp, pos, noside);
}



static const struct exp_descriptor exp_descriptor_rust = 
{
  print_subexp_standard,
  operator_length_standard,
  operator_check_standard,
  op_name_standard,
  dump_subexp_body_standard,
  evaluate_subexp_rust
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
