/* Interface to coff-pe-read.c (portable-executable-specific symbol reader).

   Copyright (C) 2003-2023 Free Software Foundation, Inc.

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
   along with this program.  If not, see <http://www.gnu.org/licenses/>.

   Contributed by Raoul M. Gough (RaoulGough@yahoo.co.uk).  */

#if !defined (COFF_PE_READ_H)
#define COFF_PE_READ_H

class minimal_symbol_reader;
struct objfile;
struct bfd;

/* Read the export table and convert it to minimal symbol table
   entries */
extern void read_pe_exported_syms (minimal_symbol_reader &reader,
				   struct objfile *objfile);

/* Extract from ABFD the offset of the .text section.
   Returns default value 0x1000 if information is not found.  */
extern CORE_ADDR pe_text_section_offset (struct bfd *abfd);

/* Return the address offset due to the use of the 'rebase' utility.
   This offset will be applied to all addresses coming from the DWARF
   reader.  The problem being solved here is that when the Windows
   'rebase' utility is used on a DLL, the DWARF is not updated, so all
   the addresses are wrong.  See
   https://sourceware.org/bugzilla/show_bug.cgi?id=18027.  More recent
   versions of binutils will add a section to a DLL so that this
   situation can be detected.  */
extern CORE_ADDR pe_rebase_offset (bfd *abfd);

#endif /* !defined (COFF_PE_READ_H) */
