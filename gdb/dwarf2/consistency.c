/* Consistency checking for cooked index

   Copyright (C) 2025 Free Software Foundation, Inc.

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

#include "block.h"
#include "event-top.h"
#include "dwarf2/cooked-index.h"
#include "dwarf2/tag.h"

/* Helper function to return the domain flags corresponding to a given
   DWARF tag and language.  */

static domain_search_flags
tag_domain (dwarf_tag tag, language lang)
{
  /* This is a slightly funny implementation but it perhaps also helps
     ensure some consistency as well.  */
#define SYM_DOMAIN(X)						\
  if (tag_matches_domain (tag, SEARCH_ ## X ## _DOMAIN, lang))	\
    return SEARCH_ ## X ## _DOMAIN;
#include "sym-domains.def"
#undef SYM_DOMAIN

  return 0;
}

/* See quick_symbol_functions::consistency_check in
   quick-symbol.h.  */

void
cooked_index_functions::consistency_check (objfile *objfile)
{
  dwarf2_per_objfile *per_objfile = get_dwarf2_per_objfile (objfile);
  cooked_index *table = wait (objfile, true);

  for (const cooked_index_entry *entry : table->all_entries ())
    {
      QUIT;

      compunit_symtab *cust = per_objfile->get_symtab (entry->per_cu);
      /* Skip un-expanded CUs.  */
      if (cust == nullptr)
	continue;

      const blockvector *bv = cust->blockvector ();
      const block *b = ((entry->flags & IS_STATIC)
			? bv->static_block ()
			: bv->global_block ());

      auto_obstack storage;
      const char *name = entry->full_name (&storage);

      lookup_name_info lookup_name (name, symbol_name_match_type::FULL);
      symbol *sym = block_lookup_symbol (b, lookup_name,
					 tag_domain (entry->tag,
						     entry->lang));
      if (sym == nullptr)
	{
	  gdb_printf ("Symbol '%s' in index but not in symtab '%s'.\n",
		      name, cust->name);
	  continue;
	}

      if (!entry->matches (to_search_flags (sym->domain ())))
	gdb_printf ("Index entry '%s' does not match symbol's domain.\n",
		    name);

      /* We don't preserve enough linkage name information for this to
	 work right now.  */
      if ((entry->flags & IS_LINKAGE) != 0)
	continue;

      lookup_name_info symname (sym->search_name (),
				symbol_name_match_type::FULL);
      lookup_name_info without_params = symname.make_ignore_params ();
      const char *lang_name = without_params.language_lookup_name (entry->lang);
      if (cooked_index_entry::compare (name, lang_name,
				       cooked_index_entry::MATCH) != 0)
	gdb_printf ("Index entry '%s' does not match symbol name '%s'.\n",
		    name, lang_name);
    }
}
