/* Read AIX xcoff symbol tables and convert to internal format, for GDB.
   Copyright (C) 1986-2026 Free Software Foundation, Inc.
   Derived from coffread.c, dbxread.c, and a lot of hacking.
   Contributed by IBM Corporation.

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

#include "bfd.h"
#include "event-top.h"

#include "coff/internal.h"
#include "libcoff.h"
#include "coff/xcoff.h"
#include "coff/rs6000.h"
#include "xcoffread.h"

#include "symtab.h"
#include "gdbtypes.h"
#include "symfile.h"
#include "objfiles.h"
#include "complaints.h"
#include "dwarf2/sect-names.h"
#include "dwarf2/public.h"


struct xcoff_symfile_info
  {
    /* Pointer to the string table.  */
    char *strtbl = nullptr;

    /* Pointer to debug section.  */
    char *debugsec = nullptr;

    /* Pointer to the a.out symbol table.  */
    char *symtbl = nullptr;

    /* Number of symbols in symtbl.  */
    int symtbl_num_syms = 0;

    /* Offset in data section to TOC anchor.  */
    CORE_ADDR toc_offset = 0;
  };

/* Key for XCOFF-associated data.  */

static const registry<objfile>::key<xcoff_symfile_info> xcoff_objfile_data_key;

/* Convenience macro to access the per-objfile XCOFF data.  */

#define XCOFF_DATA(objfile)						\
  xcoff_objfile_data_key.get (objfile)

/* XCOFF names for dwarf sections.  There is no compressed sections.  */

static const struct dwarf2_debug_sections dwarf2_xcoff_names = {
  { ".dwinfo", NULL },
  { ".dwabrev", NULL },
  { ".dwline", NULL },
  { ".dwloc", NULL },
  { NULL, NULL }, /* debug_loclists */
  /* AIX XCOFF defines one, named DWARF section for macro debug information.
     XLC does not generate debug_macinfo for DWARF4 and below.
     The section is assigned to debug_macro for DWARF5 and above. */
  { NULL, NULL },
  { ".dwmac", NULL },
  { ".dwstr", NULL },
  { NULL, NULL }, /* debug_str_offsets */
  { NULL, NULL }, /* debug_line_str */
  { ".dwrnges", NULL },
  { NULL, NULL }, /* debug_rnglists */
  { ".dwpbtyp", NULL },
  { NULL, NULL }, /* debug_addr */
  { ".dwframe", NULL },
  { NULL, NULL }, /* eh_frame */
  { NULL, NULL }, /* gdb_index */
  { NULL, NULL }, /* debug_names */
  { NULL, NULL }, /* debug_aranges */
  23
};

static void xcoff_initial_scan (struct objfile *, symfile_add_flags);

static void scan_xcoff_symtab (struct objfile *);

static void xcoff_symfile_init (struct objfile *);

/* Search all BFD sections for the section whose target_index is
   equal to N_SCNUM.  Set *BFD_SECT to that section.  The section's
   associated index in the objfile's section_offset table is also
   stored in *SECNUM.

   If no match is found, *BFD_SECT is set to NULL, and *SECNUM
   is set to the text section's number.  */

static void
xcoff_secnum_to_sections (int n_scnum, struct objfile *objfile,
			  asection **bfd_sect, int *secnum)
{
  *bfd_sect = NULL;
  *secnum = SECT_OFF_TEXT (objfile);

  for (asection *sec : gdb_bfd_sections (objfile->obfd.get ()))
    {
      if (sec->target_index == n_scnum)
	{
	  /* This is the section.  Figure out what SECT_OFF_* code it is.  */
	  if (bfd_section_flags (sec) & SEC_CODE)
	    *secnum = SECT_OFF_TEXT (objfile);
	  else if (bfd_section_flags (sec) & SEC_LOAD)
	    *secnum = SECT_OFF_DATA (objfile);
	  else
	    *secnum = gdb_bfd_section_index (objfile->obfd.get (), sec);
	  *bfd_sect = sec;
	}
    }
}

/* Do initialization in preparation for reading symbols from OBJFILE.

   We will only be called if this is an XCOFF or XCOFF-like file.
   BFD handles figuring out the format of the file, and code in symfile.c
   uses BFD's determination to vector to us.  */

