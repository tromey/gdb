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

#ifndef GDB_PYTHON_PY_SAFETY_H
#define GDB_PYTHON_PY_SAFETY_H

#include <type_traits>
#include "py-ref.h"
#include "py-wrappers.h"
#include "charset.h"

/* This file holds wrapper templates for the various ways that gdb
   code might be exposed to Python.  These wrappers are part of gdb's
   "Python safety" approach -- utilities designed to try to prevent
   refcount problems, missing error checks, and that also remove the
   need to wrap calls into gdb in an explicit try/catch.

   See py-wrappers.h for some more discussion of this.

   Implementation methods -- the stuff you write to expose some part
   of gdb to Python -- are written in a certain style.  They will
   accept gdbpy_borrowed_ref arguments (or in some more limited
   situations, a gdbpy_opt_borrowed_ref) and return any relevant type,
   which will be automatically converted (see the 'result_converter'
   overloads below) to the correct Python type.

   Implementation methods are expected to use the wrappers in
   py-wrappers.h and not generally call into Python directly.

   Implementation details of the method-wrapping safety code are put
   into this namespace, just to emphasize that these shouldn't be used
   elsewhere.  Skip past the namespace to find the public APIs.  */
namespace safety_details
{
/* Overloads of "result_converter" are used by the safety wrappers to
   convert a function's real return value to a Python object.  A new
   reference will always be returned.  */

static inline PyObject *
result_converter (bool value)
{
  if (value)
    Py_RETURN_TRUE;
  Py_RETURN_FALSE;
}

static inline PyObject *
result_converter (LONGEST value)
{
  return gdb_py_object_from_longest (value).release ();
}

static inline PyObject *
result_converter (int value)
{
  return gdb_py_object_from_longest (value).release ();
}

static inline PyObject *
result_converter (ULONGEST value)
{
  return gdb_py_object_from_ulongest (value).release ();
}

static inline PyObject *
result_converter (const char *value)
{
  if (value == nullptr)
    Py_RETURN_NONE;
  return PyUnicode_Decode (value, strlen (value), host_charset (), nullptr);
}

static inline PyObject *
result_converter (std::string &&value)
{
  return PyUnicode_Decode (value.c_str (), value.size (),
			   host_charset (), nullptr);
}

static inline PyObject *
result_converter (gdb::unique_xmalloc_ptr<char> &&value)
{
  if (value == nullptr)
    Py_RETURN_NONE;
  return PyUnicode_Decode (value.get (), strlen (value.get ()),
			   host_charset (), nullptr);
}

static inline PyObject *
result_converter (gdbpy_ref<> &&value)
{
  return value.release ();
}

/* An instantiation of this function is used when calling a gdb method
   from Python.  It accepts some number of arguments (normally
   gdbpy_borrowed_ref or gdbpy_opt_borrowed_ref), and then then calls
   the underlying function F.  Any exceptions are caught and
   converted, and the return value of F is converted to a Python
   object as appropriate.  */
template<auto F, typename... Args>
PyObject *
wrapped_function (Args... args)
{
  try
    {
      using result_type = typename std::invoke_result_t<decltype (F), Args...>;

      if constexpr (std::is_void_v<result_type>)
	{
	  F (args...);
	  Py_RETURN_NONE;
	}
      else
	return result_converter (F (args...));
    }
  catch (const gdb_python_exception &pye)
    {
      gdb_assert (PyErr_Occurred () != nullptr);
      return nullptr;
    }
  catch (const gdb_exception &exc)
    {
      return gdbpy_handle_gdb_exception (nullptr, exc);
    }
}

/* An instantiation of this function is used when calling a gdb method
   from Python.  It accepts some number of arguments (normally
   gdbpy_borrowed_ref or gdbpy_opt_borrowed_ref), and then then calls
   the underlying function F.  Any exceptions are caught and
   converted, and the return value of F is converted to a Python
   object as appropriate.  */
template<typename Class, typename Ret, typename... Args>
PyObject *
wrapped_method (Ret (Class::*meth) (Args...), Class *self, Args... args)
{
  try
    {
      if constexpr (std::is_void_v<Ret>)
	{
	  (self->*meth) (args...);
	  Py_RETURN_NONE;
	}
      else
	return result_converter ((self->*meth) (args...));
    }
  catch (const gdb_python_exception &pye)
    {
      gdb_assert (PyErr_Occurred () != nullptr);
      return nullptr;
    }
  catch (const gdb_exception &exc)
    {
      return gdbpy_handle_gdb_exception (nullptr, exc);
    }
}

/* This is needed by wrap_varargs because the compiler will complain
   about casting a lambda to PyCFunction.  */
template<auto F>
PyObject *
varargs_wrapper (PyObject *self, PyObject *args, PyObject *kw)
{
  return wrapped_function<F> (gdbpy_borrowed_ref (self),
			      gdbpy_borrowed_ref (args),
			      gdbpy_opt_borrowed_ref (kw));
}

template<typename C, auto M>
PyObject *
varargs_wrapper (PyObject *self, PyObject *args, PyObject *kw)
{
  return wrapped_method (M, static_cast<C *> (self),
			 gdbpy_borrowed_ref (args),
			 gdbpy_opt_borrowed_ref (kw));
}

} /* namespace safety_details */

