/* C language support for compilation.

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
#include "compile-internal.h"
#include "compile.h"
#include "gdb-dlfcn.h"
#include "c-lang.h"
#include "macrotab.h"
#include "macroscope.h"
#include "regcache.h"

/* A silly little helper to get the gcc mode name for a size.  */

const char *
c_get_mode_for_size (int size)
{
  const char *mode = NULL;

  switch (size)
    {
    case 1:
      mode = "QI";
      break;
    case 2:
      mode = "HI";
      break;
    case 4:
      mode = "SI";
      break;
    case 8:
      mode = "DI";
      break;
    }

  return mode;
}



#define STR(x) #x
#define STRINGIFY(x) STR(x)

/* Helper function for get_compile_context.  dlopen the GCC front-end,
   extract the symbol that will provide a vtable and call that
   function.  Return the gcc_context that was returned.  */

static gcc_c_fe_context_function *
load_libcc (void)
{
  void *handle;
  gcc_c_fe_context_function *func;
  struct gcc_context *result;

   /* gdb_dlopen and gdb_dlsym will call error () on an error, so no
      need to check value.  */
  handle = gdb_dlopen (STRINGIFY (GCC_C_FE_LIBCC));
  return gdb_dlsym (handle, STRINGIFY (GCC_C_FE_CONTEXT));
}

/* Return the GCC FE context.  */

struct compile_instance *
c_get_compile_context (void)
{
  static gcc_c_fe_context_function *func;

  struct gcc_c_context *context;

  if (func == NULL)
    {
      func = load_libcc ();
      gdb_assert (func != NULL);
    }

  context = (*func) (GCC_FE_VERSION_0, GCC_C_FE_VERSION_0);
  if (context == NULL)
    error (_("the loaded version of GCC does not support the required version "
	     "of the API"));

  return new_compile_instance (context);
}



/* Write one macro definition.  */

static void
print_one_macro (const char *name, const struct macro_definition *macro,
		 struct macro_source_file *source, int line,
		 void *user_data)
{
  struct ui_file *file = user_data;

  /* Don't print command-line defines.  They will be supplied another
     way.  */
  if (line == 0)
    return;

  fprintf_filtered (file, "#define %s", name);

  if (macro->kind == macro_function_like)
    {
      int i;

      fputs_filtered ("(", file);
      for (i = 0; i < macro->argc; i++)
	{
	  fputs_filtered (macro->argv[i], file);
	  if (i + 1 < macro->argc)
	    fputs_filtered (", ", file);
	}
      fputs_filtered (")", file);
    }

  fprintf_filtered (file, " %s\n", macro->replacement);
}

/* Write macro definitions at PC to FILE.  */

static void
write_macro_definitions (const struct block *block, CORE_ADDR pc,
			 struct ui_file *file)
{
  struct macro_scope *scope;

  if (block != NULL)
    scope = sal_macro_scope (find_pc_line (pc, 0));
  else
    scope = default_macro_scope ();
  if (scope == NULL)
    scope = user_macro_scope ();

  if (scope != NULL && scope->file != NULL && scope->file->table != NULL)
    macro_for_each_in_scope (scope->file, scope->line, print_one_macro, file);
}

/* Helper function to construct a header scope for a block of code.
   Takes a scope argument which selects the correct header to
   insert.  */

static void
add_code_header (enum compile_i_scope_types type, struct ui_file *buf)
{
  switch (type)
  {
  case COMPILE_I_SIMPLE_SCOPE:
    fputs_unfiltered ("void "
		      GCC_FE_WRAPPER_FUNCTION
		      " (struct "
		      COMPILE_I_SIMPLE_REGISTER_STRUCT_TAG
		      " *"
		      COMPILE_I_SIMPLE_REGISTER_ARG_NAME
		      ") {\n",
		      buf);
    break;
  case COMPILE_I_RAW_SCOPE:
    break;
  default:
    break;
    /* TODO: Error case, but do nothing for now.  */
  }
}

/* Helper function to construct a footer scope for a block of code.
   Takes a scope argument which selects the correct footer to
   insert.  */

static void
add_code_footer (enum compile_i_scope_types type, struct ui_file *buf)
{
  switch (type)
  {
  case COMPILE_I_SIMPLE_SCOPE:
    fputs_unfiltered ("}\n", buf);
    break;
  case COMPILE_I_RAW_SCOPE:
    break;
  default:
    /* TODO: Error case, but do nothing for now.  */
    break;
  }
}

/* Generate a structure holding all the registers used by the function
   we're generating.  */