static void
xcoff_symfile_init (struct objfile *objfile)
{
  /* Allocate struct to keep track of the symfile.  */
  xcoff_objfile_data_key.emplace (objfile);
}

/* Swap raw symbol at *RAW and put the name in *NAME, the symbol in
   *SYMBOL, the first auxent in *AUX.  Advance *RAW and *SYMNUMP over
   the symbol and its auxents.  */

static void
swap_sym (struct internal_syment *symbol, union internal_auxent *aux,
	  const char **name, char **raw, unsigned int *symnump,
	  struct objfile *objfile)
{
  bfd_coff_swap_sym_in (objfile->obfd.get (), *raw, symbol);
  if (symbol->n_zeroes)
    {
      /* If it's exactly E_SYMNMLEN characters long it isn't
	 '\0'-terminated.  */
      if (symbol->n_name[E_SYMNMLEN - 1] != '\0')
	{
	  /* FIXME: wastes memory for symbols which we don't end up putting
	     into the minimal symbols.  */
	  *name = obstack_strndup (&objfile->objfile_obstack,
				    symbol->n_name, E_SYMNMLEN);
	}
      else
	/* Point to the unswapped name as that persists as long as the
	   objfile does.  */
	*name = ((struct external_syment *) *raw)->e.e_name;
    }
  else if (symbol->n_sclass & 0x80)
    {
      *name = XCOFF_DATA (objfile)->debugsec + symbol->n_offset;
    }
  else
    {
      *name = XCOFF_DATA (objfile)->strtbl + symbol->n_offset;
    }
  ++*symnump;
  *raw += coff_data (objfile->obfd)->local_symesz;
  if (symbol->n_numaux > 0)
    {
      bfd_coff_swap_aux_in (objfile->obfd.get (), *raw, symbol->n_type,
			    symbol->n_sclass, 0, symbol->n_numaux, aux);

      *symnump += symbol->n_numaux;
      *raw += coff_data (objfile->obfd)->local_symesz * symbol->n_numaux;
    }
}

static void
scan_xcoff_symtab (struct objfile *objfile)
{
  CORE_ADDR toc_offset = 0;	/* toc offset value in data section.  */

  const char *namestring;
  bfd *abfd;
  asection *bfd_sect = nullptr;
  int ignored;
  unsigned int nsyms;

  char *sraw_symbol;
  struct internal_syment symbol;
  union internal_auxent main_aux[5];
  unsigned int ssymnum;

  abfd = objfile->obfd.get ();

  sraw_symbol = XCOFF_DATA (objfile)->symtbl;
  nsyms = XCOFF_DATA (objfile)->symtbl_num_syms;
  ssymnum = 0;
  while (ssymnum < nsyms)
    {
      int sclass;

      QUIT;

      bfd_coff_swap_sym_in (abfd, sraw_symbol, &symbol);
      sclass = symbol.n_sclass;

      switch (sclass)
	{
	case C_HIDEXT:
	  {
	    /* The CSECT auxent--always the last auxent.  */
	    union internal_auxent csect_aux;

	    swap_sym (&symbol, &main_aux[0], &namestring, &sraw_symbol,
		      &ssymnum, objfile);
	    if (symbol.n_numaux > 1)
	      {
		bfd_coff_swap_aux_in
		  (objfile->obfd.get (),
		   sraw_symbol - coff_data (abfd)->local_symesz,
		   symbol.n_type,
		   symbol.n_sclass,
		   symbol.n_numaux - 1,
		   symbol.n_numaux,
		   &csect_aux);
	      }
	    else
	      csect_aux = main_aux[0];

	    switch (csect_aux.x_csect.x_smtyp & 0x7)
	      {
	      case XTY_SD:
		switch (csect_aux.x_csect.x_smclas)
		  {
		  case XMC_TC0:
		    if (toc_offset)
		      warning (_("More than one XMC_TC0 symbol found."));
		    toc_offset = symbol.n_value;

		    /* Make TOC offset relative to start address of
		       section.  */
		    xcoff_secnum_to_sections (symbol.n_scnum, objfile,
							 &bfd_sect, &ignored);
		    if (bfd_sect)
		      toc_offset -= bfd_section_vma (bfd_sect);
		    break;

		  default:
		    break;
		  }
		break;
	      }
	  }
	  break;
	default:
	  {
	    complaint (_("Storage class %d not recognized during scan"),
		       sclass);
	  }
	  [[fallthrough]];

	case C_RSYM:
	  {
	    /* We probably could save a few instructions by assuming that
	       C_LSYM, C_PSYM, etc., never have auxents.  */
	    int naux1 = symbol.n_numaux + 1;

	    ssymnum += naux1;
	    sraw_symbol += bfd_coff_symesz (abfd) * naux1;
	  }
	  break;
	}
    }

  /* Record the toc offset value of this symbol table into objfile
     structure.  If no XMC_TC0 is found, toc_offset should be zero.
     Another place to obtain this information would be file auxiliary
     header.  */

  XCOFF_DATA (objfile)->toc_offset = toc_offset;
}

