/* "target valgrind"

   Copyright (C) 2020 Free Software Foundation, Inc.

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
#include "remote.h"

static const target_info valgrind_target_info = {
  "valgrind",
  N_("Valgrind convenience target using the gdb remote protocol"),
  N_("FIXME")
};

class valgrind_target : public remote_target
{
public:
  valgrind_target () = default;
  DISABLE_COPY_AND_ASSIGN (valgrind_target);

  const target_info &info () const override { return valgrind_target_info; }

  static void open (const char *, int);

  bool can_create_inferior () override { return true; }
  void create_inferior (const char *, const std::string &,
			char **, int) override;
};

void
valgrind_target::open (const char *args, int from_tty)
{
  // FIXME we want to process ARGS ourselves etc.
  // And, we don't want to call open_1 until we "run"?
  // This approach seems confused - instead we have to intercept
  // any method that can be called before "run" and make it error
  // before that time.
  open_1 (args, from_tty, 0,
	  [] () -> remote_target * { return new valgrind_target (); });
}

bool
valgrind_target::create_inferior (const char *exec_file,
				  const std::string &args,
				  char **env, int from_tty)
{
}

void _initialize_valgrind ();
void
_initialize_valgrind ()
{
  add_target (valgrind_target_info, valgrind_target::open);
}
