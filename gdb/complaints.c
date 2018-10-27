/* Support for complaint handling during symbol reading in GDB.

   Copyright (C) 1990-2019 Free Software Foundation, Inc.

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
#include "complaints.h"
#include "command.h"
#include "gdbcmd.h"

/* Map format strings to counters.  */

static std::unordered_map<const char *, int> counters;

/* How many complaints about a particular thing should be printed
   before we stop whining about it?  Default is no whining at all,
   since so many systems have ill-constructed symbol files.  */

int stop_whining = 0;

/* See complaints.h.  */

void
complaint_internal (const char *key, const char *fmt, ...)
{
  va_list args;

  if (++counters[key] > stop_whining)
    return;

  va_start (args, fmt);

  if (deprecated_warning_hook)
    (*deprecated_warning_hook) (fmt, args);
  else
    {
      fputs_filtered (_("During symbol reading: "), gdb_stderr);
      vfprintf_filtered (gdb_stderr, fmt, args);
      fputs_filtered ("\n", gdb_stderr);
    }

  va_end (args);
}

/* See complaints.h.  */

void
clear_complaints ()
{
  counters.clear ();
}

static void
complaints_show_value (struct ui_file *file, int from_tty,
		       struct cmd_list_element *cmd, const char *value)
{
  fprintf_filtered (file, _("Max number of complaints about incorrect"
			    " symbols is %s.\n"),
		    value);
}



thread_local deferred_complaints *omt_complaints;

class complain_on_main_thread : public runnable
{
public:

  explicit complain_on_main_thread (std::unordered_map<const char *,
				    std::vector<std::string>> &&complaints)
    : m_complaints (std::move (complaints))
  {
  }

  void operator() () override
  {
    for (auto &iter : m_complaints)
      {
	for (auto &text : iter.second)
	  complaint_internal (iter.first, "%s", text.c_str ());
      }
  }

private:

  std::unordered_map<const char *, std::vector<std::string>> m_complaints;
};

deferred_complaints::deferred_complaints ()
  : m_max (stop_whining)
{
  gdb_assert (omt_complaints == nullptr);
  omt_complaints = this;
}

deferred_complaints::~deferred_complaints ()
{
  gdb_assert (omt_complaints == this);
  omt_complaints = nullptr;

  complain_on_main_thread *omt
    = new complain_on_main_thread (std::move (m_complaints));
  run_on_main_thread (std::unique_ptr<runnable> (omt));
}

void
deferred_complaints::complain (const char *fmt, ...)
{
  va_list args;

  auto iter = m_complaints.find (fmt);
  if (iter != m_complaints.end ())
    {
      if (iter->second.size () >= m_max)
	return;
      va_start (args, fmt);
      std::string text = string_vprintf (fmt, args);
      va_end (args);
      iter->second.push_back (std::move (text));
    }
  else
    {
      va_start (args, fmt);
      std::string text = string_vprintf (fmt, args);
      va_end (args);
      std::vector<std::string> vec;
      vec.push_back (std::move (text));
      m_complaints[fmt] = std::move (vec);
    }
}



void
_initialize_complaints (void)
{
  add_setshow_zinteger_cmd ("complaints", class_support, 
			    &stop_whining, _("\
Set max number of complaints about incorrect symbols."), _("\
Show max number of complaints about incorrect symbols."), NULL,
			    NULL, complaints_show_value,
			    &setlist, &showlist);
}