/* Return the toc offset value for a given objfile.  */

CORE_ADDR
xcoff_get_toc_offset (struct objfile *objfile)
{
  if (objfile)
    return XCOFF_DATA (objfile)->toc_offset;
  return 0;
}

/* Scan and build partial symbols for a symbol file.
   We have been initialized by a call to dbx_symfile_init, which
   put all the relevant info into a "struct dbx_symfile_info",
   hung off the objfile structure.

   SECTION_OFFSETS contains offsets relative to which the symbols in the
   various sections are (depending where the sections were actually
   loaded).  */

static void
xcoff_initial_scan (struct objfile *objfile, symfile_add_flags symfile_flags)
{
  bfd *abfd;
  int val;
  int num_symbols;		/* # of symbols */
  file_ptr symtab_offset;	/* symbol table and */
  struct xcoff_symfile_info *info;
  const char *name;
  unsigned int size;

  info = XCOFF_DATA (objfile);
  abfd = objfile->obfd.get ();
  name = objfile_name (objfile);

  num_symbols = bfd_get_symcount (abfd);	/* # of symbols */
  symtab_offset = obj_sym_filepos (abfd);	/* symbol table file offset */

  if (num_symbols > 0)
    {
      /* Read the .debug section, if present and if we're not ignoring
	 it.  */
      if (!(objfile->flags & OBJF_READNEVER))
	{
	  struct bfd_section *secp;
	  bfd_size_type length;
	  bfd_byte *debugsec = NULL;

	  secp = bfd_get_section_by_name (abfd, ".debug");
	  if (secp)
	    {
	      length = bfd_get_section_alloc_size (abfd, secp);
	      if (length)
		{
		  debugsec
		    = (bfd_byte *) obstack_alloc (&objfile->objfile_obstack,
						  length);

		  if (!bfd_get_full_section_contents (abfd, secp, &debugsec))
		    {
		      error (_("Error reading .debug section of `%s': %s"),
			     name, bfd_errmsg (bfd_get_error ()));
		    }
		}
	    }
	  info->debugsec = (char *) debugsec;
	}
    }

  /* Read the symbols.  We keep them in core because we will want to
     access them randomly in read_symbol*.  */
  val = bfd_seek (abfd, symtab_offset, SEEK_SET);
  if (val < 0)
    error (_("Error reading symbols from %s: %s"),
	   name, bfd_errmsg (bfd_get_error ()));
  size = coff_data (abfd)->local_symesz * num_symbols;
  info->symtbl = (char *) obstack_alloc (&objfile->objfile_obstack, size);
  info->symtbl_num_syms = num_symbols;

  val = bfd_read (info->symtbl, size, abfd);
  if (val != size)
    error (_("reading symbol table: %s"), bfd_errmsg (bfd_get_error ()));

  /* We need to do this to get the TOC information only.  STABS
     format is no longer supported.  */
  scan_xcoff_symtab (objfile);

  /* DWARF2 sections.  */

  dwarf2_initialize_objfile (objfile, &dwarf2_xcoff_names);
}

