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

#define HAVE_GCC_JIT 1
#define STR(x) #x
#define STRINGIFY(x) STR(x)

struct gcc_context *fe_context = NULL;

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

/* Return the GCC FE context.  This function operates lazily, and the
   actual dlopen and dlsym is only called once.  Other times we just
   returned the previous context.  */

struct gcc_context *
get_gcc_jit_context (void)
{
  if (fe_context == NULL)
    {
      fe_context = load_libcc ();
      gdb_assert (fe_context != NULL);
    }

  return fe_context;
}

/* Helper function to construct a header scope for a block of code.
   Takes a scope argument which selects the correct header to
   insert.  */

static char *
add_code_header (enum gccjit_i_scope_types type)
{
  char *header;

  switch (type)
  {
  case GCCJIT_I_SIMPLE_SCOPE:
    header = xmalloc (strlen (GCCJIT_I_SIMPLE_HEADER));
    strcpy (header, GCCJIT_I_SIMPLE_HEADER);
    return header;
  default:
    /* TODO: Error case, but do nothing for now.  */
    return NULL;
  }
}

/* Helper function to construct a footer scope for a block of code.
   Takes a scope argument which selects the correct footer to
   insert.  */

static char *
add_code_footer (enum gccjit_i_scope_types type)
{
  char *footer;

  switch (type)
  {
  case GCCJIT_I_SIMPLE_SCOPE:
    footer = xmalloc (strlen (GCCJIT_I_SIMPLE_FOOTER));
    strcpy (footer, GCCJIT_I_SIMPLE_FOOTER);
    return footer;
  default:
    /* TODO: Error case, but do nothing for now.  */
    return NULL;
  }
}

/* Helper function to take an expression and wrap it in a scope for
   the compiler.  CMD is populated for a multi-line expression, while
   SIMPLE_STRING is populated if the expression is on one single line.
   TYPE denotes the scope type to use.  Either CMD must be NULL and
   SIMPLE_STRING populated (the two are mutually exclusive), or
   vice-versa.  */

static char *
concat_expr_and_scope (struct command_line *cmd,
		       char *simple_string,
		       enum gccjit_i_scope_types type)
{
  struct command_line *iter;
  char *code = NULL;
  char *body = NULL;
  char *header;
  char *footer;
  int size = 0;
  int splice = 0;

  /* For development, build the string pedantically, mallocing each
     section and joining them together. Add in \n, and \0 for the body
     so strlen works.  The header and footer must already have \n\0
     included in the declarations.  */
  header = add_code_header (type);
  if (header == NULL)
    error(_("Incorrect header scope type defined."));

  footer = add_code_footer (type);
  if (footer == NULL)
    {
      xfree (header);
      error(_("Incorrect footer scope type defined."));
    }

  size = strlen (header) + strlen (footer);

  /* Annoyingly, GDB has two return modes for commands.  A multi line
     command returns a linked list of lines (a simplification, but
     that is essentially what it is), or a char * string for a single
     line.  Cope with both.  */
  if (simple_string != NULL)
    {
      /* Add space for \n\0  */
      int len = strlen (simple_string) + 2;

      body = xmalloc (len);
      strcpy (body, simple_string);
      body[len++] = '\n';
      body[len++] = '\0';
    }
  else if (cmd != NULL)
    {
      int body_size = 0;
      int location = 0;

      /* To avoid re-mallocing. just iterate through the cmd list structure
	 to get a final size for the body. Add in space for \n per
	 line, and \0 at the end.  */
      for (iter = cmd->body_list[0]; iter; iter = iter->next)
	body_size += strlen (iter->line) + 1;

      body = xmalloc (body_size + 1);

      /* Build Body. */
      for (iter = cmd->body_list[0]; iter; iter = iter->next)
	{
	  int len = strlen (iter->line);

	  strcpy (&body[location], iter->line);
	  location += len;
	  body[location++] = '\n';
	}
      body[location++] = '\0';
    }

  /* Finally, assemble the whole scope.  */
  size += strlen (body);
  code = xmalloc (size);

  /* Insert the header.  */
  strcpy (code, header);
  splice += strlen (header);
  xfree (header);

  /* Insert the body.  */
  strcpy (&code[splice], body);
  splice += strlen (body);
  xfree (body);

  /* And footer.  */
  strcpy (&code[splice],footer);
  xfree (footer);

  return code;
}


/* Public function that is called from jit_control case from the
   expression command.  GDB returns either a CMD, or a CMD_STRING, but
   never both.  */

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string)
{
  char *code;

  /* Load the context.  */
  fe_context = get_gcc_jit_context ();

  if (cmd != NULL)
    code = concat_expr_and_scope (cmd, NULL, GCCJIT_I_SIMPLE_SCOPE);
  else if (cmd_string != NULL)
    code = concat_expr_and_scope (NULL, cmd_string, GCCJIT_I_SIMPLE_SCOPE);
  else
    error(_("Neither a simple expression, or a multi-line specified."));

  /* TODO: Other compiler call backs go here.  */
  fprintf_unfiltered (gdb_stdout, "debug output:\n\n%s", code);
  xfree (code);
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
}
