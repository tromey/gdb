/* Command-line output logging for GDB, the GNU debugger.

   Copyright (C) 2003-2024 Free Software Foundation, Inc.

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

#include "cli/cli-cmds.h"
#include "ui-out.h"
#include "interps.h"
#include "cli/cli-style.h"
#include "cli/cli-decode.h"
#include "top.h"
#include "ui.h"

static bool logging_overwrite;
static std::string logging_filename = "gdb.txt";
static bool logging_redirect;
static bool debug_redirect;
static bool logging_enabled;

static void
set_logging_filename (const char *args,
		      int from_tty, struct cmd_list_element *c)
{
  current_ui->logging_filename = logging_filename;
}

static void
show_logging_filename (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  gdb_printf (file, _("The current logfile is \"%ps\".\n"),
	      styled_string (file_name_style.style (),
			     current_ui->logging_filename.c_str ()));
}

static void
set_logging_overwrite (const char *args,
		       int from_tty, struct cmd_list_element *c)
{
  current_ui->logging_overwrite = logging_overwrite;
  current_ui->maybe_warn_already_logging ();
}

static void
show_logging_overwrite (struct ui_file *file, int from_tty,
			struct cmd_list_element *c, const char *value)
{
  if (current_ui->logging_overwrite)
    gdb_printf (file, _("on: Logging overwrites the log file.\n"));
  else
    gdb_printf (file, _("off: Logging appends to the log file.\n"));
}

static void
set_logging_redirect (const char *args,
		      int from_tty, struct cmd_list_element *c)
{
  current_ui->logging_redirect = logging_redirect;
  current_ui->maybe_warn_already_logging ();
}

static void
show_logging_redirect (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  if (current_ui->logging_redirect)
    gdb_printf (file, _("on: Output will go only to the log file.\n"));
  else
    gdb_printf
      (file,
       _("off: Output will go to both the screen and the log file.\n"));
}

static void
set_logging_debug_redirect (const char *args,
			    int from_tty, struct cmd_list_element *c)
{
  current_ui->debug_redirect = debug_redirect;
  current_ui->maybe_warn_already_logging ();
}

static void
show_logging_debug_redirect (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  if (current_ui->debug_redirect)
    gdb_printf (file, _("on: Debug output will go only to the log file.\n"));
  else
    gdb_printf
      (file,
       _("off: Debug output will go to both the screen and the log file.\n"));
}

static void
set_logging_on (const char *args, int from_tty)
{
  const char *rest = args;

  if (rest && *rest)
    current_ui->logging_filename = rest;

  current_ui->handle_redirections (from_tty);
}

static void 
set_logging_off (const char *args, int from_tty)
{
  if (current_ui->saved_filename.empty ())
    return;

  current_ui->pop_output_files ();
  if (from_tty)
    gdb_printf ("Done logging to %s.\n",
		current_ui->saved_filename.c_str ());
  current_ui->saved_filename.clear ();
}

static void
set_logging_enabled (const char *args,
		     int from_tty, struct cmd_list_element *c)
{
  current_ui->logging_enabled = logging_enabled;
  if (logging_enabled)
    set_logging_on (args, from_tty);
  else
    set_logging_off (args, from_tty);
}

static void
show_logging_enabled (struct ui_file *file, int from_tty,
		       struct cmd_list_element *c, const char *value)
{
  if (current_ui->logging_enabled)
    gdb_printf (file, _("on: Logging is enabled.\n"));
  else
    gdb_printf (file, _("off: Logging is disabled.\n"));
}

void _initialize_cli_logging ();
void
_initialize_cli_logging ()
{
  static struct cmd_list_element *set_logging_cmdlist, *show_logging_cmdlist;

  /* Set/show logging.  */
  add_setshow_prefix_cmd ("logging", class_support,
			  _("Set logging options."),
			  _("Show logging options."),
			  &set_logging_cmdlist, &show_logging_cmdlist,
			  &setlist, &showlist);

  /* Set/show logging overwrite.  */
  add_setshow_boolean_cmd ("overwrite", class_support, &logging_overwrite, _("\
Set whether logging overwrites or appends to the log file."), _("\
Show whether logging overwrites or appends to the log file."), _("\
If set, logging overwrites the log file."),
			   set_logging_overwrite,
			   show_logging_overwrite,
			   &set_logging_cmdlist, &show_logging_cmdlist);

  /* Set/show logging redirect.  */
  add_setshow_boolean_cmd ("redirect", class_support, &logging_redirect, _("\
Set the logging output mode."), _("\
Show the logging output mode."), _("\
If redirect is off, output will go to both the screen and the log file.\n\
If redirect is on, output will go only to the log file."),
			   set_logging_redirect,
			   show_logging_redirect,
			   &set_logging_cmdlist, &show_logging_cmdlist);

  /* Set/show logging debugredirect.  */
  add_setshow_boolean_cmd ("debugredirect", class_support,
			   &debug_redirect, _("\
Set the logging debug output mode."), _("\
Show the logging debug output mode."), _("\
If debug redirect is off, debug will go to both the screen and the log file.\n\
If debug redirect is on, debug will go only to the log file."),
			   set_logging_debug_redirect,
			   show_logging_debug_redirect,
			   &set_logging_cmdlist, &show_logging_cmdlist);

  /* Set/show logging file.  */
  add_setshow_filename_cmd ("file", class_support, &logging_filename, _("\
Set the current logfile."), _("\
Show the current logfile."), _("\
The logfile is used when directing GDB's output."),
			    set_logging_filename,
			    show_logging_filename,
			    &set_logging_cmdlist, &show_logging_cmdlist);

  /* Set/show logging enabled.  */
  set_show_commands setshow_logging_enabled_cmds
    = add_setshow_boolean_cmd ("enabled", class_support, &logging_enabled,
			       _("Enable logging."),
			       _("Show whether logging is enabled."),
			       _("When on, enable logging."),
			       set_logging_enabled,
			       show_logging_enabled,
			       &set_logging_cmdlist, &show_logging_cmdlist);

  /* Set logging on, deprecated alias.  */
  cmd_list_element *set_logging_on_cmd
    = add_alias_cmd ("on", setshow_logging_enabled_cmds.set, class_support,
		     false, &set_logging_cmdlist);
  deprecate_cmd (set_logging_on_cmd, "set logging enabled on");
  set_logging_on_cmd->default_args = "on";

  /* Set logging off, deprecated alias.  */
  cmd_list_element *set_logging_off_cmd
    = add_alias_cmd ("off", setshow_logging_enabled_cmds.set, class_support,
		     false, &set_logging_cmdlist);
  deprecate_cmd (set_logging_off_cmd, "set logging enabled off");
  set_logging_off_cmd->default_args = "off";
}
