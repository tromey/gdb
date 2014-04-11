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
#include "gccjit-internal.h"
#include "gdbjit-load.h"
#include "frame.h"
#include "symfile.h"
#include "source.h"
#include "block.h"
#include "filestuff.h"
#include "completer.h"
#include "readline/tilde.h"
#include "arch-utils.h"
#include "value.h"
#include "gdbjit-run.h"
#include "gdb_wait.h"
#include "filestuff.h"



static int gccjit_debug;
struct cmd_list_element *compile_command_list;

static void
show_gccjit_debug (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("GCC JIT debugging is %s.\n"), value);
}



static int gccjit_fork = 1;

static void
show_gccjit_fork (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("GCC JIT fork is %s.\n"), value);
}



static int
check_raw_argument (char **arg)
{
  *arg = skip_spaces (*arg);

  if (arg != NULL
      && (check_for_argument (arg, "-raw", sizeof ("-raw") - 1)
	  || check_for_argument (arg, "-r", sizeof ("-r") - 1)))
      return 1;
  return 0;
}

/* Handle the input from the 'compile file' command.  The "compile
   file" command is used to evaluate an expression contained in a file
   that may contain calls to the GCC compiler.  */

static void
compile_file_command (char *arg, int from_tty)
{
  enum gccjit_i_scope_types scope = GCCJIT_I_SIMPLE_SCOPE;
  char *buffer;
  struct cleanup *cleanup;

  cleanup = make_cleanup_restore_integer (&interpreter_async);
  interpreter_async = 0;

  /* Check the user did not just <enter> after command.  */
  if (arg == NULL)
    error (_("You must provide a filename for this command."));

  /* Check if a raw (-r|-raw) argument is provided.  */
  if (arg != NULL && check_raw_argument (&arg))
    {
      scope = GCCJIT_I_RAW_SCOPE;
      arg = skip_spaces (arg);
    }

  /* After processing arguments, check there is a filename at the end
     of the command.  */
  if (arg[0] == '\0')
    error (_("You must provide a filename with the raw option set."));

  arg = skip_spaces (arg);
  arg = gdb_abspath (arg);
  make_cleanup (xfree, arg);
  buffer = xstrprintf ("#include \"%s\"\n", arg);
  make_cleanup (xfree, buffer);
  eval_gcc_jit_command (NULL, buffer, scope);
  do_cleanups (cleanup);
}

/* Handle the input from the 'compile code' command.  The
   "compile code" command is used to evaluate an expression that may
   contain calls to the GCC compiler.  TODO: Initially all we
   expect in this command is straight up C code blocks.  */

static void
compile_code_command (char *arg, int from_tty)
{
  struct cleanup *cleanup;
  enum gccjit_i_scope_types scope = GCCJIT_I_SIMPLE_SCOPE;

  cleanup = make_cleanup_restore_integer (&interpreter_async);
  interpreter_async = 0;

  if (arg != NULL && check_raw_argument (&arg))
    {
      scope = GCCJIT_I_RAW_SCOPE;
      arg = skip_spaces (arg);
    }

  arg = skip_spaces (arg);

  if (arg && *arg)
      eval_gcc_jit_command (NULL, arg, scope);
  else
    {
      struct command_line *l = get_command_line (jit_control, "");

      make_cleanup_free_command_lines (&l);
      l->control_u.jit.scope = scope;
      execute_control_command_untraced (l);
    }

  do_cleanups (cleanup);
}

/* A cleanup function to remove a directory and all its contents.  */

static void
do_rmdir (void *arg)
{
  char *zap = concat ("rm -rf ", arg, (char *) NULL);

  system (zap);
}

/* Return the name of the temporary directory to use for .o files, and
   arrange for the directory to be removed at shutdown.  */

static const char *
get_object_file_tempdir (void)
{
  static char *tempdir_name;

#define TEMPLATE "/tmp/gdbobj-XXXXXX"
  char tname[sizeof (TEMPLATE)];

  if (tempdir_name != NULL)
    return tempdir_name;

  strcpy (tname, TEMPLATE);
#undef TEMPLATE
  tempdir_name = mkdtemp (tname);
  if (tempdir_name == NULL)
    perror_with_name (_("could not make temporary directory"));

  tempdir_name = xstrdup (tempdir_name);
  make_final_cleanup (do_rmdir, tempdir_name);
  return tempdir_name;
}

/* Return the name of an object file.  The name is allocated by malloc
   and should be freed by the caller.  */

