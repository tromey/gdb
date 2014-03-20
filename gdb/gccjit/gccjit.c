/* General GCC JIT GDB code

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
#include "command.h"
#include "ui-out.h"
#include "cli/cli-script.h"
#include "gdbcmd.h"
#include "language.h"
#include "exceptions.h"
#include "cli/cli-utils.h"
#include <ctype.h>
#include "interps.h"
#include "gccjit/gccjit.h"
#include "gdb-dlfcn.h"
#include "gccjit-internal.h"
#include "gdbjit-load.h"
#include "frame.h"
#include "symfile.h"
#include "source.h"
#include "block.h"
#include "macrotab.h"
#include "macroscope.h"



static int gccjit_debug;

static void
show_gccjit_debug (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("GCC JIT debugging is %s.\n"), value);
}



#define HAVE_GCC_JIT 1
#define STR(x) #x
#define STRINGIFY(x) STR(x)

/* Handle the input from the expression or expr command.  The
   "expression" command is used to evaluate an expression that may
   contain calls to the GCC JIT interface.  TODO: Initially all we
   expect in this command is straight up C code blocks.  */

static void
gcc_jit_command (char *arg, int from_tty)
{
  struct cleanup *cleanup;

  cleanup = make_cleanup_restore_integer (&interpreter_async);
  interpreter_async = 0;

  arg = skip_spaces (arg);
  if (arg && *arg)
    eval_gcc_jit_command (NULL, arg);
  else
    {
      struct command_line *l = get_command_line (jit_control, "");

      make_cleanup_free_command_lines (&l);
      execute_control_command_untraced (l);
    }

  do_cleanups (cleanup);
}

/* Helper function for get_gcc_jit_context.  dlopen the GCC front-end,
   extract the symbol that will provide a vtable and call that
   function.  Return the gcc_context that was returned.  */

static struct gcc_context *
load_libcc (void)
{
  void *handle;
  gcc_c_fe_context_function *func;

   /* gdb_dlopen and gdb_dlsym will call error () on an error, so no
      need to check value.  */
  handle = gdb_dlopen (STRINGIFY (GCC_C_FE_LIBCC));
  func = gdb_dlsym (handle, STRINGIFY (GCC_C_FE_CONTEXT));
  return (*func) (GCC_C_FE_VERSION);
}

/* Return the GCC FE context.  */

