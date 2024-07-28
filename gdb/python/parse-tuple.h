/* Type-safe wrapper for PyArg_ParseTupleAndKeywords

   Copyright (C) 2024 Free Software Foundation, Inc.

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

#ifndef PYTHON_PARSE_TUPLE_H
#define PYTHON_PARSE_TUPLE_H

#include "python-internal.h"

template<typename T>
struct arg_format {};

template<>
struct arg_format<gdb_py_longest>
{
  static constexpr char format = GDB_PY_LL_ARG[0];
};

template<>
struct arg_format<gdb_py_ulongest>
{
  static constexpr char format = GDB_PY_LLU_ARG[0];
};

template<>
struct arg_format<int>
{
  static constexpr char format = 'i';
};

template<>
struct arg_format<unsigned>
{
  static constexpr char format = 'I';
};

template<typename T>
struct argument
{
  constexpr argument (const char *keyword, T &var)
    : m_keyword (keyword),
      m_var (var)
  {
  }

  const char *m_keyword;
  T &m_var;

  // FIXME how can this work for "O!"?
  // maybe a pass to add a dummy object, can this be done?
  static constexpr char format = arg_format<T>::format;
};

struct remaining_arguments_optional
{
  constexpr remaining_arguments_optional () = default;

  static constexpr char format = '|';
};

template<typename... Args>
constexpr std::array<char, sizeof... (Args) + 1>
make_parse_fmt ()
{
  return { Args::format..., '\0' };
}

template<typename... Args>
constexpr std::array<const char *, sizeof... (Args) + 1>
make_parse_keywords ()
{
  // FIXME have to omit optional here
  return { Args::m_keyword..., nullptr };
}

template<typename... Args>
static inline int
gdb_PyArg_ParseTupleAndKeywords (PyObject *args, PyObject *kw,
				 Args... results)
{
  constexpr const auto fmt = make_parse_fmt<Args...> ();
  constexpr const auto keywords = make_parse_keywords<Args...> ();

  return PyArg_ParseTupleAndKeywords (args, kw, fmt.data (), keywords.data (),
				      fixme);
}

#endif /* PYTHON_PARSE_TUPLE_H */
