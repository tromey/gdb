/* DWARF DWZ handling for GDB.

   Copyright (C) 2003-2021 Free Software Foundation, Inc.

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
#include "dwarf2/dwz.h"

#include "build-id.h"
#include "debuginfod-support.h"
#include "dwarf2/leb.h"
#include "dwarf2/read.h"
#include "dwarf2/sect-names.h"
#include "filenames.h"
#include "gdb_bfd.h"
#include "gdbcore.h"
#include "gdbsupport/pathstuff.h"
#include "gdbsupport/scoped_fd.h"

const char *
dwz_file::read_string (struct objfile *objfile, LONGEST str_offset)
{
  str.read (objfile);

  if (str.buffer == NULL)
    error (_("DW_FORM_GNU_strp_alt used without .debug_str "
	     "section [in module %s]"),
	   bfd_get_filename (dwz_bfd.get ()));
  if (str_offset >= str.size)
    error (_("DW_FORM_GNU_strp_alt pointing outside of "
	     ".debug_str section [in module %s]"),
	   bfd_get_filename (dwz_bfd.get ()));
  gdb_assert (HOST_CHAR_BIT == 8);
  if (str.buffer[str_offset] == '\0')
    return NULL;
  return (const char *) (str.buffer + str_offset);
}

/* A helper function to find the sections for a .dwz file.  */

static void
locate_dwz_sections (bfd *abfd, asection *sectp, dwz_file *dwz_file)
{
  /* Note that we only support the standard ELF names, because .dwz
     is ELF-only (at the time of writing).  */
  if (dwarf2_elf_names.abbrev.matches (sectp->name))
    {
      dwz_file->abbrev.s.section = sectp;
      dwz_file->abbrev.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.info.matches (sectp->name))
    {
      dwz_file->info.s.section = sectp;
      dwz_file->info.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.str.matches (sectp->name))
    {
      dwz_file->str.s.section = sectp;
      dwz_file->str.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.line.matches (sectp->name))
    {
      dwz_file->line.s.section = sectp;
      dwz_file->line.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.macro.matches (sectp->name))
    {
      dwz_file->macro.s.section = sectp;
      dwz_file->macro.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.gdb_index.matches (sectp->name))
    {
      dwz_file->gdb_index.s.section = sectp;
      dwz_file->gdb_index.size = bfd_section_size (sectp);
    }
  else if (dwarf2_elf_names.debug_names.matches (sectp->name))
    {
      dwz_file->debug_names.s.section = sectp;
      dwz_file->debug_names.size = bfd_section_size (sectp);
    }
}

/* Like bfd_get_alt_debug_link_info, but looks at the .debug_sup
   section.  Returns true on success and fills in the out parameters;
   false on failure.  */

static bool
get_debug_sup_info (bfd *abfd,
		    gdb::unique_xmalloc_ptr<char> *filename,
		    size_t *buildid_len,
		    gdb::unique_xmalloc_ptr<bfd_byte> *buildid)
{
  asection *sect = bfd_get_section_by_name (abfd, ".debug_sup");
  if (sect == nullptr)
    return false;

  bfd_byte *contents;
  if (!bfd_malloc_and_get_section (abfd, sect, &contents))
    return false;

  gdb::unique_xmalloc_ptr<bfd_byte> content_holder (contents);
  bfd_size_type size = bfd_section_size (sect);

  /* Version of this section.  */
  if (size < 4)
    return false;
  unsigned int version = read_2_bytes (abfd, contents);
  contents += 2;
  size -= 2;
  if (version != 5)
    return false;

  /* Skip the is_supplementary value.  We already ensured there were
     enough bytes, above.  */
  ++contents;
  --size;

  /* The spec says that in the supplementary file, this must be \0,
     but it doesn't seem very important.  */
  *filename = make_unique_xstrdup ((const char *) contents);
  int len = strlen (filename->get ());
  contents += len + 1;
  size -= len + 1;

  if (size == 0)
    return false;

  unsigned int bytes_read;
  *buildid_len = read_unsigned_leb128 (abfd, contents, &bytes_read);
  contents += bytes_read;
  size -= bytes_read;

  if (size < *buildid_len)
    return false;

  if (*buildid_len != 0)
    buildid->reset ((bfd_byte *) xmemdup (contents, *buildid_len,
					  *buildid_len));

  return true;
}

