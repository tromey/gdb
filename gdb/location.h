/* Data structures and API for event locations in GDB.
   Copyright (C) 2013-2020 Free Software Foundation, Inc.

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

#ifndef LOCATION_H
#define LOCATION_H

#include "symtab.h"

struct language_defn;
struct event_location;

/* An enumeration of possible signs for a line offset.  */

enum offset_relative_sign
{
  /* No sign  */
  LINE_OFFSET_NONE,

  /* A plus sign ("+")  */
  LINE_OFFSET_PLUS,

  /* A minus sign ("-")  */
  LINE_OFFSET_MINUS,

  /* A special "sign" for unspecified offset.  */
  LINE_OFFSET_UNKNOWN
};

/* A line offset in a location.  */

struct line_offset
{
  /* Line offset and any specified sign.  */
  int offset;
  enum offset_relative_sign sign;
};

/* An enumeration of the various ways to specify a stop event
   location (used with create_breakpoint).  */

enum event_location_type
{
  /* A traditional linespec.  */
  LINESPEC_LOCATION,

  /* An address in the inferior.  */
  ADDRESS_LOCATION,

  /* An explicit location.  */
  EXPLICIT_LOCATION,

  /* A probe location.  */
  PROBE_LOCATION
};

/* A traditional linespec.  */

struct linespec_location
{
  /* Whether the function name is fully-qualified or not.  */
  symbol_name_match_type match_type;

  /* The linespec.  */
  char *spec_string;
};

/* A unique pointer for event_location.  */
typedef std::unique_ptr<event_location> event_location_up;

/* An explicit location.  This structure is used to bypass the
   parsing done on linespecs.  It still has the same requirements
   as linespecs, though.  For example, source_filename requires
   at least one other field.  */

struct explicit_location
{
  /* The source filename. Malloc'd.  */
  char *source_filename = nullptr;

  /* The function name.  Malloc'd.  */
  char *function_name = nullptr;

  /* Whether the function name is fully-qualified or not.  */
  symbol_name_match_type func_name_match_type = symbol_name_match_type::WILD;

  /* The name of a label.  Malloc'd.  */
  char *label_name = nullptr;

  /* A line offset relative to the start of the symbol
     identified by the above fields or the current symtab
     if the other fields are NULL.  */
  struct line_offset line_offset = {0, LINE_OFFSET_UNKNOWN};
};

/* An event location used to set a stop event in the inferior.
   This structure is the base class for the various ways
   to specify where a stop event should be set.  */

struct event_location
{
  explicit event_location (enum event_location_type type)
    : m_type (type)
  {
  }

  event_location (enum event_location_type type,
		  gdb::unique_xmalloc_ptr<char> &&name)
    : m_type (type),
      m_as_string (std::move (name))
  {
  }

  event_location (const event_location &other)
    : m_type (other.m_type)
  {
    if (other.m_as_string != nullptr)
      m_as_string = make_unique_xstrdup (other.m_as_string.get ());
  }

  virtual ~event_location () = default;

  virtual event_location_up clone () const = 0;

  virtual bool empty_p () const = 0;

  const char *to_string () const
  {
    if (m_as_string == nullptr)
      m_as_string = compute_name ();
    return m_as_string.get ();
  }

  void set_string (gdb::unique_xmalloc_ptr<char> &&str)
  {
    m_as_string = std::move (str);
  }

  enum event_location_type type () const
  {
    return m_type;
  }

protected:

  virtual gdb::unique_xmalloc_ptr<char> compute_name () const = 0;

  char *maybe_copy (const char *s)
  {
    return s == nullptr ? nullptr : xstrdup (s);
  }

private:

  /* The type of this breakpoint specification.  */
  enum event_location_type m_type;

  /* Cached string representation of this location.  This is used, e.g., to
     save stop event locations to file.  Malloc'd.  */
  mutable gdb::unique_xmalloc_ptr<char> m_as_string;
};

/* A "normal" linespec.  */
struct linespec_location_internal : public event_location
{
  linespec_location_internal (const char **linespec,
			      symbol_name_match_type match_type);

  linespec_location_internal (const linespec_location_internal &other)
    : event_location (other)
  {
    m_linespec_location.match_type = other.m_linespec_location.match_type;
    m_linespec_location.spec_string
      = maybe_copy (other.m_linespec_location.spec_string);
  }

