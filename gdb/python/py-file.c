/* Python interface to target files

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

#include "python-internal.h"
#include "inferior.h"
#include "target.h"

/* These have the same values as the Python os.SEEK_* constants.
   Normally this would be bad but it seems like these are fairly well
   documented.  */
enum constants : int
{
  SET = 0,
  CUR = 1,
  END = 2,
};

struct target_file_object : public PyObject
{
  target_fd fd;
  LONGEST offset;
  LONGEST file_length;

  void close ()
  {
    if (fd != target_fd::INVALID)
      {
	fileio_error ignore;
	target_fileio_close (fd, &ignore);
      }
  }
};

static_assert (gdb::is_python_allocatable_v<target_file_object>);

extern PyTypeObject target_file_object_type;

static void
tf_dealloc (PyObject *obj)
{
  target_file_object *tf = (target_file_object *) obj;
  tf->close ();
}

static bool
check_valid (target_file_object *tf)
{
  if (tf->fd == target_fd::INVALID)
    {
      PyErr_SetString (PyExc_ValueError, "file closed");
      return false;
    }
  return true;
}

static PyObject *
tf_read (PyObject *self, PyObject *args)
{
  target_file_object *tf = (target_file_object *) self;
  if (!check_valid (tf))
    return nullptr;

  long nbytes = -1;
  if (!PyArg_ParseTuple (args, "|l", &nbytes))
    return nullptr;

  long chunk = nbytes == -1 ? 1024 : nbytes;
  gdb::byte_vector result (chunk);
  int n_read = 0;
  try
    {
      while (true)
	{
	  fileio_error err;
	  int this_read = target_fileio_pread (tf->fd, &result[n_read], chunk,
					       tf->offset, &err);
	  if (this_read == -1)
	    {
	      errno = fileio_error_to_host (err);
	      perror_with_name ("read failed");
	    }
	  n_read += this_read;
	  tf->offset += this_read;
	  if (nbytes != -1 || this_read == 0)
	    break;

	  result.resize (result.size () + chunk);
	}
    }
  catch (const gdb_exception &exc)
    {
      return gdbpy_handle_gdb_exception (nullptr, exc);
    }

  return PyBytes_FromStringAndSize ((char *) result.data (), n_read);
}

static PyObject *
tf_seek (PyObject *self, PyObject *args)
{
  target_file_object *tf = (target_file_object *) self;
  if (!check_valid (tf))
    return nullptr;

  Py_ssize_t value;
  int what = SET;
  if (!PyArg_ParseTuple (args, "n|i", &value, &what))
    return nullptr;

  LONGEST new_offset = tf->offset;
  switch (what)
    {
    case SET:
      new_offset = value;
      break;

    case CUR:
      new_offset += value;
      break;

    case END:
      new_offset = tf->file_length + value;
      break;

    default:
      PyErr_SetString (PyExc_ValueError, "invalid seek constant");
      return nullptr;
    }

  /* Check the new value.  Note we allow the new offset to exactly
     equal the file length.  */
  if (new_offset < 0 || new_offset > tf->file_length)
    {
      PyErr_SetString (PyExc_ValueError, "invalid seek offset");
      return nullptr;
    }

  tf->offset = new_offset;
  return gdb_py_object_from_ulongest (tf->offset).release ();
}

static PyObject *
tf_seekable (PyObject *self, PyObject *args)
{
  return py_true ().release ();
}

static PyObject *
tf_tell (PyObject *self, PyObject *args)
{
  target_file_object *tf = (target_file_object *) self;
  if (!check_valid (tf))
    return nullptr;
  return gdb_py_object_from_ulongest (tf->offset).release ();
}

static PyObject *
tf_close (PyObject *self, PyObject *args)
{
  target_file_object *tf = (target_file_object *) self;
  tf->close ();
  return py_none ().release ();
}

static PyObject *
tf_new (PyTypeObject *type, PyObject *args, PyObject *keywords)
{
  gdbpy_ref<target_file_object> self
    ((target_file_object *) type->tp_alloc (type, 0));
  if (self != nullptr)
    self->fd = target_fd::INVALID;
  return self.release ();
}

static int
tf_init (PyObject *self, PyObject *args, PyObject *kwargs)
{
  static const char *keywords[] = { "filename", nullptr };
  const char *filename = nullptr;

  target_file_object *tf = (target_file_object *) self;
  /* __init__ can be called twice, but we just reject it for this
     type.  */
  if (tf->fd != target_fd::INVALID)
    {
      PyErr_SetString (PyExc_ValueError,
		       "cannot call __init__ twice for gdb.TargetFile");
      return -1;
    }

  if (!gdb_PyArg_ParseTupleAndKeywords (args, kwargs, "s", keywords,
					&filename))
    return -1;

  try
    {
      fileio_error err;
      tf->fd = target_fileio_open (current_inferior (), filename,
				   FILEIO_O_RDONLY, 0, true, &err);
      if (tf->fd == target_fd::INVALID)
	{
	  errno = fileio_error_to_host (err);
	  perror_with_name ("open failed");
	}

      struct stat buf;
      if (target_fileio_fstat (tf->fd, &buf, &err))
	{
	  tf->close ();
	  errno = fileio_error_to_host (err);
	  perror_with_name ("stat failed while opening");
	}

      tf->file_length = buf.st_size;
    }
  catch (const gdb_exception &exc)
    {
      return gdbpy_handle_gdb_exception (-1, exc);
    }

  return 0;
}

static PyMethodDef target_file_object_methods[] =
{
  { "read", tf_read, METH_VARARGS, "read bytes" },
  { "seek", tf_seek, METH_VARARGS, "seek to position" },
  { "seekable", tf_seekable, METH_VARARGS, "seek to position" },
  { "tell", tf_tell, METH_NOARGS, "return current position" },
  { "close", tf_close, METH_NOARGS, "close the file" },
  { nullptr }
};

PyTypeObject target_file_object_type =
{
  PyVarObject_HEAD_INIT (NULL, 0)
  "gdb.TargetFile",		  /*tp_name*/
  sizeof (target_file_object),	  /*tp_basicsize*/
  0,				  /*tp_itemsize*/
  tf_dealloc,			  /*tp_dealloc*/
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
  "GDB remote file object",	  /* tp_doc */
  0,				  /* tp_traverse */
  0,				  /* tp_clear */
  0,				  /* tp_richcompare */
  0,				  /* tp_weaklistoffset */
  0,				  /* tp_iter */
  0,				  /* tp_iternext */
  target_file_object_methods,	  /* tp_methods */
  0,				  /* tp_members */
  0,				  /* tp_getset */
  0,				  /* tp_base */
  0,				  /* tp_dict */
  0,				  /* tp_descr_get */
  0,				  /* tp_descr_set */
  0,				  /* tp_dictoffset */
  tf_init,			  /* tp_init */
  0,				  /* tp_alloc */
  tf_new,			  /* tp_new */
};

static int
gdbpy_initialize_target_file ()
{
  return gdbpy_type_ready (&target_file_object_type);
}

GDBPY_INITIALIZE_FILE (gdbpy_initialize_target_file);
