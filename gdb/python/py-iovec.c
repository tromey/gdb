/* Python interface to BFD iovec

   Copyright (C) 2026 Free Software Foundation, Inc.

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

#include "gdb_bfd.h"
#include "gdbcore.h"
#include "python-internal.h"

struct py_iovec : public gdb_bfd_iovec_base
{
  py_iovec (gdbpy_ref<> &&file_obj, ULONGEST file_size)
    : m_file (std::move (file_obj)),
      m_size (file_size)
  {
  }

  ~py_iovec ();

  file_ptr read (bfd *abfd, void *buffer, file_ptr nbytes,
		 file_ptr offset) override;

  int stat (struct bfd *abfd, struct stat *sb) override
  {
    /* We only support the size here.  */
    memset (sb, 0, sizeof (*sb));
    sb->st_size = m_size;
    return 0;
  }

  gdbpy_ref<> m_file;
  ULONGEST m_size;
};

py_iovec::~py_iovec ()
{
  /* Make sure the GIL is held before destruction.  */
  gdbpy_enter enter_py;

  gdbpy_ref<> result = gdbpy_call_method (m_file.get (), "close");
  if (result == nullptr)
    gdbpy_print_stack ();

  m_file.reset (nullptr);
}

file_ptr
py_iovec::read (bfd *abfd, void *buffer, file_ptr nbytes, file_ptr offset)
{
  /* If this read will read all of the file, limit it to just the rest.  */
  if (offset + nbytes > m_size)
    nbytes = m_size - offset;

  /* If there are no more bytes left, we've reached EOF.  */
  if (nbytes == 0)
    return 0;

  gdbpy_enter enter_py;

  char *data;
  Py_ssize_t len;
  gdbpy_ref<> bytes = gdbpy_call_method (m_file.get (), "read",
					 (gdb_py_ulongest) offset,
					 (gdb_py_ulongest) nbytes);
  if (bytes == nullptr
      || PyBytes_AsStringAndSize (bytes.get (), &data, &len) == -1)
    {
      gdbpy_print_stack ();
      bfd_set_error (bfd_error_bad_value);
      return -1;
    }

  memcpy (buffer, data, len);
  return len;
}

gdb_bfd_ref_ptr
open_bfd_from_python (gdbpy_ref<> &&file_obj)
{
  int has_attr = PyObject_HasAttrString (file_obj.get (), "read");
  if (has_attr != 1)
    {
      /* This may return without the error set, but this scenario is
	 handled by the caller.  */
      return nullptr;
    }

  gdbpy_ref<> size_obj = gdbpy_call_method (file_obj.get (), "size");
  if (size_obj == nullptr)
    return nullptr;
  gdb_py_ulongest size = gdb_py_long_as_ulongest (size_obj.get ());
  if (PyErr_Occurred ())
    return nullptr;

  gdbpy_ref<> name_obj = gdbpy_call_method (file_obj.get (), "filename");
  if (name_obj == nullptr)
    return nullptr;
  gdb::unique_xmalloc_ptr<char> filename
    = python_string_to_host_string (name_obj.get ());
  if (filename == nullptr)
    return nullptr;

  auto iovec = std::make_unique<py_iovec> (std::move (file_obj), size);

  auto open = [&] (bfd *nbfd) -> py_iovec *
  {
    return iovec.release ();
  };

  gdb_bfd_ref_ptr result = gdb_bfd_openr_iovec (filename.get (),
						gnutarget, open);
  if (result == nullptr)
    PyErr_Format (PyExc_OSError, "could not open %s: %s",
		  filename.get (), bfd_errmsg (bfd_get_error ()));
  return result;
}