static char *
get_new_object_name (void)
{
  static int seq;
  const char *dir = get_object_file_tempdir ();

  ++seq;
  return xstrprintf ("%s%sout%d.o", dir, SLASH_STRING, seq);
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

/* Produce final vector of GCC compilation options.  First element is target
   size ("-m64", "-m32" etc.), optionally followed by DW_AT_producer options
   and then gdbjit-args string GDB variable.  */

static void
get_args (int *argcp, char ***argvp)
{
  int argc_producer;
  char **argv_producer;

  get_selected_pc_producer_args (&argc_producer, &argv_producer);
  *argcp = 1 + argc_producer;
  *argvp = xrealloc (argv_producer, sizeof (*argv_producer) * ((*argcp) + 1));
  memmove ((*argvp) + 1, (*argvp), sizeof (**argvp) * argc_producer);
  (*argvp)[1 + argc_producer] = NULL;
  (*argvp)[0] = gdbarch_gcc_target_options (target_gdbarch ());
  append_gdbjit_args (argcp, argvp);
}

static void
do_compile (struct gdb_gcc_instance *compiler, char *object_file)
{
  pid_t child;
  int status;

  /* Fork to do the actual compilation.  */
  if (gccjit_fork)
    child = fork ();
  else
    child = 0;

  /* Child.  Do the compile.  */
  if (child == 0)
    {
      status = compiler->fe->ops->compile (compiler->fe, object_file,
					   gccjit_debug);

      if (gccjit_fork)
	_exit (status ? 0 : 1);
    }
  /* Fail.  */
  else if (child == -1)
    error (_("Could not fork child compilation."));
  /* Parent.  */
  else
    {
      /* Just wait on the child.  TODO: I really think we should not do
  	 a blocking wait here, though there are some concerns with the
  	 robustness of WNOHANG.  The user may want to interrupt the
  	 compilation process.  Right now that interruption is ignored
  	 and GDB is blocked.  We might have to poll () for the filename
  	 and check that the child is still alive with waitpid (WNOHANG)
  	 and WIFEXITED so that we can listen for GDB interruptions
  	 too.  */
      int exit_status;

      waitpid (child, &exit_status, 0);
      status = exit_status == 0;
    }

  if (status == 0)
    error (_("Compilation failed."));
}

/* Process the compilation request.  This process sets up the context,
   args, text and calls fork to compile the result.  Returns the
   object file name on success.  On an error condition, error () is
   called.  The caller is responsible for freeing this string.  */

static char *
compile_jit_expression (struct command_line *cmd, char *cmd_string,
			enum gccjit_i_scope_types scope)
{
  char *code;
  char *object_file = NULL;
  struct gdb_gcc_instance *compiler;
  struct cleanup *cleanup, *inner_cleanup;
  const struct block *expr_block;
  CORE_ADDR trash_pc, expr_pc;
  int argc;
  char **argv;
  struct gdbjit_module gdbjit_module;
  int ok;
  struct gcc_context *context;

  expr_block = get_expr_block_and_pc (&trash_pc);
  expr_pc = get_frame_address_in_block (get_selected_frame (NULL));

  /* Set up instance and context for the compiler.  */
  if (current_language->la_get_gcc_context == NULL)
    error (_("no compiler support for this language"));
  context = current_language->la_get_gcc_context ();

  /* FIXME this seems questionable in the multi-language scheme.  */
  compiler = new_gdb_gcc_instance (context, expr_block);
  cleanup = make_cleanup_delete_gdb_gcc_instance (compiler);

  /* From the provided expression, build a scope to pass to the
     compiler.  */
  if (cmd != NULL)
    {
      struct ui_file *stream = mem_fileopen ();
      struct command_line *iter;

      make_cleanup_ui_file_delete (stream);
      for (iter = cmd->body_list[0]; iter; iter = iter->next)
	{
	  fputs_unfiltered (iter->line, stream);
	  fputs_unfiltered ("\n", stream);
	}

      code = ui_file_xstrdup (stream, NULL);
      make_cleanup (xfree, code);
    }
  else if (cmd_string != NULL)
    code = cmd_string;
  else
    error (_("Neither a simple expression, or a multi-line specified."));

  code = current_language->la_compute_program (code, scope,
					       get_current_arch (),
					       expr_block, expr_pc);
  make_cleanup (xfree, code);
  if (gccjit_debug)
    fprintf_unfiltered (gdb_stdout, "debug output:\n\n%s", code);

  /* Set compiler command-line arguments.  */
  get_args (&argc, &argv);
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

  object_file = get_new_object_name ();
  inner_cleanup = make_cleanup (xfree, object_file);
  do_compile (compiler, object_file);
  discard_cleanups (inner_cleanup);

  if (gccjit_debug)
    fprintf_unfiltered (gdb_stdout, "object file produced: %s\n\n",
			object_file);

  do_cleanups (cleanup);
  return object_file;
}


static void
compile_command (char *args, int from_tty)
{
  printf_unfiltered (_("\"compile\" must be followed by "
		       "the name of a compile command.\n"));
  help_list (compile_command_list, "compile ", -1, gdb_stdout);
}

/* Public function that is called from jit_control case in the
   expression command.  GDB returns either a CMD, or a CMD_STRING, but
   never both.  */

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string,
		      enum gccjit_i_scope_types scope)
{
  volatile struct gdb_exception except;
  struct gdbjit_module gdbjit_module;
  char *object_file;

