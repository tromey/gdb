/* Call module for 'expression' command.

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
#include "gdbjit-run.h"
#include "value.h"
#include "infcall.h"
#include "objfiles.h"
#include "gccjit-internal.h"
#include "dummy-frame.h"

/* Cleanup everything after the inferior function dummy frame gets
   discarded.  */

static dummy_frame_dtor_ftype do_module_cleanup;
static void
do_module_cleanup (void *arg)
{
  char *objfile_name_string = arg;
  struct objfile *objfile;

  ALL_OBJFILES (objfile)
    if ((objfile->flags & OBJF_USERLOADED) == 0
        && (strcmp (objfile_name (objfile), objfile_name_string) == 0))
      {
	free_objfile (objfile);

	/* It may be a bit too pervasive in this dummy_frame dtor callback.  */
	clear_symtab_users (0);

	break;
      }

  /* Delete the .o file.  */
  unlink (objfile_name_string);
  xfree (objfile_name_string);
}

/* Perform inferior call of MODULE.  This function may throw an error.
   This function may leave resources of MODULE allocated until the inferior
   call dummy frame is discarded.  This function may throw errors.
   Thrown errors and left MODULE resources allocation are unrelated events.
   Caller has to free the memory of MODULE after calling this function.
   Caller must not deallocate the resources MODULE points to.  */

void
gdbjit_run (const struct gdbjit_module *module)
{
  struct value *func_val;
  struct frame_id dummy_id;
  struct cleanup *cleanups;
  char *objfile_name_string;
  volatile struct gdb_exception ex;

  objfile_name_string = xstrdup (objfile_name (module->objfile));

  TRY_CATCH (ex, RETURN_MASK_ERROR)
    {
      func_val = value_from_pointer
		 (builtin_type (target_gdbarch ())->builtin_func_ptr,
		  module->func_addr);

      if (module->regs_addr == 0)
	call_function_by_hand_dummy (func_val, 0, NULL,
				     do_module_cleanup, objfile_name_string);
      else
	{
	  struct value *arg_val;

	  arg_val = value_from_pointer
		    (builtin_type (target_gdbarch ())->builtin_func_ptr,
		     module->regs_addr);
	  call_function_by_hand_dummy (func_val, 1, &arg_val,
				       do_module_cleanup, objfile_name_string);
	}
    }
  if (ex.reason < 0)
    {
      if (!find_dummy_frame_dtor (do_module_cleanup, objfile_name_string))
	do_module_cleanup (objfile_name_string);
      throw_exception (ex);
    }
}
