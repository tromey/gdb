/* Python green thread interface.

   Copyright (C) 2022 Free Software Foundation, Inc.

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
#include "python-internal.h"
#include "green-thread.h"
#include "regcache.h"

class py_green_thread : public green_thread
{
public:

  explicit py_green_thread (ULONGEST tid, gdbpy_ref<> &&obj)
    : green_thread (tid),
      m_obj (std::move (obj))
  {
  }

  ~py_green_thread ();

  std::string get_pid_name () const override;

  ptid_t underlying_thread () const override;

  void fetch_registers (struct regcache *regcache, int regnum) const override;
  void store_registers (struct regcache *regcache, int regnum) const override;

  struct regcache *get_regcache ()
  {
    return m_regcache;
  }

private:

  /* The callback provided by the user.  */
  gdbpy_ref<> m_obj;

  /* If storing or fetching registers, the regcache to use.
     Otherwise, nullptr.  */
  mutable struct regcache *m_regcache = nullptr;
};

py_green_thread::~py_green_thread ()
{
  /* We must hold the GIL while clearing m_obj.  */
  gdbpy_enter enter_py;
  m_obj.reset (nullptr);
}

bool
py_green_thread_p (thread_info *thr)
{
  return dynamic_cast<py_green_thread *> (thr->priv.get ()) != nullptr;
}

void
py_green_thread::fetch_registers (struct regcache *regcache, int regnum) const
{
  gdbpy_enter enter_py;

  gdbpy_ref<> reg_obj;
  if (regnum == -1)
    reg_obj = gdbpy_ref<>::new_reference (Py_None);
  else
    {
      reg_obj = gdbpy_get_register_descriptor (regcache->arch (), regnum);
      if (reg_obj == nullptr)
	gdbpy_handle_exception ();
    }

  scoped_restore set_regcache = make_scoped_restore (&m_regcache, regcache);
  gdbpy_ref<> result (PyObject_CallMethod (m_obj.get (), "fetch", "O",
					   reg_obj.get ()));
  if (result == nullptr)
    gdbpy_handle_exception ();
}

void
py_green_thread::store_registers (struct regcache *regcache, int regnum) const
{
  gdbpy_enter enter_py;

  gdbpy_ref<> reg_obj;
  if (regnum == -1)
    reg_obj = gdbpy_ref<>::new_reference (Py_None);
  else
    {
      reg_obj = gdbpy_get_register_descriptor (regcache->arch (), regnum);
      if (reg_obj == nullptr)
	gdbpy_handle_exception ();
    }

  scoped_restore set_regcache = make_scoped_restore (&m_regcache, regcache);
  gdbpy_ref<> result (PyObject_CallMethod (m_obj.get (), "store", "O",
					   reg_obj.get ()));
  if (result == nullptr)
    gdbpy_handle_exception ();
}

ptid_t
py_green_thread::underlying_thread () const
{
  gdbpy_enter enter_py;

  gdbpy_ref<> result (PyObject_CallMethod (m_obj.get (),
					   "underlying_thread",
					   nullptr));
  if (result == nullptr)
    gdbpy_handle_exception ();
  if (result == Py_None)
    return null_ptid;
  thread_info *thr = thread_object_to_thread (result.get ());
  if (thr->is_green_thread ())
    error (_("underlying thread must not be another green thread"));
  return thr->ptid;
}

std::string
py_green_thread::get_pid_name () const
{
  gdbpy_enter enter_py;

  gdbpy_ref<> result (PyObject_CallMethod (m_obj.get (), "name", nullptr));
  if (result == nullptr)
    gdbpy_handle_exception ();
  gdb::unique_xmalloc_ptr<char> str = gdbpy_obj_to_string (result.get ());
  if (str == nullptr)
    gdbpy_handle_exception ();
  return std::string (str.get ());
}

static struct regcache *
get_green_regcache (thread_object *tho)
{
  THPY_REQUIRE_VALID (tho);

  py_green_thread *gth
    = dynamic_cast<py_green_thread *> (tho->thread->priv.get ());
  if (gth == nullptr)
    {
      PyErr_SetString (PyExc_RuntimeError, _("Thread is not a GreenThread"));
      return nullptr;
    }

  struct regcache *cache = gth->get_regcache ();
  if (cache == nullptr)
    {
      PyErr_SetString (PyExc_RuntimeError, _("cannot call read_register now"));
      return nullptr;
    }
  return cache;
}

/* Implementation of gdb.GreenThread.read_register (self, register) ->
   gdb.Value.  Returns the value of a register in this thread.  */

static PyObject *
green_read_register (PyObject *self, PyObject *args)
{
  PyObject *pyo_reg_id;

  if (!PyArg_UnpackTuple (args, "read_register", 1, 1, &pyo_reg_id))
    return nullptr;

  struct regcache *cache = get_green_regcache ((thread_object *) self);
  if (cache == nullptr)
    return nullptr;

  try
    {
      int regnum;
      if (!gdbpy_parse_register_id (cache->arch (), pyo_reg_id, &regnum))
	{
	  PyErr_SetString (PyExc_ValueError, "Bad register");
	  return nullptr;
	}

      struct value *val = cache->cooked_read_value (regnum);
      return value_to_value_object (val);
    }
  catch (const gdb_exception &except)
    {
      gdbpy_convert_exception (except);
      return nullptr;
    }
}