  ~linespec_location_internal () override
  {
    xfree (m_linespec_location.spec_string);
  }

  event_location_up clone () const override
  {
    return event_location_up (new linespec_location_internal (*this));
  }

  bool empty_p () const override
  {
    /* Linespecs are never "empty."  (NULL is a valid linespec)  */
    return true;
  }

  const linespec_location *get_location () const
  {
    return &m_linespec_location;
  }

protected:

  gdb::unique_xmalloc_ptr<char> compute_name () const override
  {
    if (m_linespec_location.spec_string != nullptr)
      {
	if (m_linespec_location.match_type == symbol_name_match_type::FULL)
	  return (gdb::unique_xmalloc_ptr<char>
		  (concat ("-qualified ", m_linespec_location.spec_string,
			   (char *) NULL)));
	else
	  return make_unique_xstrdup (m_linespec_location.spec_string);
      }
    return {};
  }

private:

  struct linespec_location m_linespec_location {};
};

/* A probe.  */
struct probe_location : public event_location
{
  explicit probe_location (const char *name)
    : event_location (PROBE_LOCATION, make_unique_xstrdup (name))
  {
  }

  probe_location (const probe_location &other) = default;

  event_location_up clone () const override
  {
    return event_location_up (new probe_location (*this));
  }

  bool empty_p () const override
  {
    return to_string () == nullptr;
  }

protected:

  gdb::unique_xmalloc_ptr<char> compute_name () const override
  {
    gdb_assert_not_reached (_("name should be non-null by construction"));
  }
};

/* An address in the inferior.  */
struct address_location : public event_location
{
  address_location (CORE_ADDR addr, gdb::unique_xmalloc_ptr<char> &&name)
    : event_location (ADDRESS_LOCATION, std::move (name)),
      m_address (addr)
  {
  }

  address_location (const address_location &other) = default;

  event_location_up clone () const override
  {
    return event_location_up (new address_location (*this));
  }

  bool empty_p () const override
  {
    return false;
  }

  CORE_ADDR address () const
  {
    return m_address;
  }

protected:

  gdb::unique_xmalloc_ptr<char> compute_name () const override
  {
    return (gdb::unique_xmalloc_ptr<char>
	    (xstrprintf ("*%s", core_addr_to_string (m_address))));
  }

private:

  CORE_ADDR m_address;
};

/* An explicit location.  */
struct explicit_location_internal : public event_location
{
  explicit explicit_location_internal
    (const struct explicit_location *explicit_loc)
      : event_location (EXPLICIT_LOCATION)
  {
    if (explicit_loc != nullptr)
      {
	m_explicit_loc.func_name_match_type
	  = explicit_loc->func_name_match_type;

	m_explicit_loc.source_filename
	  = maybe_copy (explicit_loc->source_filename);

	m_explicit_loc.function_name
	  = maybe_copy (explicit_loc->function_name);

	m_explicit_loc.label_name
	  = maybe_copy (explicit_loc->label_name);

	m_explicit_loc.line_offset = explicit_loc->line_offset;
      }
  }

  explicit_location_internal (const explicit_location_internal &other)
    : event_location (other)
  {
    m_explicit_loc.source_filename
      = maybe_copy (other.m_explicit_loc.source_filename);
    m_explicit_loc.function_name
      = maybe_copy (other.m_explicit_loc.function_name);
    m_explicit_loc.func_name_match_type
      = other.m_explicit_loc.func_name_match_type;
    m_explicit_loc.label_name
      = maybe_copy (other.m_explicit_loc.label_name);
    m_explicit_loc.line_offset = other.m_explicit_loc.line_offset;
  }

  ~explicit_location_internal () override
  {
    xfree (m_explicit_loc.source_filename);
    xfree (m_explicit_loc.function_name);
    xfree (m_explicit_loc.label_name);
  }

  event_location_up clone () const override
  {
    return event_location_up (new explicit_location_internal (*this));
  }

  bool empty_p () const override
  {
    return (m_explicit_loc.source_filename == NULL
	    && m_explicit_loc.function_name == NULL
	    && m_explicit_loc.label_name == NULL
	    && m_explicit_loc.line_offset.sign == LINE_OFFSET_UNKNOWN);
  }