static struct gcc_context *
get_gcc_jit_context (void)
{
  static struct gcc_context *fe_context;

  if (fe_context == NULL)
    {
      fe_context = load_libcc ();
      gdb_assert (fe_context != NULL);
    }

  return fe_context;
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
add_code_header (enum gccjit_i_scope_types type, struct ui_file *buf)
{
  switch (type)
  {
  case GCCJIT_I_SIMPLE_SCOPE:
    fputs_unfiltered (GCCJIT_I_SIMPLE_HEADER, buf);
    fputs_unfiltered ("\n", buf);
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
add_code_footer (enum gccjit_i_scope_types type, struct ui_file *buf)
{
  switch (type)
  {
  case GCCJIT_I_SIMPLE_SCOPE:
    fputs_unfiltered (GCCJIT_I_SIMPLE_FOOTER, buf);
    fputs_unfiltered ("\n", buf);
    break;
  default:
    /* TODO: Error case, but do nothing for now.  */
    break;
  }
}

/* Helper process for single line expressions that will add a
   complimentary semi-colon at the end of the string if one has not
   been already specified.  Computes a few simple rules for
   dis-allowable scenarios.  */
static void
add_semicolon_if_needed (char *simple_string, struct ui_file *buf)
{
  int len = strlen (simple_string) - 1;
  if (simple_string[0] != '#' && simple_string[len] != '}'
      && simple_string[len] != ';')
      fputs_unfiltered (";", buf);
}

/* Helper function to take an expression and wrap it in a scope for
   the compiler.  CMD is populated for a multi-line expression, while
   SIMPLE_STRING is populated if the expression is on one single line.
   TYPE denotes the scope type to use.  Either CMD must be NULL and
   SIMPLE_STRING populated (the two are mutually exclusive), or
   vice-versa.  MACRO_BLOCK denotes the block relevant contextually to
   the inferior when the expression was created, and MACRO_PC
   indicates the value of $PC.  */

static char *
concat_expr_and_scope (struct command_line *cmd,
		       char *simple_string,
		       enum gccjit_i_scope_types type,
		       const struct block *macro_block,
		       CORE_ADDR macro_pc)
{
  struct command_line *iter;
  struct ui_file *buf;
  char *code;

  buf = mem_fileopen ();

  write_macro_definitions (macro_block, macro_pc, buf);
  add_code_header (type, buf);

  /* The expression was a single string, I.E. "expression z=i;".  Just
     write this directly.  */
  if (simple_string != NULL)
    {
      fputs_unfiltered (simple_string, buf);
      add_semicolon_if_needed (simple_string, buf);
      fputs_unfiltered ("\n", buf);
    }
  else if (cmd != NULL)
    {
      /* Iterate over each line of the multi-line command writing each
	 line to the buffer unaltered. */
      for (iter = cmd->body_list[0]; iter; iter = iter->next)
	{
	  fputs_unfiltered (iter->line, buf);
	  fputs_unfiltered ("\n", buf);
	}
    }

  add_code_footer (type, buf);
  code = ui_file_xstrdup (buf, NULL);
  ui_file_delete (buf);
  return code;
}

/* Get the block and PC at which to evaluate an expression.  */

static const struct block *
get_expr_block_and_pc (CORE_ADDR *pc)
{
  const struct block *block = get_selected_block (pc);

  if (block == NULL)
    {
      struct symtab_and_line cursal = get_current_source_symtab_and_line ();

      if (cursal.symtab)
	block = BLOCKVECTOR_BLOCK (BLOCKVECTOR (cursal.symtab), STATIC_BLOCK);
      if (block != NULL)
	*pc = BLOCK_START (block);
    }
  else
    *pc = BLOCK_START (block);

  return block;
}

/* Public function that is called from jit_control case in the
   expression command.  GDB returns either a CMD, or a CMD_STRING, but
   never both.  */

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string)
{
  char *code;
  char *object_file = NULL;
  struct gdb_gcc_instance *compiler;
  struct cleanup *cleanup;
  const struct block *expr_block;
  CORE_ADDR expr_pc = 0;

  expr_block = get_expr_block_and_pc (&expr_pc);

  /* Set up instance and context for the compiler.  */
  compiler = new_gdb_gcc_instance (get_gcc_jit_context (), expr_block);
  cleanup = make_cleanup_delete_gdb_gcc_instance (compiler);

  /* From the provided expression, build a scope to pass to the
     compiler.  */
  if (cmd != NULL)
    code = concat_expr_and_scope (cmd, NULL, GCCJIT_I_SIMPLE_SCOPE,
				  expr_block, expr_pc);
  else if (cmd_string != NULL)
    code = concat_expr_and_scope (NULL, cmd_string, GCCJIT_I_SIMPLE_SCOPE,
				  expr_block, expr_pc);
  else
    error (_("Neither a simple expression, or a multi-line specified."));
  make_cleanup (xfree, code);

  /* Call the compiler and start the compilation process.  */
  compiler->fe->ops->set_arguments (compiler->fe, 0, NULL);
  compiler->fe->ops->set_program_text (compiler->fe, code);
  object_file = compiler->fe->ops->compile (compiler->fe, gccjit_debug);

  if (gccjit_debug)
    {
      fprintf_unfiltered (gdb_stdout, "object file produced: %s\n\n",
			  object_file);
      fprintf_unfiltered (gdb_stdout, "debug output:\n\n%s", code);
    }

  /* Execute object code returned from compiler.  */
  if (object_file)
    gdbjit_load (object_file);

  do_cleanups (cleanup);
}

void
_initialize_gcc_jit (void)
{
  /* Right now we always have a jit.  TODO: Work out how to
     tell if currently installed GCC supports a JIT interface.  */

  add_com ("expression", class_obscure, gcc_jit_command,
#ifdef HAVE_GCC_JIT
	   _("\
Evaluate a block of C code via GCC JIT.\n\
\n\
The code block can be given as an argument, for instance:\n\
\n\
    expression printf(\"Hello world\\n\");\n\
\n\
If no argument is given, the following lines are read and used\n\
as C code.  Type a line containing \"end\" to indicate\n\
the end of the C code block.")
	   
#else /* HAVE_GCC_JIT */
	   _("\
Evaluate a block of C code via GCC JIT.\n\
\n\
GCC JIT is not supported in this copy of GDB.\n\
This command is only a placeholder.")

#endif /* HAVE_GCC_JIT */
	   );
  add_com_alias ("expr", "expression", class_obscure, 1);

  add_setshow_boolean_cmd ("gccjit", class_maintenance, &gccjit_debug, _("\
Set GCC JIT debugging."), _("\
Show GCC JIT debugging."), _("\
When on, GCC JIT debugging is enabled."),
			   NULL, show_gccjit_debug,
			   &setdebuglist, &showdebuglist);
}
