/* Output generating routines for Python.

   Copyright (C) 2013 Free Software Foundation, Inc.

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
#include "ui-out.h"
#include "python-internal.h"

typedef struct gdbpy_out_data
{
  /* The Python object we're currently building.  If this is NULL then
     an error occurred -- no further work should be done and the
     Python exception may be assumed to have been set.  */

  PyObject *current;

} gdbpy_out_data;

static void
gdbpy_table_begin (struct ui_out *uiout, int nbrofcols,
		   int nr_rows,
		   const char *tblid)
{
  gdb_assert_not_reached (_("gdbpy_table_begin"));
}

static void
gdbpy_table_body (struct ui_out *uiout)
{
  gdb_assert_not_reached (_("gdbpy_table_body"));
}

static void
gdbpy_table_end (struct ui_out *uiout)
{
  gdb_assert_not_reached (_("gdbpy_table_end"));
}

static void
gdbpy_begin (struct ui_out *uiout,
	     enum ui_out_type type,
	     int level,
	     const char *id)
{
  gdb_assert_not_reached (_("gdbpy_begin"));
}

static void
gdbpy_end (struct ui_out *uiout,
	   enum ui_out_type type,
	   int level)
{
  gdb_assert_not_reached (_("gdbpy_end"));
}

static void
gdbpy_field_int (struct ui_out *uiout, int fldno, int width,
		 enum ui_align alignment,
		 const char *fldname, int value)
{
  gdbpy_out_data *data = ui_out_data (uiout);
  PyObject *val;

  if (data->current == NULL)
    return;

  val = PyInt_FromLong (value);
  if (val == NULL
      || PyDict_SetItemString (data->current, fldname, val) < 0)
    {
      Py_DECREF (data->current);
      data->current = NULL;
    }
  Py_XDECREF (val);
}

static void
gdbpy_field_string (struct ui_out *uiout,
		    int fldno,
		    int width,
		    enum ui_align align,
		    const char *fldname,
		    const char *string)
{
  gdbpy_out_data *data = ui_out_data (uiout);
  PyObject *val;

  if (data->current == NULL)
    return;

  val = PyString_FromString (value);
  if (val == NULL
      || PyDict_SetItemString (data->current, fldname, val) < 0)
    {
      Py_DECREF (data->current);
      data->current = NULL;
    }
  Py_XDECREF (val);
}

static void ATTRIBUTE_PRINTF (6, 0)
gdbpy_field_fmt (struct ui_out *uiout, int fldno,
		 int width, enum ui_align align,
		 const char *fldname,
		 const char *format,
		 va_list args)
{
  gdbpy_out_data *data = ui_out_data (uiout);
  char *str;

  str = xstrvprintf (format, args);
  gdbpy_field_string (uiout, fldno, width, align, fldname, str);
  xfree (str);
}

static void
gdbpy_data_destroy (struct ui_out *uiout)
{
  gdbpy_out_data *data = ui_out_data (uiout);

  Py_DECREF (data->current);
}

static const struct ui_out_impl gdbpy_ui_out_impl =
{
  gdbpy_table_begin,
  gdbpy_table_body,
  gdbpy_table_end,
  NULL,				/* table_header */
  gdbpy_begin,
  gdbpy_end,
  gdbpy_field_int,
  NULL,				/* field_skip */
  gdbpy_field_string,
  gdbpy_field_fmt,
  NULL,				/* spaces */
  NULL,				/* text */
  NULL,				/* message */
  NULL,				/* wrap_hint */
  NULL,				/* flush */
  NULL,				/* redirect */
  gdbpy_data_destroy,
  1, /* Does need MI hacks.  */
};

struct ui_out *
gdbpy_out_new (void)
{
  gdbpy_out_data *data = XNEW (gdbpy_out_data);

  data->current = PyDict_New ();
  if (data->current == NULL)
    {
      xfree (data);
      return NULL;
    }

  return ui_out_new (&gdbpy_ui_out_impl, data, 0);
}

PyObject *
gdbpy_out_result (struct ui_out *uiout)
{
  gdbpy_out_data *data = ui_out_data (uiout);
  PyObject *result = data->current;

  Py_XINCREF (result);
  return result;
}
