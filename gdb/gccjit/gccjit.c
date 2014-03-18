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
#include "dlfcn.h"
#include "gdb-dlfcn.h"
#include "gccjit-internal.h"

#define HAVE_GCC_JIT 1
#define STR(x) #x
#define STRINGIFY(x) STR(x)

struct gcc_context *fe_context = NULL;

/* The "expression" command is used to evaluate an expression that may
   contain calls to the GCC JIT interface.  TODO: Initially all we
   expect in this command is straight up C code blocks.  */

static void
gcc_jit_command (char *arg, int from_tty)
{
  struct cleanup *cleanup;

  /* When I was writing this skeleton I based it of the
     python_command.  I noticed we do some pre-python environment
     setting.  TODO: Perhaps we need ensure_gcc_cleanup.  */
 cleanup = make_cleanup_restore_integer (&interpreter_async);
 interpreter_async = 0;

 arg = skip_spaces (arg);
 if (arg && *arg)
   {
     eval_gcc_jit_command (NULL, arg);
   }
 else
   {
     struct command_line *l = get_command_line (jit_control, "");
     
     make_cleanup_free_command_lines (&l);
     execute_control_command_untraced (l);
   }
 
 do_cleanups (cleanup);
}

/* TODO: Putting this function here for now, and it will be run on
   command invocation.  This is just for convenience at the moment, but
   it should be moved elsewhere.  */
static struct gcc_context *
load_libcc (void)
{
   void *handle;
   struct gcc_context *(*func)(unsigned int);
   struct gcc_context *context;

   /* gdb_dlopen and gdb_dlsym will call error () on an error, so no
      need to check value.  */
   handle = gdb_dlopen (STRINGIFY (GCC_C_FE_LIBCC));
   func = gdb_dlsym (handle, STRINGIFY (GCC_C_FE_CONTEXT));
   return (*func) (GCC_C_FE_VERSION);
}

struct gcc_context *
get_gcc_jit_context (void)
{
  if (fe_context == NULL)
    error (_("GCC JIT context is NULL"));
  else
    return fe_context;
}

void
eval_gcc_jit_command (struct command_line *cmd, char *cmd_string)
{
  /* TODO: We don't want to implement the scripting control palaver
     (though I guess we might), so for now just error until the API
     becomes available.  I think all we want is the C text anyway. */
  error (_("This command does nothing (yet)"));
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

  fe_context = load_libcc ();
}
