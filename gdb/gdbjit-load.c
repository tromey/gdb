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
#include "command.h"
#include "value.h"
#include "objfiles.h"
#include "infcall.h"
#include "gdbcore.h"
#include "readline/tilde.h"
#include <sys/mman.h> /* FIXME */

static CORE_ADDR
get_pages (CORE_ADDR size)
{
  struct objfile *objf;
  struct value *mmap_val = find_function_in_inferior ("mmap64", &objf);
  struct value *addr_val;
  struct gdbarch *gdbarch = get_objfile_arch (objf);
  CORE_ADDR retval;
  enum
    {
      ARG_ADDR, ARG_LENGTH, ARG_PROT, ARG_FLAGS, ARG_FD, ARG_OFFSET, ARG_MAX
    };
  struct value *arg[ARG_MAX];

  arg[ARG_ADDR] = value_from_pointer (builtin_type (gdbarch)->builtin_data_ptr,
				      0);
  /* FIXME: Assuming sizeof (unsigned long) == sizeof (size_t).  */
  arg[ARG_LENGTH] = value_from_ulongest
		    (builtin_type (gdbarch)->builtin_unsigned_long, size);
  /* FIXME: Move PROT_* to tdep.  */
  /* FIXME: Separate r/o vs. r/w segments.  */
  arg[ARG_PROT] = value_from_longest (builtin_type (gdbarch)->builtin_int,
				      PROT_EXEC | PROT_READ | PROT_WRITE);
  /* FIXME: Move MAP_* to tdep.  */
  arg[ARG_FLAGS] = value_from_longest (builtin_type (gdbarch)->builtin_int,
				       MAP_PRIVATE | MAP_ANONYMOUS);
  arg[ARG_FD] = value_from_longest (builtin_type (gdbarch)->builtin_int, -1);
  /* FIXME: Assuming sizeof (off_t) == sizeof (size_t).  */
  arg[ARG_OFFSET] = value_from_longest (builtin_type (gdbarch)->builtin_int64,
					0);
  addr_val = call_function_by_hand (mmap_val, ARG_MAX, arg);
  retval = value_as_address (addr_val);
  if (retval == (CORE_ADDR) -1)
    error (_("Failed inferior mmap call for %s bytes."), pulongest (size));
  return retval;
}

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

static void
copy_sections (bfd *abfd, asection *sect, void *data_unused)
{
  bfd_byte *sect_data;
  struct cleanup *cleanups;

  if ((bfd_get_section_flags (abfd, sect) & (SEC_ALLOC | SEC_LOAD))
      != (SEC_ALLOC | SEC_LOAD))
    return;

  if (bfd_get_section_size (sect) == 0)
    return;

  /* FIXME: It does not report relocations to undefined symbols.  */
  sect_data = bfd_simple_get_relocated_section_contents (abfd, sect, NULL,
							 NULL);
  if (sect_data == NULL)
    error (_("Cannot map JIT module \"%s\" section \"%s\": %s"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, sect),
	   bfd_errmsg (bfd_get_error ()));
  cleanups = make_cleanup (xfree, sect_data);

  if (0 != target_write_memory (bfd_get_section_vma (abfd, sect), sect_data,
				bfd_get_section_size (sect)))
    error (_("Cannot write JIT module \"%s\" section \"%s\": %s"),
	   bfd_get_filename (abfd), bfd_get_section_name (abfd, sect),
	   bfd_errmsg (bfd_get_error ()));

  do_cleanups (cleanups);
}

static CORE_ADDR
get_func_addr (bfd *abfd, const char *name)
{
  long storage_needed;
  asymbol **symbol_table;
  long number_of_symbols, symi;

  storage_needed = bfd_get_symtab_upper_bound (abfd);
  if (storage_needed < 0)
    error (_("Cannot read symbols of JIT module \"%s\": %s"),
	   bfd_get_filename (abfd), bfd_errmsg (bfd_get_error ()));

  symbol_table = xmalloc (storage_needed);
  number_of_symbols = bfd_canonicalize_symtab (abfd, symbol_table);
  if (number_of_symbols < 0)
    error (_("Cannot parse symbols of JIT module \"%s\": %s"),
	   bfd_get_filename (abfd), bfd_errmsg (bfd_get_error ()));

  for (symi = 0; symi < number_of_symbols; symi++)
    {
      asymbol *sym= symbol_table[symi];

      if ((sym->flags & (BSF_GLOBAL | BSF_FUNCTION))
	  != (BSF_GLOBAL | BSF_FUNCTION))
	continue;
      if (strcmp (bfd_asymbol_name (sym), name) != 0)
	continue;

      /* BFD symbols are section relative.  */
      return sym->value + bfd_get_section_vma (abfd, sym->section);
    }

  error (_("Could not find symbol \"%s\" of JIT module \"%s\"."),
	 name, bfd_get_filename (abfd));
}

static void
call_func (CORE_ADDR func_addr)
{
  struct value *func_val;

  func_val = value_from_pointer
	     (builtin_type (target_gdbarch ())->builtin_func_ptr, func_addr);
  call_function_by_hand (func_val, 0, NULL);
}

static void
expression_load_command (char *args, int from_tty)
{
  struct cleanup *cleanups;
  char *filename, **matching;
  bfd *abfd;
  struct setup_sections_data setup_sections_data;
  CORE_ADDR addr, func_addr;

  filename = tilde_expand (args);
  cleanups = make_cleanup (xfree, filename);

  /* FIXME: Use struct objfile for module's unwinding, 'info sym' etc.  */
  abfd = gdb_bfd_open (filename, gnutarget, -1);
  if (abfd == NULL)
    error (_("\"%s\": could not open as JIT module: %s"),
	   filename, bfd_errmsg (bfd_get_error ()));
  make_cleanup_bfd_unref (abfd);

  if (!bfd_check_format_matches (abfd, bfd_object, &matching))
    error (_("\"%s\": not in loadable format: %s"),
	   filename, gdb_bfd_errmsg (bfd_get_error (), matching));

  if ((bfd_get_file_flags (abfd) & EXEC_P) != 0)
    error (_("\"%s\": not in object format: %s"),
	   filename, gdb_bfd_errmsg (bfd_get_error (), matching));

  setup_sections_data.vma = 0;
  setup_sections_data.max_alignment = 1;
  bfd_map_over_sections (abfd, setup_sections, &setup_sections_data);

  addr = get_pages (setup_sections_data.vma);
  if ((addr & (setup_sections_data.max_alignment - 1)) != 0)
    error (_("Inferior JIT module address %s not aligned to BFD required %s."),
	   paddress (target_gdbarch (), addr),
	   paddress (target_gdbarch (), setup_sections_data.max_alignment));

  bfd_map_over_sections (abfd, add_to_vma, &addr);

  func_addr = get_func_addr (abfd, "func");

  bfd_map_over_sections (abfd, copy_sections, NULL);

  call_func (func_addr);

  do_cleanups (cleanups);
}

extern initialize_file_ftype _initialize_gdbjit_load; /* -Wmissing-prototypes */

void
_initialize_gdbjit_load (void)
{
  add_com ("expression-load", class_files, expression_load_command,
	   _("Load shared object library symbols for files matching REGEXP."));
}
