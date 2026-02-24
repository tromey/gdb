/* Python interface to objfiles.

   Copyright (C) 2008-2026 Free Software Foundation, Inc.

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
#include "charset.h"
#include "objfiles.h"
#include "language.h"
#include "build-id.h"
#include "symtab.h"
#include "python.h"
#include "inferior.h"

struct objfile_object : public gdbpy_dict_wrapper
{
  /* The corresponding objfile.  */
  struct objfile *objfile;

  /* The pretty-printer list of functions.  */
  PyObject *printers;

  /* The frame filter list of functions.  */
  PyObject *frame_filters;

  /* The list of frame unwinders.  */
  PyObject *frame_unwinders;

  /* The type-printer list.  */
  PyObject *type_printers;

  /* The debug method matcher list.  */
  PyObject *xmethods;

  /* Throw exception if this wrapper is invalid.  */
  void require_valid ()
  {
    if (!is_valid ())
      gdbpy_err_set_string (PyExc_RuntimeError,
			    _("Objfile no longer exists."));
  }

  /* Implementation of gdb.Objfile.is_valid (self) -> Boolean.
     Returns True if this object file still exists in GDB.  */
  bool is_valid ()
  {
    return objfile != nullptr;
  }

  gdbpy_ref<> get_printers ()
  {
    return gdbpy_ref<>::new_reference (printers);
  }

  /* Return the Python dictionary attribute containing frame filters
     for this object file.  */
  gdbpy_ref<> get_frame_filters ()
  {
    return gdbpy_ref<>::new_reference (frame_filters);
  }

  /* Return the frame unwinders attribute for this object file.  */
  gdbpy_ref<> get_frame_unwinders ()
  {
    return gdbpy_ref<>::new_reference (frame_unwinders);
  }

  /* Get the 'type_printers' attribute.  */
  gdbpy_ref<> get_type_printers ()
  {
    return gdbpy_ref<>::new_reference (type_printers);
  }

  /* Get the 'xmethods' attribute.  */
  gdbpy_ref<> get_xmethods ()
  {
    return gdbpy_ref<>::new_reference (xmethods);
  }

  /* An Objfile method which returns the objfile's file name, or
     NULL.  */
  const char *get_filename ();

  /* An Objfile method which returns the objfile's file name, as
     specified by the user, or NULL.  */
  const char *get_username ();

  /* An Objfile method which returns the objfile's progspace, or
     None.  */
  gdbpy_ref<> get_progspace ();

  /* Get the 'is_file' attribute.  */
  gdbpy_ref<> is_file ();

  /* Get the 'owner' attribute.  */
  struct objfile *get_owner ();

  /* Get the 'build_id' attribute.  */
  gdbpy_ref<> get_build_id ();

  /* Implement 'repr.  */
  gdbpy_ref<> repr ();

  /* Method to add a separate debug file.  */
  void add_separate_debug_file (gdbpy_borrowed_ref<> args,
				gdbpy_opt_borrowed_ref<> kw);

  /* Method to look up global symbol.  */
  symbol *lookup_global_symbol (gdbpy_borrowed_ref<> args,
				gdbpy_opt_borrowed_ref<> kw);

  /* Method to look up static symbol.  */
  symbol *lookup_static_symbol (gdbpy_borrowed_ref<> args,
				gdbpy_opt_borrowed_ref<> kw);

  static PyTypeObject *corresponding_object_type;
};

extern PyTypeObject objfile_object_type;

/* Clear the OBJFILE pointer in an Objfile object and remove the
   reference.  */
struct objfpy_deleter
{
  void operator() (objfile_object *obj)
  {
    gdbpy_enter enter_py;
    gdbpy_ref<objfile_object> object (obj);
    object->objfile = nullptr;
  }
};

static const registry<objfile>::key<objfile_object, objfpy_deleter>
     objfpy_objfile_data_key;



const char *
objfile_object::get_filename ()
{
  if (objfile != nullptr)
    return objfile_name (objfile);
  return nullptr;
}

const char *
objfile_object::get_username ()
{
  if (objfile != nullptr)
    return objfile->original_name;
  return nullptr;
}

gdbpy_ref<>
objfile_object::is_file ()
{
  if (objfile != nullptr)
    return gdbpy_bool_from_long ((objfile->flags & OBJF_NOT_FILENAME) == 0);
  return gdbpy_ref<>::new_reference (Py_None);
}

