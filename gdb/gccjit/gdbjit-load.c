/* Load module for 'expression' command.

   Copyright (C) 2014 Free Software Foundation, Inc.

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
#include "gdbjit-load.h"
#include "gccjit-internal.h"
#include "command.h"
#include "value.h"
#include "objfiles.h"
#include "infcall.h"
#include "gdbcore.h"
#include "readline/tilde.h"
#include "bfdlink.h"
#include "gdbcmd.h"

struct setup_sections_data
{
  CORE_ADDR vma;
  CORE_ADDR max_alignment;
};

static void
setup_sections (bfd *abfd, asection *sect, void *data_voidp)
{
  struct setup_sections_data *data = data_voidp;
  CORE_ADDR alignment;

  /* It is required by later bfd_get_relocated_section_contents.  */
  if (sect->output_section == NULL)
    sect->output_section = sect;

  if ((bfd_get_section_flags (abfd, sect) & SEC_ALLOC) == 0)
    return;

  alignment = ((CORE_ADDR) 1) << bfd_get_section_alignment (abfd, sect);
  data->max_alignment = max (data->max_alignment, alignment);

  data->vma = (data->vma + alignment - 1) & -alignment;

  bfd_set_section_vma (abfd, sect, data->vma);

  data->vma += bfd_get_section_size (sect);
  data->vma = (data->vma + alignment - 1) & -alignment;
}

static void
add_to_vma (bfd *abfd, asection *sect, void *data)
{
  CORE_ADDR addr = *(CORE_ADDR *) data;
  CORE_ADDR alignment;

  if ((bfd_get_section_flags (abfd, sect) & SEC_ALLOC) == 0)
    return;

  bfd_set_section_vma (abfd, sect, addr + bfd_get_section_vma (abfd, sect));
}

static bfd_boolean
link_callbacks_multiple_definition (struct bfd_link_info *link_info,
				    struct bfd_link_hash_entry *h, bfd *nbfd,
				    asection *nsec, bfd_vma nval)
{
  bfd *abfd = link_info->input_bfds;

  if (link_info->allow_multiple_definition)
    return TRUE;
  warning (_("JIT module \"%s\": multiple symbol definitions: %s\n"),
	   bfd_get_filename (abfd), h->root.string);
  return FALSE;
}

static bfd_boolean
link_callbacks_warning (struct bfd_link_info *link_info, const char *xwarning,
                        const char *symbol, bfd *abfd, asection *section,
			bfd_vma address)
{
  warning (_("JIT module \"%s\" section \"%s\": warning: %s\n"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, section),
	   xwarning);
  /* Maybe permit running such a JIT module?  */
  return FALSE;
}

static bfd_boolean
link_callbacks_undefined_symbol (struct bfd_link_info *link_info,
				 const char *name, bfd *abfd, asection *section,
				 bfd_vma address, bfd_boolean is_fatal)
{
  warning (_("Cannot resolve relocation to \"%s\" "
	     "from JIT module \"%s\" section \"%s\"."),
	   name, bfd_get_filename (abfd), bfd_get_section_name (abfd, section));
  return FALSE;
}

static bfd_boolean
link_callbacks_reloc_overflow (struct bfd_link_info *link_info,
			       struct bfd_link_hash_entry *entry,
			       const char *name, const char *reloc_name,
			       bfd_vma addend, bfd *abfd, asection *section,
			       bfd_vma address)
{
  /* TRUE is required for intra-module relocations.  */
  return TRUE;
}

static bfd_boolean
link_callbacks_reloc_dangerous (struct bfd_link_info *link_info,
				const char *message, bfd *abfd,
				asection *section, bfd_vma address)
{
  warning (_("JIT module \"%s\" section \"%s\": dangerous relocation: %s\n"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, section),
	   message);
  return FALSE;
}

static bfd_boolean
link_callbacks_unattached_reloc (struct bfd_link_info *link_info,
				 const char *name, bfd *abfd, asection *section,
				 bfd_vma address)
{
  warning (_("JIT module \"%s\" section \"%s\": unattached relocation: %s\n"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, section),
	   name);
  return FALSE;
}

static void
link_callbacks_einfo (const char *fmt, ...)
{
  struct cleanup *cleanups;
  va_list ap;
  char *str;

  va_start (ap, fmt);
  str = xstrvprintf (fmt, ap);
  va_end (ap);
  cleanups = make_cleanup (xfree, str);

  warning (_("JIT module: warning: %s\n"), str);

  do_cleanups (cleanups);
}

/* FIXME: These symbols are set by bfd_simple_get_relocated_section_contents
   but bfd/ seems to use even the NULL ones without checking them first.  */
