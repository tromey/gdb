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
#include "compile-internal.h"
#include "gdb_assert.h"
#include "symtab.h"
#include "parser-defs.h"
#include "block.h"
#include "objfiles.h"

/* Compute the name of the pointer representing a local symbol's
   address.  */

static char *
symbol_substitution_name (struct symbol *sym)
{
  return concat ("__", SYMBOL_NATURAL_NAME (sym), "_ptr", (char *) NULL);
}

static void
convert_one_symbol (struct compile_instance *context,
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
      char *symbol_name = NULL;

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

	case LOC_CONST:		/* FIXME */
	case LOC_CONST_BYTES:	/* FIXME */

	case LOC_UNDEF:
	case LOC_COMMON_BLOCK:
	case LOC_OPTIMIZED_OUT:
	case LOC_UNRESOLVED:
	  /* FIXME: some kind of error here.  */
	  return;

	case LOC_REGISTER:
	case LOC_ARG:
	case LOC_REF_ARG:
	case LOC_REGPARM_ADDR:
	case LOC_LOCAL:
	case LOC_COMPUTED:
	  kind = GCC_C_SYMBOL_VARIABLE;
	  symbol_name = symbol_substitution_name (sym);
	  break;

	case LOC_STATIC:
	  kind = GCC_C_SYMBOL_VARIABLE;
	  addr = SYMBOL_VALUE_ADDRESS (sym);
	  break;

	case LOC_FINAL_VALUE:
	default:
	  gdb_assert_not_reached ("unreachable case in convert_one_symbol");

	}

      decl = context->fe->ops->build_decl (context->fe,
					   SYMBOL_NATURAL_NAME (sym), kind,
					   sym_type,
					   symbol_name, addr,
					   filename, line);

      context->fe->ops->bind (context->fe, decl, is_global);

      xfree (symbol_name);
    }
}

void
gcc_convert_symbol (void *datum,
		    struct gcc_context *gcc_context,
		    enum gcc_c_oracle_request request,
		    const char *identifier)
{
  struct compile_instance *context = datum;
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

gcc_address
gcc_symbol_address (void *datum, struct gcc_context *gcc_context,
		    const char *identifier)
{
  struct compile_instance *context = datum;
  struct symbol *sym;
  struct bound_minimal_symbol msym;

  /* We only need global functions here.  */
  sym = lookup_symbol (identifier, NULL, VAR_DOMAIN, NULL);
  if (sym != NULL && SYMBOL_CLASS (sym) == LOC_BLOCK)
    return BLOCK_START (SYMBOL_BLOCK_VALUE (sym));

  msym = lookup_bound_minimal_symbol (identifier);
  if (msym.minsym != NULL)
    return BMSYMBOL_VALUE_ADDRESS (msym);

  return 0;
}



/* A hash function for symbol names.  */

static hashval_t
hash_symname (const void *a)
{
  const struct symbol *sym = a;

  return htab_hash_string (SYMBOL_NATURAL_NAME (sym));
}

/* A comparison function for hash tables that just looks at symbol
   names.  */

static int
eq_symname (const void *a, const void *b)
{
  const struct symbol *syma = a;
  const struct symbol *symb = b;

  return strcmp (SYMBOL_NATURAL_NAME (syma), SYMBOL_NATURAL_NAME (symb)) == 0;
}

/* If a symbol with the same name as SYM is already in HASHTAB, return
   1.  Otherwise, add SYM to HASHTAB and return 0.  */

static int
symbol_seen (htab_t hashtab, struct symbol *sym)
{
  void **slot;

  slot = htab_find_slot (hashtab, sym, INSERT);
  if (*slot != NULL)
    return 1;

  *slot = sym;
  return 0;
}

/* Generate C code to compute the address of SYM.  */

static void
generate_c_for_for_one_variable (struct ui_file *stream,
				 struct gdbarch *gdbarch,
				 unsigned char *registers_used,
				 CORE_ADDR pc,
				 struct symbol *sym)
{
  if (SYMBOL_COMPUTED_OPS (sym) != NULL)
    {
      char *generated_name = symbol_substitution_name (sym);
      struct cleanup *cleanup = make_cleanup (xfree, generated_name);

      SYMBOL_COMPUTED_OPS (sym)->generate_c_location (sym, stream, gdbarch,
						      registers_used,
						      pc, generated_name);
      do_cleanups (cleanup);
      return;
    }

  switch (SYMBOL_CLASS (sym))
    {
    case LOC_REGISTER:
    case LOC_ARG:
    case LOC_REF_ARG:
    case LOC_REGPARM_ADDR:
    case LOC_LOCAL:
      error (_("local symbol unhandled when generating C code"));

    case LOC_COMPUTED:
      gdb_assert_not_reached (_("LOC_COMPUTED variable missing a method"));

    default:
      /* Nothing to do for all other cases, as they don't represent
	 local variables.  */
      break;
    }
}

/* Emit code to compute the address for all the local variables in
   scope at PC in BLOCK.  Returns a malloc'd vector, indexed by gdb
   register number, where each element indicates if the corresponding
   register is needed to compute a local variable.  */

unsigned char *
generate_c_for_variable_locations (struct ui_file *stream,
				   struct gdbarch *gdbarch,
				   const struct block *block,
				   CORE_ADDR pc)
{
  struct cleanup *cleanup, *outer;
  htab_t symhash;
  const struct block *static_block = block_static_block (block);
  unsigned char *registers_used;

  /* If we're already in the static or global block, there is nothing
     to write.  */
  if (static_block == NULL || block == static_block)
    return NULL;

  registers_used = XCNEWVEC (unsigned char, gdbarch_num_regs (gdbarch));
  outer = make_cleanup (xfree, registers_used);

  /* Ensure that a given name is only entered once.  This reflects the
     reality of shadowing.  */
  symhash = htab_create_alloc (1, hash_symname, eq_symname, NULL,
			       xcalloc, xfree);
  cleanup = make_cleanup_htab_delete (symhash);

  while (1)
    {
      struct symbol *sym;
      struct block_iterator iter;

      /* Iterate over symbols in this block, generating code to
	 compute the location of each local variable.  */
      for (sym = block_iterator_first (block, &iter);
	   sym != NULL;
	   sym = block_iterator_next (&iter))
	{
	  if (!symbol_seen (symhash, sym))
	    generate_c_for_for_one_variable (stream, gdbarch, registers_used,
					     pc, sym);
	}

      /* If we just finished the outermost block of a function, we're
	 done.  */
      if (BLOCK_FUNCTION (block) != NULL)
	break;
      block = BLOCK_SUPERBLOCK (block);
    }

  do_cleanups (cleanup);
  discard_cleanups (outer);
  return registers_used;
}