/* If SELF is a separate debug-info file, return the "backlink" field.
   Otherwise return None.  */

struct objfile *
objfile_object::get_owner ()
{
  require_valid ();
  return objfile->separate_debug_objfile_backlink;
}

/* An Objfile method which returns the objfile's build id, or None.  */

gdbpy_ref<>
objfile_object::get_build_id ()
{
  require_valid ();

  const struct bfd_build_id *build_id
    = build_id_bfd_get (objfile->obfd.get ());

  if (build_id == nullptr)
    return py_none ();

  std::string hex_form = bin2hex (build_id->data, build_id->size);
  return host_string_to_python_string (hex_form.c_str ());
}

gdbpy_ref<>
objfile_object::get_progspace ()
{
  if (objfile != nullptr)
    return pspace_to_pspace_object (objfile->pspace ());
  return gdbpy_ref<>::new_reference (Py_None);
}

static void
objfpy_dealloc (PyObject *o)
{
  objfile_object *self = (objfile_object *) o;

  Py_XDECREF (self->dict);
  Py_XDECREF (self->printers);
  Py_XDECREF (self->frame_filters);
  Py_XDECREF (self->frame_unwinders);
  Py_XDECREF (self->type_printers);
  Py_XDECREF (self->xmethods);
  Py_TYPE (self)->tp_free (self);
}

/* Initialize an objfile_object.
   The result is a boolean indicating success.  */

static bool
objfpy_initialize (gdbpy_ref<objfile_object> &self)
{
  if (!self->allocate_dict ())
    return false;

  self->objfile = NULL;

  self->printers = PyList_New (0);
  if (self->printers == NULL)
    return false;

  self->frame_filters = PyDict_New ();
  if (self->frame_filters == NULL)
    return false;

  self->frame_unwinders = PyList_New (0);
  if (self->frame_unwinders == NULL)
    return false;

  self->type_printers = PyList_New (0);
  if (self->type_printers == NULL)
    return false;

  self->xmethods = PyList_New (0);
  if (self->xmethods == NULL)
    return false;

  return true;
}

static PyObject *
objfpy_new (PyTypeObject *type, PyObject *args, PyObject *keywords)
{
  gdbpy_ref<objfile_object> self ((objfile_object *) type->tp_alloc (type, 0));

  if (self != nullptr && !objfpy_initialize (self))
    return nullptr;

  return self.release ();
}

gdbpy_ref<>
objfpy_get_printers (gdbpy_borrowed_ref<> o)
{
  objfile_object *self = o;
  return self->get_printers ();
}

static int
objfpy_set_printers (PyObject *o, PyObject *value, void *ignore)
{
  objfile_object *self = (objfile_object *) o;

  if (! value)
    {
      PyErr_SetString (PyExc_TypeError,
		       _("Cannot delete the pretty_printers attribute."));
      return -1;
    }

  if (! PyList_Check (value))
    {
      PyErr_SetString (PyExc_TypeError,
		       _("The pretty_printers attribute must be a list."));
      return -1;
    }

  /* Take care in case the LHS and RHS are related somehow.  */
  gdbpy_ref<> tmp (self->printers);
  Py_INCREF (value);
  self->printers = value;

  return 0;
}

/* Set this object file's frame filters dictionary to FILTERS.  */
static int
objfpy_set_frame_filters (PyObject *o, PyObject *filters, void *ignore)
{
  objfile_object *self = (objfile_object *) o;

  if (! filters)
    {
      PyErr_SetString (PyExc_TypeError,
		       _("Cannot delete the frame filters attribute."));
      return -1;
    }

  if (! PyDict_Check (filters))
    {
      PyErr_SetString (PyExc_TypeError,
		       _("The frame_filters attribute must be a dictionary."));
      return -1;
    }

  /* Take care in case the LHS and RHS are related somehow.  */
  gdbpy_ref<> tmp (self->frame_filters);
  Py_INCREF (filters);
  self->frame_filters = filters;

  return 0;
}

/* Set this object file's frame unwinders list to UNWINDERS.  */

