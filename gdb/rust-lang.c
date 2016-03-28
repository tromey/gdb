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
#include "c-lang.h"
#include "valprint.h"
#include "varobj.h"

extern initialize_file_ftype _initialize_rust_language;

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

    default:
      /* Nothing special yet.  */
      generic_val_print (type, valaddr, embedded_offset, address, stream,
			 recurse, val, options, &rust_decorations);
    }
}



static const struct language_defn rust_language_defn =
{
  "rust",
  "Rust",
  language_rust,
  range_check_on,
  case_sensitive_on,
  array_row_major,
  macro_expansion_no,
  &exp_descriptor_c,
  c_parse,
  c_error,
  null_post_parser,
  c_printchar,			/* Print a character constant */
  c_printstr,			/* Function to print string constant */
  c_emit_char,			/* Print a single char */
  c_print_type,			/* Print a type using appropriate syntax */
  c_print_typedef,		/* Print a typedef using appropriate syntax */
  rust_val_print,		/* Print a value using appropriate syntax */
  c_value_print,		/* Print a top-level value */
  default_read_var_value,	/* la_read_var_value */
  NULL,				/* Language specific skip_trampoline */
  NULL,				/* name_of_this */
  basic_lookup_symbol_nonlocal,	/* lookup_symbol_nonlocal */
  basic_lookup_transparent_type,/* lookup_transparent_type */
  NULL,				/* Language specific symbol demangler */
  NULL,				/* Language specific
				   class_name_from_physname */
  c_op_print_tab,		/* expression operators for printing */
  1,				/* c-style arrays */
  0,				/* String lower bound */
  default_word_break_characters,
  default_make_symbol_completion_list,
  c_language_arch_info,
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