  object_file = compile_jit_expression (cmd, cmd_string, scope);

  if (object_file != NULL)
    {
      struct cleanup *cleanup = make_cleanup (xfree, object_file);

      gdbjit_module = gdbjit_load (object_file);
      gdbjit_run (&gdbjit_module);
      do_cleanups (cleanup);
    }
}

/* See gccjit/gccjit-internal.h.  */

char *
gdbjit_register_name_mangled (struct gdbarch *gdbarch, int regnum)
{
  const char *regname = gdbarch_register_name (gdbarch, regnum);

  return xstrprintf ("__%s", regname);
}

/* See gccjit/gccjit-internal.h.  */

int
gdbjit_register_name_demangle (struct gdbarch *gdbarch, const char *regname)
{
  int regnum;

  if (regname[0] != '_' || regname[1] != '_')
    error (_("Invalid register name \"%s\"."), regname);
  regname += 2;

  for (regnum = 0; regnum < gdbarch_num_regs (gdbarch); regnum++)
    if (strcmp (regname, gdbarch_register_name (gdbarch, regnum)) == 0)
      return regnum;

  error (_("Cannot find gdbarch register \"%s\"."), regname);
}

void
_initialize_gcc_jit (void)
{
  struct cmd_list_element *c = NULL;

  add_prefix_cmd ("compile", class_obscure, compile_command,
		  _("\
Command to compile ad-hoc code and inject it into the inferior."),
		  &compile_command_list, "compile ", 1, &cmdlist);
  add_com_alias ("expression", "compile", class_obscure, 0);

  add_cmd ("code", class_obscure, compile_code_command,
	   _("\
Evaluate a block of C code.\n\
\n\
Usage: code [-r|-raw] [CODE]\n\
-r|-raw: Suppress automatic 'void _gdb_expr () { CODE }' wrapping.\n\
the compiler.\n\
\n\
As an alternative you can provide your source code directly\n\
to the command.  For example,\n\
\n\
    compile code printf(\"Hello world\\n\");\n\
\n\
If no argument is given (I.E., \"code\" is typed with\n\
nothing after it),  an interactive prompt will be shown\n\
allowing you to enter multiple lines of source code.  Type a\n\
line containing \"end\" to indicate the end of the source code."),
	   &compile_command_list);

  c = add_cmd ("file", class_obscure, compile_file_command,
	       _("\
Evaluate a file containing C code.\n\
\n\
Usage: file [-r|-raw] [filename]\n\
-r|-raw: Suppress automatic 'void _gdb_expr () { CODE }' wrapping.\n\
the compiler."),
	       &compile_command_list);
  set_cmd_completer (c, filename_completer);

  add_setshow_boolean_cmd ("gccjit", class_maintenance, &gccjit_debug, _("\
Set GCC JIT debugging."), _("\
Show GCC JIT debugging."), _("\
When on, GCC JIT debugging is enabled."),
			   NULL, show_gccjit_debug,
			   &setdebuglist, &showdebuglist);

  add_setshow_boolean_cmd ("gccjit-fork", class_maintenance, &gccjit_fork, _("\
Set whether the GCC JIT runs in a child process."), _("\
Show whether the GCC JIT runs in a child process."), _("\
When on, the GCC JIT runs in a child process."),
			   NULL, show_gccjit_fork,
			   &maintenance_set_cmdlist, &maintenance_show_cmdlist);


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
