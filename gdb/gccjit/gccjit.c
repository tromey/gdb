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
#include "filestuff.h"
#include "completer.h"
#include "readline/tilde.h"
#include "arch-utils.h"
#include "regcache.h"
#include "value.h"



static int gccjit_debug;

static void
show_gccjit_debug (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("GCC JIT debugging is %s.\n"), value);
}



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
  enum gccjit_i_scope_types scope = GCCJIT_I_SIMPLE_SCOPE;

  cleanup = make_cleanup_restore_integer (&interpreter_async);
  interpreter_async = 0;

  arg = skip_spaces (arg);

  if (arg != NULL
      && (check_for_argument (&arg, "-raw", sizeof ("-raw") - 1)
	  || check_for_argument (&arg, "-r", sizeof ("-r") - 1)))
    {
      scope = GCCJIT_I_RAW_SCOPE;
      arg = skip_spaces (arg);
    }

  if (arg && *arg)
    {
      char *buffer;

      /* Check for arguments.  Exit if an argument string is found but
	 we don't recognize the argument. */
      if (arg[0] == '-')
	{
	  if (check_for_argument (&arg, "-file", sizeof ("-file") - 1)
	      || check_for_argument (&arg, "-f", sizeof ("-f") - 1))
	    {
	      FILE *input;
	      int file_size;

	      /* "check_for_argument" still leaves a space at the
	       beginning of the arg string.  Trim to the first
	       non-space character.  */
	      arg = skip_spaces (arg);
	      arg = tilde_expand (arg);

	      input = gdb_fopen_cloexec (arg, "r");

	      if (input == NULL)
		error (_("Cannot open \"%s\" for reading."), arg);

	      fseek (input, 0L, SEEK_END);
	      file_size = ftell (input);
	      fseek (input, 0L, SEEK_SET);
	      buffer = xmalloc (file_size + 1);
	      make_cleanup (xfree, buffer);

	      if (fread (buffer, sizeof (char), file_size, input)
		  != file_size)
		{
		  fclose (input);
		  error (_("Error reading %s"), arg);
		}

	      fclose (input);
	      buffer[file_size] = 0;
	    }
	  else
	    error(_("Unknown argument passed to command."));
	}
      else
	buffer = arg;

      eval_gcc_jit_command (NULL, buffer, scope);
    }
  else
    {
      struct command_line *l = get_command_line (jit_control, "");

      make_cleanup_free_command_lines (&l);
      l->control_u.jit.scope = scope;
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
  struct gcc_context *result;

   /* gdb_dlopen and gdb_dlsym will call error () on an error, so no
      need to check value.  */
  handle = gdb_dlopen (STRINGIFY (GCC_C_FE_LIBCC));
  func = gdb_dlsym (handle, STRINGIFY (GCC_C_FE_CONTEXT));
  result = (*func) (GCC_C_FE_VERSION);
  if (result == NULL)
    error (_("the loaded version of GCC does not support version "
	     "%d of the API"),
	   GCC_C_FE_VERSION);
  return result;
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
    fputs_unfiltered ("void "
		      GCCJIT_I_SIMPLE_FUNCNAME
		      " (struct "
		      GCCJIT_I_SIMPLE_REGISTER_STRUCT_TAG
		      " *"
		      GCCJIT_I_SIMPLE_REGISTER_ARG_NAME
		      ") {\n",
		      buf);
    break;
  case GCCJIT_I_RAW_SCOPE:
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
    fputs_unfiltered ("}\n", buf);
    break;
  case GCCJIT_I_RAW_SCOPE:
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

/* Generate a structure holding all the registers used by the function
   we're generating.  */

static void
generate_register_struct (struct ui_file *stream, struct gdbarch *gdbarch,
			  unsigned char *registers_used)
{
  int i;
  int seen = 0;

  fputs_unfiltered ("struct " GCCJIT_I_SIMPLE_REGISTER_STRUCT_TAG " {\n",
		    stream);

  if (registers_used != NULL)
    for (i = 0; i < gdbarch_num_regs (gdbarch); ++i)
      {
	if (registers_used[i])
	  {
	    struct type *regtype = register_type (gdbarch, i);

	    seen = 1;

	    fputs_unfiltered ("  ", stream);
	    type_print (regtype, gdbarch_register_name (gdbarch, i), stream,
			-1); /* FIXME? */
	    fputs_unfiltered (";\n", stream);
	  }
      }

  if (!seen)
    fputs_unfiltered ("  char " GCCJIT_I_SIMPLE_REGISTER_DUMMY ";\n", stream);

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

static char *
concat_expr_and_scope (struct command_line *cmd,
		       char *simple_string,
		       enum gccjit_i_scope_types type,
		       struct gdbarch *gdbarch,
		       const struct block *expr_block,
		       CORE_ADDR expr_pc)
{
  struct command_line *iter;
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

  add_code_header (type, buf);

  reg_code = ui_file_xstrdup (var_stream, NULL);
  make_cleanup (xfree, reg_code);
  fputs_unfiltered (reg_code, buf);

  fputs_unfiltered ("#pragma GCC user_expression\n", buf);
  fputs_unfiltered ("#line 1 \"gdb command line\"\n", buf);

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
  do_cleanups (cleanup);
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

/* Cleanup function that calls the compiler's cleanup method.  */

static void
compiler_cleanup (void *arg)
{
  struct gdb_gcc_instance *compiler = arg;

  compiler->fe->ops->cleanup (compiler->fe);
}

/* Call gdb_buildargv, set its result for S into *ARGVP but calculate also the
   number of parsed arguments into *ARGCP.  If gdb_buildargv has returned NULL
   then *ARGCP is set to zero.  */

static void
build_argc_argv (const char *s, int *argcp, char ***argvp)
{
  *argvp = gdb_buildargv (s);
  *argcp = countargv (*argvp);
}

/* String for 'set gdbjit-args' and 'show gdbjit-args'.  */
static char *gdbjit_args;

/* Parsed form of GDBJIT_ARGS.  GDBJIT_ARGS_ARGV is NULL terminated.  */
static int gdbjit_args_argc;
static char **gdbjit_args_argv;

/* Implement 'set gdbjit-args'.  */

static void
set_gdbjit_args (char *args, int from_tty, struct cmd_list_element *c)
{
  freeargv (gdbjit_args_argv);
  build_argc_argv (gdbjit_args, &gdbjit_args_argc, &gdbjit_args_argv);
}

/* Implement 'show gdbjit-args'.  */

static void
show_gdbjit_args (struct ui_file *file, int from_tty,
		  struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("JIT expression GCC command-line arguments "
			    "are \"%s\".\n"),
		    value);
}

/* Append current 'set gdbjit-args' content parsed by build_argc_argv to *ARGCP
   and *ARGVP.  *ARGVP may be NULL and also 'set gdbjit-args' may be empty.  */

static void
append_gdbjit_args (int *argcp, char ***argvp)
{
  int argi;

  *argvp = xrealloc (*argvp,
		     (*argcp + gdbjit_args_argc + 1) * sizeof (**argvp));

  for (argi = 0; argi < gdbjit_args_argc; argi++)
    (*argvp)[(*argcp)++] = xstrdup (gdbjit_args_argv[argi]);
  (*argvp)[(*argcp)] = NULL;
}

/* Return DW_AT_producer parsed for get_selected_frame () (if any) and store it
   parsed into *ARGCP and *ARGVP.  *ARGVP may be returned NULL.

   GCC already filters its command-line arguments only for the suitable ones to
   put into DW_AT_producer - see GCC function gen_producer_string.  */

static void
get_selected_pc_producer_args (int *argcp, char ***argvp)
{
  CORE_ADDR pc = get_frame_pc (get_selected_frame (NULL));
  struct symtab *symtab = find_pc_symtab (pc);
  const char *cs;

  *argcp = 0;
  *argvp = NULL;

  if (symtab == NULL || symtab->producer == NULL
      || strncmp (symtab->producer, "GNU ", strlen ("GNU ")) != 0)
    return;

  cs = symtab->producer;
  while (*cs != 0 && *cs != '-')
    cs = skip_spaces_const (skip_to_space_const (cs));
  if (*cs != '-')
    return;

  build_argc_argv (cs, argcp, argvp);
}

/* Public function that is called from jit_control case in the
   expression command.  GDB returns either a CMD, or a CMD_STRING, but
   never both.  */

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string,
		      enum gccjit_i_scope_types scope)
{
  char *code;
  char *object_file = NULL;
  struct gdb_gcc_instance *compiler;
  struct cleanup *cleanup;
  const struct block *expr_block;
  CORE_ADDR expr_pc = 0;
  int argc;
  char **argv;

  expr_block = get_expr_block_and_pc (&expr_pc);

  /* Set up instance and context for the compiler.  */
  compiler = new_gdb_gcc_instance (get_gcc_jit_context (), expr_block);
  cleanup = make_cleanup_delete_gdb_gcc_instance (compiler);

  /* From the provided expression, build a scope to pass to the
     compiler.  */
  if (cmd != NULL)
    code = concat_expr_and_scope (cmd, NULL, scope, get_current_arch (),
				  expr_block, expr_pc);
  else if (cmd_string != NULL)
    code = concat_expr_and_scope (NULL, cmd_string, scope, get_current_arch (),
				  expr_block, expr_pc);
  else
    error (_("Neither a simple expression, or a multi-line specified."));
  make_cleanup (xfree, code);

  /* Set compiler command-line arguments.  */
  get_selected_pc_producer_args (&argc, &argv);
  append_gdbjit_args (&argc, &argv);
  compiler->fe->ops->set_arguments (compiler->fe, argc, argv);
  if (gccjit_debug)
    {
      int argi;

      fprintf_unfiltered (gdb_stdout, "Passing %d compiler options:\n", argc);
      for (argi = 0; argi < argc; argi++)
	fprintf_unfiltered (gdb_stdout, "Compiler option %d: <%s>\n",
			    argi, argv[argi]);
    }
  freeargv (argv);

  /* Call the compiler and start the compilation process.  */
  compiler->fe->ops->set_program_text (compiler->fe, code);
  object_file = compiler->fe->ops->compile (compiler->fe, gccjit_debug);
  make_cleanup (compiler_cleanup, compiler);

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
     tell if currently installed GCC supports a JIT interface.

     TODO: The reason that the expression command has no completer for
     filenames is that filename_completer undergoes special treatment
     in completer.c:653.  The completion hint is massaged before the
     completion hint is passed to filename_completer, and also the
     break characters are changed from language->word_break_chars to
     gdb_completer_command_word_break_characters.  So in the case of

     jit_completer (text, word)
     {
        if (check if the -file flag was passed in, if so)
	   return filename_completer (text, word)
	else if (we return NULL, we don't complete on source)
	   return NULL
     }

     Won't work as the text and word strings are different than if
     passed to filename_completer.  A possible solution would be to
     add a case for jit_control: in completer.c.
  */

  add_com ("expression", class_obscure, gcc_jit_command,
	   _("\
Evaluate a block of C code with a compiler JIT.\n\
\n\
Usage: expression [-r|-raw] [-f|-file FILE] [CODE]\n\
-r|-raw: Suppress automatic 'void _gdb_expr () { CODE }' wrapping.\n\
-f|-file: Open the filename specified and pass the contents to\n\
the compiler.\n\
\n\
As an alternative you can provide your source code directly\n\
to the command.  For example,\n\
\n\
    expression printf(\"Hello world\\n\");\n\
\n\
If no argument is given (I.E., \"expression\" is typed with\n\
nothing after it),  an interactive prompt will be shown\n\
allowing you to enter multiple lines of source code.  Type a\n\
line containing \"end\" to indicate the end of the source code."));
  add_com_alias ("expr", "expression", class_obscure, 1);

  add_setshow_boolean_cmd ("gccjit", class_maintenance, &gccjit_debug, _("\
Set GCC JIT debugging."), _("\
Show GCC JIT debugging."), _("\
When on, GCC JIT debugging is enabled."),
			   NULL, show_gccjit_debug,
			   &setdebuglist, &showdebuglist);

  add_setshow_string_cmd ("gdbjit-args", class_support,
			  &gdbjit_args,
			  _("Set JIT expression GCC command-line arguments"),
			  _("Show JIT expression GCC command-line arguments"),
			  _("\
Use options like -I (include file directory) or ABI settings.\n\
String quoting is parsed like in shell, for example:\n\
  -mno-align-double \"-I/dir with a space/include\""),
			  set_gdbjit_args, show_gdbjit_args, &setlist, &showlist);

  // Override flags possibly coming from DW_AT_producer.
  gdbjit_args = xstrdup ("-O0 -gdwarf-4"
  // We use -fPIC to ensure that we can reference properly.  Otherwise
  // on x86-64 a string constant's address might be truncated when gdb
  // loads the object; another approach would be -mcmodel=large, but
  // -fPIC seems more portable across back ends.
			 " -fPIC"
  // We don't want warnings.
			 " -w"
  // override CU's possible -fstack-protector-strong.
			 " -fno-stack-protector"
  // FIXME: Use -std=gnu++11 when C++ JIT gets supported.
		         " -std=gnu11"
  );
  set_gdbjit_args (gdbjit_args, 0, NULL);
}