  const explicit_location *get_location () const
  {
    return &m_explicit_loc;
  }

  explicit_location *get_location ()
  {
    return &m_explicit_loc;
  }

protected:

  gdb::unique_xmalloc_ptr<char> compute_name () const override;

private:

  struct explicit_location m_explicit_loc;
};

/* Return a malloc'd linespec string representation of the given
   explicit location.  The location must already be canonicalized/valid.  */

extern gdb::unique_xmalloc_ptr<char>
  explicit_location_to_linespec (const struct explicit_location *explicit_loc);

/* Return the linespec location of the given event_location (which
   must be of type LINESPEC_LOCATION).  */

extern const linespec_location *
  get_linespec_location (const struct event_location *location);

/* Return the address location (a CORE_ADDR) of the given event_location
   (which must be of type ADDRESS_LOCATION).  */

extern CORE_ADDR
  get_address_location (const struct event_location *location);

/* Return the expression (a string) that was used to compute the address
   of the given event_location (which must be of type ADDRESS_LOCATION).  */

extern const char *
  get_address_string_location (const struct event_location *location);

/* Return the probe location (a string) of the given event_location
   (which must be of type PROBE_LOCATION).  */

extern const char *
  get_probe_location (const struct event_location *location);

/* Return the explicit location of the given event_location
   (which must be of type EXPLICIT_LOCATION).  */

extern struct explicit_location *
  get_explicit_location (struct event_location *location);

/* A const version of the above.  */

extern const struct explicit_location *
  get_explicit_location_const (const struct event_location *location);

/* Attempt to convert the input string in *ARGP into an event_location.
   ARGP is advanced past any processed input.  Returns an event_location
   (malloc'd) if an event location was successfully found in *ARGP,
   NULL otherwise.

   This function may call error() if *ARGP looks like properly formed,
   but invalid, input, e.g., if it is called with missing argument parameters
   or invalid options.

   This function is intended to be used by CLI commands and will parse
   explicit locations in a CLI-centric way.  Other interfaces should use
   string_to_event_location_basic if they want to maintain support for
   legacy specifications of probe, address, and linespec locations.

   MATCH_TYPE should be either WILD or FULL.  If -q/--qualified is specified
   in the input string, it will take precedence over this parameter.  */

extern event_location_up string_to_event_location
  (const char **argp, const struct language_defn *language,
   symbol_name_match_type match_type = symbol_name_match_type::WILD);

/* Like string_to_event_location, but does not attempt to parse
   explicit locations.  MATCH_TYPE indicates how function names should
   be matched.  */

extern event_location_up
  string_to_event_location_basic (const char **argp,
				  const struct language_defn *language,
				  symbol_name_match_type match_type);

/* Structure filled in by string_to_explicit_location to aid the
   completer.  */
struct explicit_completion_info
{
  /* Pointer to the last option found.  E.g., in "b -sou src.c -fun
     func", LAST_OPTION is left pointing at "-fun func".  */
  const char *last_option = NULL;

  /* These point to the start and end of a quoted argument, iff the
     last argument was quoted.  If parsing finds an incomplete quoted
     string (e.g., "break -function 'fun"), then QUOTED_ARG_START is
     set to point to the opening \', and QUOTED_ARG_END is left NULL.
     If the last option is not quoted, then both are set to NULL. */
  const char *quoted_arg_start = NULL;
  const char *quoted_arg_end = NULL;

  /* True if we saw an explicit location option, as opposed to only
     flags that affect both explicit locations and linespecs, like
     "-qualified".  */
  bool saw_explicit_location_option = false;
};

/* Attempt to convert the input string in *ARGP into an explicit location.
   ARGP is advanced past any processed input.  Returns an event_location
   (malloc'd) if an explicit location was successfully found in *ARGP,
   NULL otherwise.

   If COMPLETION_INFO is NULL, this function may call error() if *ARGP
   looks like improperly formed input, e.g., if it is called with
   missing argument parameters or invalid options.  If COMPLETION_INFO
   is not NULL, this function will not throw any exceptions.  */

extern event_location_up
  string_to_explicit_location (const char **argp,
			       const struct language_defn *language,
			       explicit_completion_info *completion_info);

#endif /* LOCATION_H */
