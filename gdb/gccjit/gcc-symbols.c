/* Convert symbols from GDB to GCC

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
#include "gccjit-internal.h"
#include "gdb_assert.h"
#include "symtab.h"
#include "parser-defs.h"
#include "block.h"

static void
convert_one_symbol (struct gdb_gcc_instance *context,
		    struct symbol *sym,
		    int is_global)
{
  gcc_type sym_type;

  if (SYMBOL_CLASS (sym) == LOC_LABEL)
    sym_type = NULL;
  else
    sym_type = convert_type (context, SYMBOL_TYPE (sym));

  if (SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN)
    {
      /* Binding a tag, so we don't need to build a decl.  */
      context->fe->ops->tagbind (context->fe,
				 SYMBOL_NATURAL_NAME (sym), sym_type);
    }
  else
    {
      const char *filename = SYMBOL_SYMTAB (sym)->filename;
      unsigned short line = SYMBOL_LINE (sym);
      gcc_decl decl;
      enum gcc_c_symbol_kind kind;
      CORE_ADDR addr = 0;

      switch (SYMBOL_CLASS (sym))
	{
	case LOC_TYPEDEF:
	  kind = GCC_C_SYMBOL_TYPEDEF;
	  break;

	case LOC_LABEL:
	  kind = GCC_C_SYMBOL_LABEL;
	  addr = SYMBOL_VALUE_ADDRESS (sym);
	  break;

	case LOC_BLOCK:
	  kind = GCC_C_SYMBOL_FUNCTION;
	  addr = BLOCK_START (SYMBOL_BLOCK_VALUE (sym));
	  break;

	case LOC_UNDEF:
	case LOC_COMMON_BLOCK:
	case LOC_OPTIMIZED_OUT:
	  /* FIXME: some kind of error here.  */
	  return;

	case LOC_FINAL_VALUE:
	  gdb_assert_not_reached ("unreachable case in convert_one_symbol");

	default:
	  kind = GCC_C_SYMBOL_VARIABLE;
	  /* FIXME: doesn't work for local and register variables
	     yet.  */
	  addr = SYMBOL_VALUE_ADDRESS (sym);
	  break;
	}

      decl = context->fe->ops->build_decl (context->fe,
					   SYMBOL_NATURAL_NAME (sym), kind,
					   sym_type, addr, filename, line);

      context->fe->ops->bind (context->fe, decl, is_global);
    }
}

void
gcc_convert_symbol (void *datum,
		    struct gcc_context *gcc_context,
		    enum gcc_c_oracle_request request,
		    const char *identifier)
{
  struct gdb_gcc_instance *context = datum;
  struct symbol *sym, *global_sym;
  domain_enum domain;
  const struct block *static_block, *found_block;

  switch (request)
    {
    case GCC_C_ORACLE_SYMBOL:
      domain = VAR_DOMAIN;
      break;
    case GCC_C_ORACLE_TAG:
      domain = STRUCT_DOMAIN;
      break;
    case GCC_C_ORACLE_LABEL:
      domain = LABEL_DOMAIN;
      break;
    default:
      gdb_assert_not_reached ("unrecognized oracle request");
    }

  sym = lookup_symbol (identifier, context->block, domain, NULL);
  if (sym == NULL)
    return;
  found_block = block_found;

  /* If we found a symbol and it is not in the  static or global
     scope, then we should first convert any static or global scope
     symbol of the same name.  This lets this unusual case work:

     int x; // Global.
     int func(void)
     {
     int x;
     // At this spot, evaluate "extern int x; x"
     }
  */

  static_block = block_static_block (found_block);
  /* STATIC_BLOCK is NULL if FOUND_BLOCK is the global block.  */
  if (found_block != static_block && static_block != NULL)
    {
      struct symbol *global_sym;

      global_sym = lookup_symbol (identifier, NULL, domain, NULL);
      /* FIXME: should we exclude the static block here?  Must look
	 up.  */
      if (global_sym != NULL)
	convert_one_symbol (context, global_sym, 1);
    }

  convert_one_symbol (context, sym, 0);
}