static void
xcoff_symfile_offsets (struct objfile *objfile,
		       const section_addr_info &addrs)
{
  const char *first_section_name;

  default_symfile_offsets (objfile, addrs);

  /* Oneof the weird side-effects of default_symfile_offsets is that
     it sometimes sets some section indices to zero for sections that,
     in fact do not exist. See the body of default_symfile_offsets
     for more info on when that happens. Undo that, as this then allows
     us to test whether the associated section exists or not, and then
     access it quickly (without searching it again).  */

  if (objfile->section_offsets.empty ())
    return; /* Is that even possible?  Better safe than sorry.  */

  first_section_name
    = bfd_section_name (objfile->sections_start[0].the_bfd_section);

  if (objfile->sect_index_text == 0
      && strcmp (first_section_name, ".text") != 0)
    objfile->sect_index_text = -1;

  if (objfile->sect_index_data == 0
      && strcmp (first_section_name, ".data") != 0)
    objfile->sect_index_data = -1;

  if (objfile->sect_index_bss == 0
      && strcmp (first_section_name, ".bss") != 0)
    objfile->sect_index_bss = -1;

  if (objfile->sect_index_rodata == 0
      && strcmp (first_section_name, ".rodata") != 0)
    objfile->sect_index_rodata = -1;
}

/* Register our ability to parse symbols for xcoff BFD files.  */

static const struct sym_fns xcoff_sym_fns =
{

  /* It is possible that coff and xcoff should be merged as
     they do have fundamental similarities (for example, the extra storage
     classes used for stabs could presumably be recognized in any COFF file).
     However, in addition to obvious things like all the csect hair, there are
     some subtler differences between xcoffread.c and coffread.c, notably
     the fact that coffread.c has no need to read in all the symbols, but
     xcoffread.c reads all the symbols and does in fact randomly access them
     (in C_BSTAT and line number processing).  */

  xcoff_symfile_init,		/* read initial info, setup for sym_read() */
  xcoff_initial_scan,		/* read a symbol file into symtab */
  xcoff_symfile_offsets,	/* xlate offsets ext->int form */
  default_symfile_segments,	/* Get segment information from a file.  */
  default_symfile_relocate,	/* Relocate a debug section.  */
  NULL,				/* sym_probe_fns */
};

/* Same as xcoff_get_n_import_files, but for core files.  */

static int
xcoff_get_core_n_import_files (bfd *abfd)
{
  asection *sect = bfd_get_section_by_name (abfd, ".ldinfo");
  gdb_byte buf[4];
  file_ptr offset = 0;
  int n_entries = 0;

  if (sect == NULL)
    return -1;  /* Not a core file.  */

  for (offset = 0; offset < bfd_section_size (sect);)
    {
      int next;

      n_entries++;

      if (!bfd_get_section_contents (abfd, sect, buf, offset, 4))
	return -1;
      next = bfd_get_32 (abfd, buf);
      if (next == 0)
	break;  /* This is the last entry.  */
      offset += next;
    }

  /* Return the number of entries, excluding the first one, which is
     the path to the executable that produced this core file.  */
  return n_entries - 1;
}

/* Return the number of import files (shared libraries) that the given
   BFD depends on.  Return -1 if this number could not be computed.  */

int
xcoff_get_n_import_files (bfd *abfd)
{
  asection *sect = bfd_get_section_by_name (abfd, ".loader");
  gdb_byte buf[4];
  int l_nimpid;

  /* If the ".loader" section does not exist, the objfile is probably
     not an executable.  Might be a core file...  */
  if (sect == NULL)
    return xcoff_get_core_n_import_files (abfd);

  /* The number of entries in the Import Files Table is stored in
     field l_nimpid.  This field is always at offset 16, and is
     always 4 bytes long.  Read those 4 bytes.  */

  if (!bfd_get_section_contents (abfd, sect, buf, 16, 4))
    return -1;
  l_nimpid = bfd_get_32 (abfd, buf);

  /* By convention, the first entry is the default LIBPATH value
     to be used by the system loader, so it does not count towards
     the number of import files.  */
  return l_nimpid - 1;
}

INIT_GDB_FILE (xcoffread)
{
  add_symtab_fns (bfd_target_xcoff_flavour, &xcoff_sym_fns);
}