static const struct bfd_link_callbacks link_callbacks =
{
  NULL, /* add_archive_element */
  link_callbacks_multiple_definition, /* multiple_definition */
  NULL, /* multiple_common */
  NULL, /* add_to_set */
  NULL, /* constructor */
  link_callbacks_warning, /* warning */
  link_callbacks_undefined_symbol, /* undefined_symbol */
  link_callbacks_reloc_overflow, /* reloc_overflow */
  link_callbacks_reloc_dangerous, /* reloc_dangerous */
  link_callbacks_unattached_reloc, /* unattached_reloc */
  NULL, /* notice */
  link_callbacks_einfo, /* einfo */
  NULL, /* info */
  NULL, /* minfo */
  NULL, /* override_segment_assignment */
};

static void
link_hash_table_free (void *data)
{
  struct bfd_link_info *link_info = data;
  bfd *abfd = link_info->input_bfds;

  bfd_link_hash_table_free (abfd, link_info->hash);
}

static void
copy_sections (bfd *abfd, asection *sect, void *data)
{
  asymbol **symbol_table = data;
  bfd_byte *sect_data, *sect_data_got;
  struct cleanup *cleanups;
  struct bfd_link_info link_info;
  struct bfd_link_order link_order;

  if ((bfd_get_section_flags (abfd, sect) & (SEC_ALLOC | SEC_LOAD))
      != (SEC_ALLOC | SEC_LOAD))
    return;

  if (bfd_get_section_size (sect) == 0)
    return;

  /* Mostly a copy of bfd_simple_get_relocated_section_contents which GDB
     cannot use as it does not report relocations to undefined symbols.  */
  memset (&link_info, 0, sizeof (link_info));
  link_info.output_bfd = abfd;
  link_info.input_bfds = abfd;
  link_info.input_bfds_tail = &abfd->link_next;
  link_info.hash = bfd_link_hash_table_create (abfd);
  cleanups = make_cleanup (link_hash_table_free, &link_info);
  link_info.callbacks = &link_callbacks;
  memset (&link_order, 0, sizeof (link_order));
  link_order.next = NULL;
  link_order.type = bfd_indirect_link_order;
  link_order.offset = 0;
  link_order.size = bfd_get_section_size (sect);
  link_order.u.indirect.section = sect;

  sect_data = xmalloc (bfd_get_section_size (sect));
  make_cleanup (xfree, sect_data);

  sect_data_got = bfd_get_relocated_section_contents (abfd, &link_info,
						      &link_order, sect_data,
						      FALSE, symbol_table);
  if (sect_data_got == NULL)
    error (_("Cannot map JIT module \"%s\" section \"%s\": %s"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, sect),
	   bfd_errmsg (bfd_get_error ()));
  gdb_assert (sect_data_got == sect_data);

  if (0 != target_write_memory (bfd_get_section_vma (abfd, sect), sect_data,
				bfd_get_section_size (sect)))
    error (_("Cannot write JIT module \"%s\" section \"%s\"."),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, sect));

  do_cleanups (cleanups);
}

static void
call_func (CORE_ADDR func_addr)
{
  struct value *func_val;

  func_val = value_from_pointer
	     (builtin_type (target_gdbarch ())->builtin_func_ptr, func_addr);
  call_function_by_hand (func_val, 0, NULL);
}

static const struct objfile_data *gdbjit_objfile_data_key;

