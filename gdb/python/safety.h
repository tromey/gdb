/* Wrappers for some Python safety.

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

#ifndef GDB_PYTHON_PYTHON_SAFETY_H
#define GDB_PYTHON_PYTHON_SAFETY_H

#include "py-ref.h"

template<typename T> struct result_converter;

template<>
struct result_converter<void>
{
  PyObject *operator() ()
  {
    Py_RETURN_NONE;
  }
};

template<>
struct result_converter<bool>
{
  PyObject *operator() (bool value)
  {
    if (value)
      Py_RETURN_TRUE;
    Py_RETURN_FALSE;
  }
};

template<>
struct result_converter<gdbpy_ref<>>
{
  PyObject *operator() (gdbpy_ref<> &value)
  {
    return value.release ();
  }
};

template<typename F>
struct do_wrap_noargs
{
  using result_type = decltype (F (nullptr));

  static PyObject *wrapper (PyObject *self)
  {
    try
      {
	return result_converter<result_type> (F (self));
      }
    catch (const gdb_exception &exc)
      {
	return gdbpy_handle_gdb_exception (nullptr, exc);
      }
  }
};

template<typename F>
constexpr PyMethodDef
wrap_noargs (const char *name, const char *doc)
{
  return {
    name,
    do_wrap_noargs<F>::wrapper,
    METH_NOARGS,
    doc,
  };
}

#endif /* GDB_PYTHON_PYTHON_SAFETY_H */