/* This is used to create the PyMethodDef for a no-argument method.
   It takes the underlying implementation function as a template
   argument, and also arguments for the method name and documentation
   string.

   The underlying function should accept a single gdbpy_borrowed_ref
   argument.  This is the 'self' argument.  The function can return
   any type (see the result_converter overloads); and should throw an
   exception on error.  If gdb_python_exception is thrown, the Python
   exception must already have been set.
*/
template<auto F>
constexpr PyMethodDef
wrap_noargs (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    [] (PyObject *self, PyObject *args) -> PyObject *
    {
      return wrapped_function<F> (gdbpy_borrowed_ref (self));
    },
    METH_NOARGS,
    doc.data (),
  };
}

template<typename C, auto M>
constexpr PyMethodDef
noargs_method (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    [] (PyObject *self, PyObject *args) -> PyObject *
    {
      return wrapped_method (M, static_cast<C *> (self));
    },
    METH_NOARGS,
    doc.data (),
  };
}

/* This is used to create the PyMethodDef for a varargs method.  It
   takes the underlying implementation function as a template
   argument, and also arguments for the method name and documentation
   string.

   The underlying function should accept a two gdbpy_borrowed_ref
   arguments (the 'self' argument and the arguments), and then a
   gdbpy_opt_borrowed_ref for the keywords.  The function can return
   any type (see the result_converter overloads); and should throw an
   exception on error.  If gdb_python_exception is thrown, the Python
   exception must already have been set.

   The gdb policy is that varargs methods must also accept keywords,
   and this is enforced here.
*/
template<auto F>
constexpr PyMethodDef
wrap_varargs (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    (PyCFunction) varargs_wrapper<F>,
    /* gdb's rule is that varargs should also use keywords.  */
    METH_VARARGS | METH_KEYWORDS,
    doc.data (),
  };
}

template<typename C, auto M>
constexpr PyMethodDef
varargs_method (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    (PyCFunction) varargs_wrapper<C, M>,
    /* gdb's rule is that varargs should also use keywords.  */
    METH_VARARGS | METH_KEYWORDS,
    doc.data (),
  };
}

/* Normally gdb requires that if a method accepts multiple arguments,
   then it should also accept keywords.  However, there are some
   exceptions to this rule.  These exceptions should use this wrapper.

   Note that this should be used sparingly.

   A typical exception is something that takes an optional argument.
   So, it may call PyArg_ParseTuple with "|s" or the like.

   The underlying function should accept two gdbpy_borrowed_ref
   arguments: 'self' and the function arguments.  */
template<auto F>
constexpr PyMethodDef
wrap_varargs_no_keywords (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    [] (PyObject *self, PyObject *args) -> PyObject *
    {
      return wrapped_function<F> (gdbpy_borrowed_ref (self),
				  gdbpy_borrowed_ref (args));
    },
    METH_VARARGS,
    doc.data (),
  };
}

/* A Python method taking a single argument.  The implementation
   function should accept two gdbpy_borrowed_ref arguments: 'self' and
   the argument to the method.  */
template<auto F>
constexpr PyMethodDef
wrap_one_arg (std::string_view name, std::string_view doc)
{
  using namespace safety_details;
  return {
    name.data (),
    [] (PyObject *self, PyObject *args) -> PyObject *
    {
      return wrapped_function<F> (gdbpy_borrowed_ref (self),
				  gdbpy_borrowed_ref (args));
    },
    METH_O,
    doc.data (),
  };
}

/* A function that wraps a "repr" or "str" method.  */
template<auto F>
PyObject *
wrap_repr (PyObject *arg)
{
  using namespace safety_details;
  return wrapped_function<F> (gdbpy_borrowed_ref (arg));
}

/* A function that wraps a "get" method.  */
template<auto F>
PyObject *
wrap_getter (PyObject *arg, void *closure)
{
  using namespace safety_details;
  /* In gdb the closure argument is never used.  */
  return wrapped_function<F> (gdbpy_borrowed_ref (arg));
}

/* A function that wraps a "set" method.  */
template<auto F>
PyObject *
wrap_setter (PyObject *arg, PyObject *value, void *closure)
{
  using namespace safety_details;
  /* In gdb the closure argument is never used.  */
  return wrapped_function<F> (gdbpy_borrowed_ref (arg),
			      gdbpy_opt_borrowed_ref (value));
}

#endif /* GDB_PYTHON_PY_SAFETY_H */
