/* "catch exit".

   Copyright (C) 2017 Free Software Foundation, Inc.

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
#include "breakpoint.h"
#include "gdbcmd.h"
#include "target.h"
#include "inferior.h"
#include "annotate.h"
#include "valprint.h"
#include "cli/cli-utils.h"
#include "arch-utils.h"

/* The breakpoint_ops structure to be used in exit catchpoints.  */

static struct breakpoint_ops catch_exit_breakpoint_ops;

/* Implement the "insert" breakpoint_ops method for exit
   catchpoints.  */

static int
insert_catch_exit (struct bp_location *bl)
{
  return 0;
}

/* Implement the "remove" breakpoint_ops method for exit
   catchpoints.  */

static int
remove_catch_exit (struct bp_location *bl, enum remove_bp_reason reason)
{
  return 0;
}

/* Implement the "breakpoint_hit" breakpoint_ops method for exit
   catchpoints.  */

static int
breakpoint_hit_catch_exit (const struct bp_location *bl,
			   struct address_space *aspace, CORE_ADDR bp_addr,
			   const struct target_waitstatus *ws)
{
  return (ws->kind == TARGET_WAITKIND_EXITED
	  || ws->kind == TARGET_WAITKIND_SIGNALLED);
}

/* Implement the "print_it" breakpoint_ops method for exit
   catchpoints.  */

static enum print_stop_action
print_it_catch_exit (bpstat bs)
{
  struct ui_out *uiout = current_uiout;
  struct breakpoint *b = bs->breakpoint_at;
  ptid_t ptid;
  struct target_waitstatus last;

  get_last_target_status (&ptid, &last);

  annotate_catchpoint (b->number);
  if (b->disposition == disp_del)
    uiout->text ("\nTemporary catchpoint ");
  else
    uiout->text ("\nCatchpoint ");

  uiout->field_int ("bkptno", b->number);
  uiout->text (" (catch exit)\n");

  if (last.kind == TARGET_WAITKIND_SIGNALLED)
    print_signal_exited_reason (uiout, last.value.sig);
  else
    print_exited_reason (uiout, last.value.integer);

  if (uiout->is_mi_like_p ())
    uiout->field_string ("disp", bpdisp_text (b->disposition));

  return PRINT_SRC_AND_LOC;
}

/* Implement the "print_one" breakpoint_ops method for exit
   catchpoints.  */

static void
print_one_catch_exit (struct breakpoint *b, struct bp_location **last_loc)
{
  struct value_print_options opts;
  struct ui_out *uiout = current_uiout;

  get_user_print_options (&opts);
  /* Field 4, the address, is omitted (which makes the columns not
     line up too nicely with the headers, but the effect is relatively
     readable).  */
  if (opts.addressprint)
    uiout->field_skip ("addr");
  annotate_field (5);
  uiout->text ("exit");
}

/* Implement the "print_mention" breakpoint_ops method for exit
   catchpoints.  */

static void
print_mention_catch_exit (struct breakpoint *b)
{
  printf_filtered (_("Catchpoint %d (exit)"), b->number);
}

/* Implement the "print_recreate" breakpoint_ops method for exit
   catchpoints.  */

static void
print_recreate_catch_exit (struct breakpoint *b, struct ui_file *fp)
{
  fprintf_unfiltered (fp, "catch exit");
  print_recreate_thread (b, fp);
}

/* Implement the "catch exit" command.  */

static void
catch_exit_command (char *arg_entry, int from_tty,
		    struct cmd_list_element *command)
{
  const char *cond_string = NULL;
  int tempflag;

  tempflag = get_cmd_context (command) == CATCH_TEMPORARY;

  if (arg_entry != NULL)
    {
      const char *arg = skip_spaces (arg_entry);
      cond_string = ep_parse_optional_if_clause (&arg);
      arg = skip_spaces (arg);
      if (*arg != '\0')
	error (_("Junk at end of arguments."));
    }

  std::unique_ptr<breakpoint> c (new breakpoint ());
  init_catchpoint (c.get (), get_current_arch (), tempflag, cond_string,
		   &catch_exit_breakpoint_ops);
  install_breakpoint (0, std::move (c), 1);
}

static void
init_exit_catchpoint_ops (void)
{
  struct breakpoint_ops *ops;

  initialize_breakpoint_ops ();

  ops = &catch_exit_breakpoint_ops;
  *ops = base_breakpoint_ops;
  ops->insert_location = insert_catch_exit;
  ops->remove_location = remove_catch_exit;
  ops->breakpoint_hit = breakpoint_hit_catch_exit;
  ops->print_it = print_it_catch_exit;
  ops->print_one = print_one_catch_exit;
  ops->print_mention = print_mention_catch_exit;
  ops->print_recreate = print_recreate_catch_exit;
}

initialize_file_ftype _initialize_break_catch_exit;

void
_initialize_break_catch_exit (void)
{
  init_exit_catchpoint_ops ();

  add_catch_command ("exit", _("\
Catch process just before it exits.\n\
Unlike \"catch syscall exit\", this will stop if the process\n\
exits for any reason, not just due to a call to \"exit\"."),
		     catch_exit_command,
		     NULL,
		     CATCH_PERMANENT,
		     CATCH_TEMPORARY);
}