static int
objfpy_set_frame_unwinders (PyObject *o, PyObject *unwinders, void *ignore)
{
  objfile_object *self = (objfile_object *) o;

  if (!unwinders)
    {
      PyErr_SetString (PyExc_TypeError,
		       _("Cannot delete the frame unwinders attribute."));
      return -1;
    }

  if (!PyList_Check (unwinders))
    {
      PyErr_SetString (PyExc_TypeError,
		       _("The frame_unwinders attribute must be a list."));
      return -1;
    }

  /* Take care in case the LHS and RHS are related somehow.  */
  gdbpy_ref<> tmp (self->frame_unwinders);
  Py_INCREF (unwinders);
  self->frame_unwinders = unwinders;

  return 0;
}

/* Get the 'xmethods' attribute.  */

gdbpy_ref<>
objfpy_get_xmethods (gdbpy_borrowed_ref<> o)
{
  objfile_object *self = o;
  return self->get_xmethods ();
}

/* Set the 'type_printers' attribute.  */

static int
objfpy_set_type_printers (PyObject *o, PyObject *value, void *ignore)
{
  objfile_object *self = (objfile_object *) o;

  if (! value)
    {
      PyErr_SetString (PyExc_TypeError,
		       _("Cannot delete the type_printers attribute."));
      return -1;
    }

  if (! PyList_Check (value))
    {
      PyErr_SetString (PyExc_TypeError,
		       _("The type_printers attribute must be a list."));
      return -1;
    }

  /* Take care in case the LHS and RHS are related somehow.  */
  gdbpy_ref<> tmp (self->type_printers);
  Py_INCREF (value);
  self->type_printers = value;

  return 0;
}

/* Implementation of gdb.Objfile.add_separate_debug_file (self, string). */

void
objfile_object::add_separate_debug_file (gdbpy_borrowed_ref<> args,
					 gdbpy_opt_borrowed_ref<> kw)
{
  require_valid ();

  static const char *keywords[] = { "file_name", NULL };
  const char *file_name;

  gdbpy_arg_parse_tuple_and_keywords (args, kw, "s", keywords, &file_name);

  gdb_bfd_ref_ptr abfd (symfile_bfd_open (file_name));
  symbol_file_add_separate (abfd, file_name, 0, objfile);
}

/* Implementation of
  gdb.Objfile.lookup_global_symbol (self, string [, domain]) -> gdb.Symbol.  */

symbol *
objfile_object::lookup_global_symbol (gdbpy_borrowed_ref<> args,
				      gdbpy_opt_borrowed_ref<> kw)
{
  require_valid ();

  static const char *keywords[] = { "name", "domain", NULL };
  const char *symbol_name;
  int domain = VAR_DOMAIN;

  gdbpy_arg_parse_tuple_and_keywords (args, kw, "s|i", keywords, &symbol_name,
				      &domain);

  domain_search_flags flags = from_scripting_domain (domain);
  return lookup_global_symbol_from_objfile (objfile, GLOBAL_BLOCK,
					    symbol_name, flags).symbol;
}

/* Implementation of
  gdb.Objfile.lookup_static_symbol (self, string [, domain]) -> gdb.Symbol.  */

symbol *
objfile_object::lookup_static_symbol (gdbpy_borrowed_ref<> args,
				      gdbpy_opt_borrowed_ref<> kw)
{
  require_valid ();

  static const char *keywords[] = { "name", "domain", NULL };
  const char *symbol_name;
  int domain = VAR_DOMAIN;

  gdbpy_arg_parse_tuple_and_keywords (args, kw, "s|i", keywords, &symbol_name,
				      &domain);

  domain_search_flags flags = from_scripting_domain (domain);
  return lookup_global_symbol_from_objfile (objfile, STATIC_BLOCK,
					    symbol_name, flags).symbol;
}

/* Implement repr() for gdb.Objfile.  */

gdbpy_ref<>
objfile_object::repr ()
{
  if (objfile == nullptr)
    return gdbpy_safe_invalid_object_repr (this);

  return gdbpy_unicode_from_format ("<gdb.Objfile filename=%s>",
				    objfile_name (objfile));
}

/* Subroutine of gdbpy_lookup_objfile_by_build_id to simplify it.
   Return non-zero if STRING is a potentially valid build id.  */

static int
objfpy_build_id_ok (const char *string)
{
  size_t i, n = strlen (string);

  if (n % 2 != 0)
    return 0;
  for (i = 0; i < n; ++i)
    {
      if (!c_isxdigit (string[i]))
	return 0;
    }
  return 1;
}