void
gdbjit_load (const char *object_file)
{
  struct cleanup *cleanups, *cleanups_symbol_table;
  char *filename, **matching;
  bfd *abfd;
  struct setup_sections_data setup_sections_data;
  CORE_ADDR addr, func_addr, got_start;
  struct objfile *objfile;
  struct bound_minimal_symbol bmsym;
  long storage_needed;
  asymbol **symbol_table, **symp;
  long number_of_symbols;
  struct type *dptr_type = builtin_type (target_gdbarch ())->builtin_data_ptr;
  unsigned dptr_type_len = TYPE_LENGTH (dptr_type);

  filename = tilde_expand (object_file);
  cleanups = make_cleanup (xfree, filename);

  abfd = gdb_bfd_open (filename, gnutarget, -1);
  if (abfd == NULL)
    error (_("\"%s\": could not open as JIT module: %s"),
	   filename, bfd_errmsg (bfd_get_error ()));
  make_cleanup_bfd_unref (abfd);

  if (!bfd_check_format_matches (abfd, bfd_object, &matching))
    error (_("\"%s\": not in loadable format: %s"),
	   filename, gdb_bfd_errmsg (bfd_get_error (), matching));

  if ((bfd_get_file_flags (abfd) & (EXEC_P | DYNAMIC)) != 0)
    error (_("\"%s\": not in object format."), filename);

  setup_sections_data.vma = 0;
  setup_sections_data.max_alignment = 1;
  bfd_map_over_sections (abfd, setup_sections, &setup_sections_data);

  storage_needed = bfd_get_symtab_upper_bound (abfd);
  if (storage_needed < 0)
    error (_("Cannot read symbols of JIT module \"%s\": %s"),
          bfd_get_filename (abfd), bfd_errmsg (bfd_get_error ()));

  /* The memory may be later needed
     by bfd_generic_get_relocated_section_contents
     called from default_symfile_relocate.  */
  symbol_table = xmalloc (storage_needed);
  cleanups_symbol_table = make_cleanup (xfree, symbol_table);
  number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);
  if (number_of_symbols < 0)
    error (_("Cannot parse symbols of JIT module \"%s\": %s"),
          bfd_get_filename (abfd), bfd_errmsg (bfd_get_error ()));

  setup_sections_data.vma = ((setup_sections_data.vma + dptr_type_len - 1)
			     & -(CORE_ADDR) dptr_type_len);
  got_start = setup_sections_data.vma;
  for (symp = symbol_table; symp < &symbol_table[number_of_symbols]; symp++)
    {
      asymbol *sym = *symp;
      if (sym->flags == 0)
	setup_sections_data.vma += dptr_type_len;
    }

  addr = gdbarch_infcall_mmap (target_gdbarch (), setup_sections_data.vma);
  if ((addr & (setup_sections_data.max_alignment - 1)) != 0)
    error (_("Inferior JIT module address %s not aligned to BFD required %s."),
	   paddress (target_gdbarch (), addr),
	   paddress (target_gdbarch (), setup_sections_data.max_alignment));

  bfd_map_over_sections (abfd, add_to_vma, &addr);
  got_start += addr;

  /* SYMFILE_VERBOSE is not passed even if FROM_TTY, user is not interested in
     "Reading symbols from ..." message for automatically generated file.  */
  objfile = symbol_file_add_from_bfd (abfd, filename, 0, NULL, 0, NULL);

  set_objfile_data (objfile, gdbjit_objfile_data_key, symbol_table);
  discard_cleanups (cleanups_symbol_table);

  bmsym = lookup_minimal_symbol_text (GCC_C_FE_WRAPPER_FUNCTION, objfile);
  if (bmsym.minsym == NULL || MSYMBOL_TYPE (bmsym.minsym) == mst_file_text)
    error (_("Could not find symbol \"%s\" of JIT module \"%s\"."),
	   GCC_C_FE_WRAPPER_FUNCTION, filename);
  func_addr = BMSYMBOL_VALUE_ADDRESS (bmsym);

  for (symp = symbol_table; symp < symbol_table + number_of_symbols; symp++)
    {
      asymbol *sym = *symp;

      if (sym->flags != 0)
	continue;
      sym->flags = BSF_GLOBAL;
      sym->section = bfd_abs_section_ptr;
      if (strcmp (sym->name, "_GLOBAL_OFFSET_TABLE_") ==0 )
	sym->value=0;
      else
	{
	  bmsym = lookup_minimal_symbol (sym->name, NULL, NULL);
	  switch (bmsym.minsym == NULL
		  ? mst_unknown : MSYMBOL_TYPE (bmsym.minsym))
	    {
	    case mst_text:
	    case mst_data:
	    case mst_bss:
	    case mst_abs:
	      {
		gdb_byte buf[sizeof (LONGEST)];

		sym->value = got_start;
		store_typed_address (buf, dptr_type,
				     BMSYMBOL_VALUE_ADDRESS (bmsym));
		if (0 != target_write_memory (got_start, buf, dptr_type_len))
		  error (_("Cannot store address to %s for JIT module \"%s\"."),
			 paddress (target_gdbarch (), got_start), filename);
		got_start += dptr_type_len;
		break;
	      }
	    default:
	      error (_("Could not find symbol \"%s\" for JIT module \"%s\"."),
		     sym->name, filename);
	    }
	}
    }

  bfd_map_over_sections (abfd, copy_sections, symbol_table);

  call_func (func_addr);

  do_cleanups (cleanups);
}

static void
expression_load_command (char *args, int from_tty)
{
  if (args == NULL || *args == 0)
    error (_("Argument required."));
  gdbjit_load (args);
}

static void
gdbjit_per_objfile_free (struct objfile *objfile, void *d)
{
  asymbol **symbol_table = d;

  xfree (symbol_table);
}

extern initialize_file_ftype _initialize_gdbjit_load; /* -Wmissing-prototypes */

void
_initialize_gdbjit_load (void)
{
  gdbjit_objfile_data_key = register_objfile_data_with_cleanup (NULL,
						       gdbjit_per_objfile_free);

  add_cmd ("expression-load", class_maintenance, expression_load_command,
	   _("Load shared object library symbols for files matching REGEXP."),
	   &maintenancelist);
}
