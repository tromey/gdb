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
#include "wait.h"
#include "poll.h"



static int gccjit_debug;

static void
show_gccjit_debug (struct ui_file *file, int from_tty,
		   struct cmd_list_element *c, const char *value)
{
  fprintf_filtered (file, _("GCC JIT debugging is %s.\n"), value);
}



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
	      /* "check_for_argument" still leaves a space at the
	       beginning of the arg string.  Trim to the first
	       non-space character.  */
	      arg = skip_spaces (arg);

	      arg = gdb_abspath (arg);
	      make_cleanup (xfree, arg);

	      buffer = xstrprintf ("#include \"%s\"\n", arg);
	      make_cleanup (xfree, buffer);
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

/* Private function that is called from a child process (originating
   from a fork).  The child process call originates from the function
   eval_gcc_jit_command below.  If GCC generates a fatal signal, then
   only this child will die.  */

static void
compile_jit_expression (struct command_line *cmd, char *cmd_string,
			enum gccjit_i_scope_types scope,
		        int write_pipe)
{
  char *code;
  char *object_file = NULL;
  struct gdb_gcc_instance *compiler;
  struct cleanup *cleanup;
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

  object_file = get_new_object_name ();
  make_cleanup (xfree, object_file);
  ok = compiler->fe->ops->compile (compiler->fe, object_file, gccjit_debug);
  make_cleanup (compiler_cleanup, compiler);
  if (gccjit_debug)
    fprintf_unfiltered (gdb_stdout, "object file produced: %s\n\n",
			object_file);


  /* If GCC generated an object file, write the name of it to the
     parent process via pipe.  */
   if (ok)
     {
       ssize_t write_status ;
       int size = strlen (object_file);

       write_status = write (write_pipe, &size, sizeof (size));
       if (!write_status)
	 exit (-1);

       write_status = write (write_pipe, object_file, size);

       if (!write_status)
	 exit (-1);
    }
  /* If it did not, just exit.  */
  else
    exit (-1);

  pause ();

  do_cleanups (cleanup);
  exit (1);
}


/* Public function that is called from jit_control case in the
   expression command.  GDB returns either a CMD, or a CMD_STRING, but
   never both.  */

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string,
		      enum gccjit_i_scope_types scope)
{
  pid_t child;
  int childExitStatus;
  int pfd[2];
  char *object_file = NULL;
  volatile struct gdb_exception except;
  struct gdbjit_module gdbjit_module;
  struct cleanup *cleanups;

  if (pipe (pfd) == -1)
    error (_("Cannot create pipe for compiler process."));

  cleanups = make_cleanup_close (pfd[0]);
  make_cleanup_close (pfd[1]);

  child = fork ();

  /* Child.  */
  if (child == 0)
    compile_jit_expression (cmd, cmd_string, scope, pfd[1]);
  /* Fail.  */
  else if (child == -1)
    goto error;
  /* Parent.  */
  else
    {
      ssize_t bytes_read;
      int size;

      close (pfd[1]);

      /* For simplicity we will do two reads.  One read of an integer
	 to determine the size of the second read, which is the
	 filename.  */
      bytes_read = read (pfd[0], &size, sizeof (size));
      if (bytes_read > 0)
	{
	  object_file = xmalloc (size + 1);
	  make_cleanup (xfree, object_file);
	}
      else
	goto error;

      bytes_read = read (pfd[0], object_file, size);
      if (bytes_read != size)
	goto error;
      object_file[size] = '\0';

      /* We have the filename, so execute it.  We need to signal
	 the child to exit when completed as it is sleeping on a
	 pause () system call.  */
      if (object_file != NULL)
	{
	  TRY_CATCH (except, RETURN_MASK_ALL)
	    {
	      gdbjit_module = gdbjit_load (object_file);
	      gdbjit_run (&gdbjit_module);
	    }
	  if (except.reason < 0)
	    {
	      /* Something has gone wrong in the execution.
		 Signal the GDB with the compiler to go ahead and
		 exit and then process errors.  */
	      do_cleanups (cleanups);
	      kill (child, SIGINT);
	      waitpid (child, &childExitStatus, 0);
	      throw_exception (except);
	    }
	}

      /* Normal shutdown.  The expression has compiled, executed
	 and returned.  We now signal the GDB process that
	 compiled the expression to go ahead and exit, close the
	 pipes and reap the child process.  */
      do_cleanups (cleanups);
      kill (child, SIGINT);
      waitpid (child, &childExitStatus, 0);
      return;
    }

 error:
  do_cleanups (cleanups);
  if (child != -1)
    waitpid (child, &childExitStatus, 0);
  error (_("Unexpected error when calling compiler process"));
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
  /* TODO: The reason that the expression command has no completer for
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