/* Subroutine of gdbpy_lookup_objfile_by_build_id to simplify it.
   Returns non-zero if BUILD_ID matches STRING.
   It is assumed that objfpy_build_id_ok (string) returns TRUE.  */

static int
objfpy_build_id_matches (const struct bfd_build_id *build_id,
			 const char *string)
{
  size_t i;

  if (strlen (string) != 2 * build_id->size)
    return 0;

  for (i = 0; i < build_id->size; ++i)
    {
      char c1 = string[i * 2], c2 = string[i * 2 + 1];
      int byte = (fromhex (c1) << 4) | fromhex (c2);

      if (byte != build_id->data[i])
	return 0;
    }

  return 1;
}

/* Implementation of gdb.lookup_objfile.  */

objfile *
gdbpy_lookup_objfile (gdbpy_borrowed_ref<> args, gdbpy_opt_borrowed_ref<> kw)
{
  static const char *keywords[] = { "name", "by_build_id", NULL };
  const char *name;
  PyObject *by_build_id_obj = NULL;

  gdbpy_arg_parse_tuple_and_keywords (args, kw, "s|O!", keywords,
				      &name, &PyBool_Type, &by_build_id_obj);

  bool by_build_id = false;
  if (by_build_id_obj != nullptr)
    by_build_id = gdbpy_object_is_true (by_build_id_obj);

  if (by_build_id && !objfpy_build_id_ok (name))
    gdbpy_err_set_string (PyExc_TypeError, _("Not a valid build id."));

  struct objfile *objfile = nullptr;
  if (by_build_id)
    current_program_space->iterate_over_objfiles_in_search_order
      ([&objfile, name] (struct objfile *obj)
	 {
	   /* Don't return separate debug files.  */
	   if (obj->separate_debug_objfile_backlink != nullptr)
	     return false;

	   bfd *obfd = obj->obfd.get ();
	   if (obfd == nullptr)
	     return false;

	   const bfd_build_id *obfd_build_id = build_id_bfd_get (obfd);
	   if (obfd_build_id == nullptr)
	     return false;

	   if (!objfpy_build_id_matches (obfd_build_id, name))
	     return false;

	   objfile = obj;
	   return true;
	 }, gdbpy_current_objfile);
  else
    current_program_space->iterate_over_objfiles_in_search_order
      ([&objfile, name] (struct objfile *obj)
	 {
	   /* Don't return separate debug files.  */
	   if (obj->separate_debug_objfile_backlink != nullptr)
	     return false;

	   if ((obj->flags & OBJF_NOT_FILENAME) != 0)
	     return false;

	   const char *filename = objfile_filename (obj);
	   if (filename != NULL
	       && compare_filenames_for_search (filename, name))
	     {
	       objfile = obj;
	       return true;
	     }

	   if (compare_filenames_for_search (obj->original_name, name))
	     {
	       objfile = obj;
	       return true;
	     }

	   return false;
	 }, gdbpy_current_objfile);

  if (objfile != nullptr)
    return objfile;

  gdbpy_err_set_string (PyExc_ValueError, _("Objfile not found."));
}



/* Return a new reference to the Python object of type Objfile
   representing OBJFILE.  If the object has already been created,
   return it.  Otherwise, create it.  Return NULL and set the Python
   error on failure.  */

gdbpy_ref<>
objfile_to_objfile_object (struct objfile *objfile)
{
  PyObject *result = objfpy_objfile_data_key.get (objfile);
  if (result == NULL)
    {
      gdbpy_ref<objfile_object> object
	(PyObject_New (objfile_object, &objfile_object_type));
      if (object == NULL)
	return NULL;
      if (!objfpy_initialize (object))
	return NULL;

      object->objfile = objfile;
      objfpy_objfile_data_key.set (objfile, object.get ());
      result = object.release ();
    }

  return gdbpy_ref<>::new_reference (result);
}

static int
gdbpy_initialize_objfile ()
{
  return gdbpy_type_ready (&objfile_object_type);
}

GDBPY_INITIALIZE_FILE (gdbpy_initialize_objfile);