/* Validate that ABFD matches the given BUILDID.  If DWARF5 is true,
   then this is done by examining the .debug_sup data.  */

static bool
verify_id (bfd *abfd, size_t len, const bfd_byte *buildid, bool dwarf5)
{
  if (!bfd_check_format (abfd, bfd_object))
    return false;

  if (dwarf5)
    {
      size_t new_len;
      gdb::unique_xmalloc_ptr<bfd_byte> new_id;
      gdb::unique_xmalloc_ptr<char> ignore;

      if (!get_debug_sup_info (abfd, &ignore, &new_len, &new_id))
	return false;
      return (len == new_len
	      && memcmp (buildid, new_id.get (), len) == 0);
    }
  else
    return build_id_verify (abfd, len, buildid);
}

/* Find either the .debug_sup or .gnu_debugaltlink section and return
   its contents.  Returns true on success and sets out parameters, or
   false if nothing is found.  */

static bool
read_alt_info (bfd *abfd, gdb::unique_xmalloc_ptr<char> *filename,
	       size_t *buildid_len,
	       gdb::unique_xmalloc_ptr<bfd_byte> *buildid,
	       bool *dwarf5)
{
  if (get_debug_sup_info (abfd, filename, buildid_len, buildid))
    {
      *dwarf5 = true;
      return true;
    }

  bfd_size_type buildid_len_arg;
  bfd_set_error (bfd_error_no_error);
  bfd_byte *buildid_out;
  filename->reset (bfd_get_alt_debug_link_info (abfd, &buildid_len_arg,
						&buildid_out));
  if (*filename == nullptr)
    {
      if (bfd_get_error () == bfd_error_no_error)
	return false;
      error (_("could not read '.gnu_debugaltlink' section: %s"),
	     bfd_errmsg (bfd_get_error ()));
    }

  *buildid_len = buildid_len_arg;
  buildid->reset (buildid_out);
  *dwarf5 = false;
  return true;
}

/* Attempt to find a .dwz file (whose full path is represented by
   FILENAME) in all of the specified debug file directories provided.

   Return the equivalent gdb_bfd_ref_ptr of the .dwz file found, or
   nullptr if it could not find anything.  */

static gdb_bfd_ref_ptr
dwz_search_other_debugdirs (std::string &filename, bfd_byte *buildid,
			    size_t buildid_len, bool dwarf5)
{
  /* Let's assume that the path represented by FILENAME has the
     "/.dwz/" subpath in it.  This is what (most) GNU/Linux
     distributions do, anyway.  */
  size_t dwz_pos = filename.find ("/.dwz/");

  if (dwz_pos == std::string::npos)
    return nullptr;

  /* This is an obvious assertion, but it's here more to educate
     future readers of this code that FILENAME at DWZ_POS *must*
     contain a directory separator.  */
  gdb_assert (IS_DIR_SEPARATOR (filename[dwz_pos]));

  gdb_bfd_ref_ptr dwz_bfd;
  std::vector<gdb::unique_xmalloc_ptr<char>> debugdir_vec
    = dirnames_to_char_ptr_vec (debug_file_directory);

  for (const gdb::unique_xmalloc_ptr<char> &debugdir : debugdir_vec)
    {
      /* The idea is to iterate over the
	 debug file directories provided by the user and
	 replace the hard-coded path in the "filename" by each
	 debug-file-directory.

	 For example, suppose that filename is:

	   /usr/lib/debug/.dwz/foo.dwz

	 And suppose that we have "$HOME/bar" as the
	 debug-file-directory.  We would then adjust filename
	 to look like:

	   $HOME/bar/.dwz/foo.dwz

	 which would hopefully allow us to find the alt debug
	 file.  */
      std::string ddir = debugdir.get ();

      if (ddir.empty ())
	continue;

      /* Make sure the current debug-file-directory ends with a
	 directory separator.  This is needed because, if FILENAME
	 contains something like "/usr/lib/abcde/.dwz/foo.dwz" and
	 DDIR is "/usr/lib/abc", then could wrongfully skip it
	 below.  */
      if (!IS_DIR_SEPARATOR (ddir.back ()))
	ddir += SLASH_STRING;

      /* Check whether the beginning of FILENAME is DDIR.  If it is,
	 then we are dealing with a file which we already attempted to
	 open before, so we just skip it and continue processing the
	 remaining debug file directories.  */
      if (filename.size () > ddir.size ()
	  && filename.compare (0, ddir.size (), ddir) == 0)
	continue;

      /* Replace FILENAME's default debug-file-directory with
	 DDIR.  */
      std::string new_filename = ddir + &filename[dwz_pos + 1];

      dwz_bfd = gdb_bfd_open (new_filename.c_str (), gnutarget);

      if (dwz_bfd == nullptr)
	continue;

      if (!verify_id (dwz_bfd.get (), buildid_len, buildid, dwarf5))
	{
	  dwz_bfd.reset (nullptr);
	  continue;
	}

      /* Found it.  */
      break;
    }

  return dwz_bfd;
}

