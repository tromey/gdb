/* GDB self-testing.
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
#include "selftest.h"
#include "vec.h"

typedef self_test_function *self_test_function_ptr;

DEF_VEC_P (self_test_function_ptr);

static VEC (self_test_function_ptr) *tests;

void
register_self_test (self_test_function *function)
{
  VEC_safe_push (self_test_function_ptr, tests, function);
}

void
run_self_tests (void)
{
  int i;
  self_test_function_ptr func;

  for (i = 0; VEC_iterate (self_test_function_ptr, tests, i, func); ++i)
    {
      QUIT;

      TRY
	{
	  (*func) ();
	}
      CATCH (ex, RETURN_MASK_ERROR)
	{
	  internal_error (__FILE__, __LINE__,
			  _("self test threw exception: %s"),
			  ex.message);
	}
      END_CATCH
    }

  printf_filtered (_("Ran %u unit tests\n"),
		   VEC_length (self_test_function_ptr, tests));
}