static PyMethodDef objfile_object_methods[] =
{
  noargs_method<objfile_object, &objfile_object::is_valid> ("is_valid",
    "is_valid () -> Boolean.\n\
Return true if this object file is valid, false if not."),

  varargs_method<objfile_object, &objfile_object::add_separate_debug_file>
    ("add_separate_debug_file",
     "add_separate_debug_file (file_name).\n\
Add FILE_NAME to the list of files containing debug info for the objfile."),

  varargs_method<objfile_object, &objfile_object::lookup_global_symbol>
    ("lookup_global_symbol",
     "lookup_global_symbol (name [, domain]).\n\
Look up a global symbol in this objfile and return it."),

  varargs_method<objfile_object, &objfile_object::lookup_static_symbol>
    ("lookup_static_symbol",
     "lookup_static_symbol (name [, domain]).\n\
Look up a static-linkage global symbol in this objfile and return it."),

  { nullptr }
};

static gdb_PyGetSetDef objfile_getset[] =
{
  gdbpy_dict_wrapper_cfg_dict_getter ("objfile"),
  { "filename", wrap_getter<objfile_object, &objfile_object::get_filename>,
    nullptr,
    "The objfile's filename, or None.", nullptr },
  { "username", wrap_getter<objfile_object, &objfile_object::get_username>,
    nullptr,
    "The name of the objfile as provided by the user, or None.", nullptr },
  { "owner", wrap_getter<objfile_object, &objfile_object::get_owner>,
    nullptr,
    "The objfile owner of separate debug info objfiles, or None.",
    nullptr },
  { "build_id", wrap_getter<objfile_object, &objfile_object::get_build_id>,
    nullptr,
    "The objfile's build id, or None.", nullptr },
  { "progspace", wrap_getter<objfile_object, &objfile_object::get_progspace>,
    nullptr,
    "The objfile's progspace, or None.", nullptr },
  { "pretty_printers", wrap_getter<objfile_object, &objfile_object::get_printers>,
    objfpy_set_printers, "Pretty printers.", nullptr },
  { "frame_filters",
    wrap_getter<objfile_object, &objfile_object::get_frame_filters>,
    objfpy_set_frame_filters, "Frame Filters.", nullptr },
  { "frame_unwinders",
    wrap_getter<objfile_object, &objfile_object::get_frame_unwinders>,
    objfpy_set_frame_unwinders, "Frame Unwinders", nullptr },
  { "type_printers",
    wrap_getter<objfile_object, &objfile_object::get_type_printers>,
    objfpy_set_type_printers, "Type printers.", nullptr },
  { "xmethods", wrap_getter<objfile_object, &objfile_object::get_xmethods>,
    nullptr, "Debug methods.", nullptr },
  { "is_file", wrap_getter<objfile_object, &objfile_object::is_file>, nullptr,
    "Whether this objfile came from a file.", nullptr },
  { nullptr }
};

PyTypeObject objfile_object_type =
{
  PyVarObject_HEAD_INIT (NULL, 0)
  "gdb.Objfile",		  /*tp_name*/
  sizeof (objfile_object),	  /*tp_basicsize*/
  0,				  /*tp_itemsize*/
  objfpy_dealloc,		  /*tp_dealloc*/
  0,				  /*tp_print*/
  0,				  /*tp_getattr*/
  0,				  /*tp_setattr*/
  0,				  /*tp_compare*/
  wrap_tp_callback<objfile_object, &objfile_object::repr>, /* tp_repr */
  0,				  /*tp_as_number*/
  0,				  /*tp_as_sequence*/
  0,				  /*tp_as_mapping*/
  0,				  /*tp_hash */
  0,				  /*tp_call*/
  0,				  /*tp_str*/
  gdbpy_dict_wrapper_getsetattro,
  0,				  /*tp_as_buffer*/
  Py_TPFLAGS_DEFAULT,		  /*tp_flags*/
  "GDB objfile object",		  /* tp_doc */
  0,				  /* tp_traverse */
  0,				  /* tp_clear */
  0,				  /* tp_richcompare */
  0,				  /* tp_weaklistoffset */
  0,				  /* tp_iter */
  0,				  /* tp_iternext */
  objfile_object_methods,	  /* tp_methods */
  0,				  /* tp_members */
  objfile_getset,		  /* tp_getset */
  0,				  /* tp_base */
  0,				  /* tp_dict */
  0,				  /* tp_descr_get */
  0,				  /* tp_descr_set */
  0,				  /* tp_dictoffset */
  0,				  /* tp_init */
  0,				  /* tp_alloc */
  objfpy_new,			  /* tp_new */
};

PyTypeObject *objfile_object::corresponding_object_type
  = &objfile_object_type;