/* Implementation of gdb.GreenThread.write_register (self, register,
   value).  */

static PyObject *
green_write_register (PyObject *self, PyObject *args, PyObject *kw)
{
  static const char *keywords[] = { "register", "value", nullptr };

  PyObject *reg_obj;
  PyObject *val_obj;
  if (!gdb_PyArg_ParseTupleAndKeywords (args, kw, "OO",
					keywords, &reg_obj, &val_obj))
    return nullptr;

  struct regcache *cache = get_green_regcache ((thread_object *) self);
  if (cache == nullptr)
    return nullptr;

  try
    {
      int regnum;
      if (!gdbpy_parse_register_id (cache->arch (), reg_obj, &regnum))
	{
	  PyErr_SetString (PyExc_ValueError, "Bad register");
	  return nullptr;
	}

      struct value *val = value_object_to_value (val_obj);
      if (val == nullptr)
	return nullptr;

      int reg_size = register_size (cache->arch (), regnum);
      gdb::array_view<const gdb_byte> val_view = value_contents (val);
      if (val_view.size () > reg_size)
	val_view = val_view.slice (0, reg_size);
      cache->raw_supply_part (regnum, 0, val_view.size (),
			      val_view.data ());
      Py_RETURN_NONE;
    }
  catch (const gdb_exception &except)
    {
      gdbpy_convert_exception (except);
      return nullptr;
    }
}

static PyObject *
green_set_exited (PyObject *self, PyObject *args)
{
  thread_object *tho = (thread_object *) self;
  THPY_REQUIRE_VALID (tho);

  try
    {
      delete_thread (tho->thread);
    }
  catch (const gdb_exception &exc)
    {
      GDB_PY_HANDLE_EXCEPTION (exc);
    }
  Py_RETURN_NONE;
}

/* Implement "gdb.create_green_thread".  */

PyObject *
gdbpy_create_green_thread (PyObject *self, PyObject *args, PyObject *kw)
{
  static const char *keywords[] = { "tid", "callback", nullptr };

  ULONGEST tid;
  PyObject *callback;
  if (!gdb_PyArg_ParseTupleAndKeywords (args, kw, GDB_PY_LLU_ARG "O",
					keywords, &tid, &callback))
    return nullptr;

  if (tid == 0)
    {
      PyErr_SetString (PyExc_RuntimeError,
		       _("GreenThread cannot have TID of 0"));
      return nullptr;
    }

  try
    {
      std::unique_ptr<green_thread> new_py_thread
	(new py_green_thread (tid, gdbpy_ref<>::new_reference (callback)));
      thread_info *thr = add_green_thread (std::move (new_py_thread));
      return thread_to_thread_object (thr).release ();
    }
  catch (const gdb_exception &exc)
    {
      gdbpy_convert_exception (exc);
      return nullptr;
    }
}

/* Initialize green thread code.  */

int
gdbpy_initialize_green ()
{
  if (PyType_Ready (&green_thread_object_type) < 0)
    return -1;
  return gdb_pymodule_addobject (gdb_module, "GreenThread",
				 (PyObject *) &green_thread_object_type);
}

static PyMethodDef green_thread_object_methods[] =
{
  { "read_register", green_read_register, METH_VARARGS,
    "read_register (REGISTER) -> gdb.Value\n\
Return the value of the register in the thread." },
  { "write_register", (PyCFunction) green_write_register,
    METH_VARARGS | METH_KEYWORDS,
    "write_register (REGISTER, VALUE) -> None\n\
Write the value of the register in the thread." },
  { "set_exited", green_set_exited, METH_NOARGS,
    "set_exited () -> None\n\
Tell GDB that this thread has exited." },
  { nullptr }
};

PyTypeObject green_thread_object_type =
{
  PyVarObject_HEAD_INIT (NULL, 0)
  "gdb.GreenThread",		  /*tp_name*/
  sizeof (thread_object),	  /*tp_basicsize*/
  0,				  /*tp_itemsize*/
  thpy_dealloc,			  /*tp_dealloc*/
  0,				  /*tp_print*/
  0,				  /*tp_getattr*/
  0,				  /*tp_setattr*/
  0,				  /*tp_compare*/
  0,				  /*tp_repr*/
  0,				  /*tp_as_number*/
  0,				  /*tp_as_sequence*/
  0,				  /*tp_as_mapping*/
  0,				  /*tp_hash */
  0,				  /*tp_call*/
  0,				  /*tp_str*/
  0,				  /*tp_getattro*/
  0,				  /*tp_setattro*/
  0,				  /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT,		  /*tp_flags*/
  "GDB green thread object",	  /* tp_doc */
  0,				  /* tp_traverse */
  0,				  /* tp_clear */
  0,				  /* tp_richcompare */
  0,				  /* tp_weaklistoffset */
  0,				  /* tp_iter */
  0,				  /* tp_iternext */
  green_thread_object_methods,	  /* tp_methods */
  0,				  /* tp_members */
  0,				  /* tp_getset */
  0,				  /* tp_base */
  0,				  /* tp_dict */
  0,				  /* tp_descr_get */
  0,				  /* tp_descr_set */
  0,				  /* tp_dictoffset */
  0,				  /* tp_init */
  0				  /* tp_alloc */
};