static void
generate_register_struct (struct ui_file *stream, struct gdbarch *gdbarch,
			  unsigned char *registers_used)
{
  int i;
  int seen = 0;

  fputs_unfiltered ("struct " COMPILE_I_SIMPLE_REGISTER_STRUCT_TAG " {\n",
		    stream);

  if (registers_used != NULL)
    for (i = 0; i < gdbarch_num_regs (gdbarch); ++i)
      {
	if (registers_used[i])
	  {
	    struct type *regtype = check_typedef (register_type (gdbarch, i));
	    char *regname = compile_register_name_mangled (gdbarch, i);
	    struct cleanup *cleanups = make_cleanup (xfree, regname);

	    seen = 1;

	    /* You might think we could use type_print here.  However,
	       target descriptions often use types with names like
	       "int64_t", which may not be defined in the inferior
	       (and in any case would not be looked up due to the
	       #pragma business).  So, we take a much simpler
	       approach: for pointer- or integer-typed registers, emit
	       the field in the most direct way; and for other
	       register types (typically flags or vectors), emit a
	       maximally-aligned array of the correct size.  */

	    fputs_unfiltered ("  ", stream);
	    switch (TYPE_CODE (regtype))
	      {
	      case TYPE_CODE_PTR:
		fprintf_filtered (stream, "void *%s", regname);
		break;

	      case TYPE_CODE_INT:
		{
		  const char *mode
		    = c_get_mode_for_size (TYPE_LENGTH (regtype));

		  if (mode != NULL)
		    {
		      if (TYPE_UNSIGNED (regtype))
			fputs_unfiltered ("unsigned ", stream);
		      fprintf_unfiltered (stream,
					  "int %s"
					  " __attribute__ ((__mode__(__%s__)))",
					  regname,
					  mode);
		      break;
		    }
		}

		/* Fall through.  */

	      default:
		fprintf_unfiltered (stream,
				    "  unsigned char %s[%d]"
				    " __attribute__((__aligned__("
				    "__BIGGEST_ALIGNMENT__)))",
				    regname,
				    TYPE_LENGTH (regtype));
	      }
	    fputs_unfiltered (";\n", stream);

	    do_cleanups (cleanups);
	  }
      }

  if (!seen)
    fputs_unfiltered ("  char " COMPILE_I_SIMPLE_REGISTER_DUMMY ";\n",
		      stream);

  fputs_unfiltered ("};\n\n", stream);
}

/* Helper function to take an expression and wrap it in a scope for
   the compiler.  CMD is populated for a multi-line expression, while
   SIMPLE_STRING is populated if the expression is on one single line.
   TYPE denotes the scope type to use.  Either CMD must be NULL and
   SIMPLE_STRING populated (the two are mutually exclusive), or
   vice-versa.  EXPR_BLOCK denotes the block relevant contextually to
   the inferior when the expression was created, and EXPR_PC
   indicates the value of $PC.  */

char *
c_compute_program (const char *input,
		   enum compile_i_scope_types scope,
		   struct gdbarch *gdbarch,
		   const struct block *expr_block,
		   CORE_ADDR expr_pc)
{
  struct ui_file *buf, *var_stream;
  char *code, *reg_code;
  unsigned char *registers_used;
  struct cleanup *cleanup;

  buf = mem_fileopen ();
  cleanup = make_cleanup_ui_file_delete (buf);

  write_macro_definitions (expr_block, expr_pc, buf);

  /* Generate the code to compute variable locations, but do it before
     generating the function header, so we can define the register
     struct before the function body.  This requires a temporary
     stream.  */
  var_stream = mem_fileopen ();
  make_cleanup_ui_file_delete (var_stream);
  registers_used = generate_c_for_variable_locations (var_stream, gdbarch,
						      expr_block, expr_pc);
  make_cleanup (xfree, registers_used);

  generate_register_struct (buf, gdbarch, registers_used);

  fputs_unfiltered ("typedef unsigned int"
		    " __attribute__ ((__mode__(__pointer__)))"
		    " __gdb_uintptr;\n",
		    buf);
  fputs_unfiltered ("typedef int"
		    " __attribute__ ((__mode__(__pointer__)))"
		    " __gdb_intptr;\n",
		    buf);

  add_code_header (scope, buf);

  reg_code = ui_file_xstrdup (var_stream, NULL);
  make_cleanup (xfree, reg_code);
  if (scope == COMPILE_I_SIMPLE_SCOPE)
    fputs_unfiltered (reg_code, buf);

  fputs_unfiltered ("#pragma GCC user_expression\n", buf);

  /* For larger user expressions the automatic semicolons may be confusing.  */
  if (strchr (input, '\n') == NULL)
    fputs_unfiltered ("#pragma GCC trailing_semicolon\n", buf);

  fputs_unfiltered ("#line 1 \"gdb command line\"\n", buf);
  fputs_unfiltered (input, buf);
  fputs_unfiltered ("\n", buf);

  add_code_footer (scope, buf);
  code = ui_file_xstrdup (buf, NULL);
  do_cleanups (cleanup);
  return code;
}