/* See dwz.h.  */

struct dwz_file *
dwarf2_get_dwz_file (dwarf2_per_bfd *per_bfd, bool require)
{
  if (per_bfd->dwz_file != NULL)
    return per_bfd->dwz_file.get ();

  size_t buildid_len;
  gdb::unique_xmalloc_ptr<bfd_byte> buildid;
  gdb::unique_xmalloc_ptr<char> data;
  bool dwarf5;
  if (!read_alt_info (per_bfd->obfd, &data, &buildid_len, &buildid, &dwarf5))
    {
      if (!require)
	return nullptr;
      error (_("could not read '.debug_sup' or '.gnu_debugaltlink' section"));
    }

  std::string filename = data.get ();

  if (!IS_ABSOLUTE_PATH (filename.c_str ()))
    {
      gdb::unique_xmalloc_ptr<char> abs
	= gdb_realpath (bfd_get_filename (per_bfd->obfd));

      filename = ldirname (abs.get ()) + SLASH_STRING + filename;
    }

  /* First try the file name given in the section.  If that doesn't
     work, try to use the build-id instead.  */
  gdb_bfd_ref_ptr dwz_bfd (gdb_bfd_open (filename.c_str (), gnutarget));
  if (dwz_bfd != NULL)
    {
      if (!verify_id (dwz_bfd.get (), buildid_len, buildid.get (), dwarf5))
	dwz_bfd.reset (nullptr);
    }

  /* In DWARF 5, the filename in the main debug file is either
     absolute, or relative to the debug file -- so we don't search the
     system directories.  */
  if (dwz_bfd == NULL && !dwarf5)
    dwz_bfd = build_id_to_debug_bfd (buildid_len, buildid.get ());

  if (dwz_bfd == nullptr)
    {
      /* If the user has provided us with different
	 debug file directories, we can try them in order.  */
      dwz_bfd = dwz_search_other_debugdirs (filename, buildid.get (),
					    buildid_len, dwarf5);
    }

  if (dwz_bfd == nullptr)
    {
      gdb::unique_xmalloc_ptr<char> alt_filename;
      const char *origname = bfd_get_filename (per_bfd->obfd);

      scoped_fd fd (debuginfod_debuginfo_query (buildid.get (),
						buildid_len,
						origname,
						&alt_filename));

      if (fd.get () >= 0)
	{
	  /* File successfully retrieved from server.  */
	  dwz_bfd = gdb_bfd_open (alt_filename.get (), gnutarget);

	  if (dwz_bfd == nullptr)
	    warning (_("File \"%s\" from debuginfod cannot be opened as bfd"),
		     alt_filename.get ());
	  else if (!verify_id (dwz_bfd.get (), buildid_len, buildid.get (),
			       dwarf5))
	    dwz_bfd.reset (nullptr);
	}
    }

  if (dwz_bfd == NULL)
    error (_("could not find supplementary DWARF file for %s"),
	   bfd_get_filename (per_bfd->obfd));

  std::unique_ptr<struct dwz_file> result
    (new struct dwz_file (std::move (dwz_bfd)));

  for (asection *sec : gdb_bfd_sections (result->dwz_bfd))
    locate_dwz_sections (result->dwz_bfd.get (), sec, result.get ());

  gdb_bfd_record_inclusion (per_bfd->obfd, result->dwz_bfd.get ());
  per_bfd->dwz_file = std::move (result);
  return per_bfd->dwz_file.get ();
}
