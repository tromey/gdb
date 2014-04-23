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
#include "compile.h"
#include "value.h"

/* Compute the name of the pointer representing a local symbol's
   address.  */

static char *
symbol_substitution_name (struct symbol *sym)
{
  return concat ("__", SYMBOL_NATURAL_NAME (sym), "_ptr", (char *) NULL);
}

static void
convert_one_symbol (struct compile_c_instance *context,
		    struct symbol *sym,
		    int is_global)
{
  gcc_type sym_type;
  const char *filename = SYMBOL_SYMTAB (sym)->filename;
  unsigned short line = SYMBOL_LINE (sym);

  if (SYMBOL_CLASS (sym) == LOC_LABEL)
    sym_type = 0;
  else
    sym_type = convert_type (context, SYMBOL_TYPE (sym));

  if (SYMBOL_DOMAIN (sym) == STRUCT_DOMAIN)
    {
      /* Binding a tag, so we don't need to build a decl.  */
      C_CTX (context)->c_ops->tagbind (C_CTX (context),
				       SYMBOL_NATURAL_NAME (sym),
				       sym_type, filename, line);
    }
  else
    {
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

	case LOC_CONST:
	  if (TYPE_CODE (SYMBOL_TYPE (sym)) == TYPE_CODE_ENUM)
	    {
	      /* Already handled by convert_enum.  */
	      return;
	    }
	  C_CTX (context)->c_ops->build_constant (C_CTX (context), sym_type,
						  SYMBOL_NATURAL_NAME (sym),
						  SYMBOL_VALUE (sym),
						  filename, line);
	  return;

	case LOC_CONST_BYTES:	/* FIXME */
	  error (_("Internal error: Unsupported LOC_CONST_BYTES for \"%s\"."),
		 SYMBOL_PRINT_NAME (sym));

	case LOC_UNDEF:
	  internal_error (__FILE__, __LINE__, _("LOC_UNDEF found for \"%s\"."),
			  SYMBOL_PRINT_NAME (sym));

	case LOC_COMMON_BLOCK:
	  error (_("Fortran common block is unsupported for compilation "
		   "evaluaton of symbol \"%s\"."),
		 SYMBOL_PRINT_NAME (sym));

	case LOC_OPTIMIZED_OUT:
	  error (_("Symbol \"%s\" cannot be used for compilation evaluation "
		   "as it is optimized out."),
		 SYMBOL_PRINT_NAME (sym));

	case LOC_UNRESOLVED:
	  {
	    // FIXME: Why does GCC crash using kind && symbol_name as below?
	    struct value *val = read_var_value (sym, NULL);

	    if (VALUE_LVAL (val) != lval_memory)
	      error (_("Symbol \"%s\" cannot be used for compilation evaluation "
		       "as its address has not been found."),
		     SYMBOL_PRINT_NAME (sym));

	    kind = GCC_C_SYMBOL_VARIABLE;
	    addr = value_address (val);
	  }
	  break;

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

      decl = C_CTX (context)->c_ops->build_decl (C_CTX (context),
						 SYMBOL_NATURAL_NAME (sym),
						 kind,
						 sym_type,
						 symbol_name, addr,
						 filename, line);

      C_CTX (context)->c_ops->bind (C_CTX (context), decl, is_global);

      xfree (symbol_name);
    }
}

static void
convert_symbol_sym (struct compile_c_instance *context, const char *identifier,
		    struct symbol *sym, domain_enum domain)
{
  const struct block *static_block, *found_block;

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
	{
	  if (compile_debug)
	    fprintf_unfiltered (gdb_stdout,
				"gcc_convert_symbol \"%s\": global symbol\n",
				identifier);
	  convert_one_symbol (context, global_sym, 1);
	}
    }

  if (compile_debug)
    fprintf_unfiltered (gdb_stdout,
			"gcc_convert_symbol \"%s\": local symbol\n",
			identifier);
  convert_one_symbol (context, sym, 0);
}

static void
convert_symbol_bmsym (struct compile_c_instance *context,
		      struct bound_minimal_symbol bmsym)
{
  struct minimal_symbol *msym = bmsym.minsym;
  struct objfile *objfile = bmsym.objfile;
  struct type *type;
  enum gcc_c_symbol_kind kind;
  gcc_type sym_type;
  gcc_decl decl;
  CORE_ADDR addr;

  /* Conversion copied from write_exp_msymbol.  */
  switch (MSYMBOL_TYPE (msym))
    {
    case mst_text:
    case mst_file_text:
    case mst_solib_trampoline:
      type = objfile_type (objfile)->nodebug_text_symbol;
      kind = GCC_C_SYMBOL_FUNCTION;
      break;

    case mst_text_gnu_ifunc:
      type = objfile_type (objfile)->nodebug_text_gnu_ifunc_symbol;
      kind = GCC_C_SYMBOL_FUNCTION;
      break;

    case mst_data:
    case mst_file_data:
    case mst_bss:
    case mst_file_bss:
      type = objfile_type (objfile)->nodebug_data_symbol;
      kind = GCC_C_SYMBOL_VARIABLE;
      break;

    case mst_slot_got_plt:
      type = objfile_type (objfile)->nodebug_got_plt_symbol;
      kind = GCC_C_SYMBOL_FUNCTION;
      break;

    default:
      type = objfile_type (objfile)->nodebug_unknown_symbol;
      kind = GCC_C_SYMBOL_VARIABLE;
      break;
    }

  sym_type = convert_type (context, type);
  addr = MSYMBOL_VALUE_ADDRESS (objfile, msym);
  decl = C_CTX (context)->c_ops->build_decl (C_CTX (context),
					     MSYMBOL_NATURAL_NAME (msym),
					     kind, sym_type, NULL, addr,
					     NULL, 0);
  C_CTX (context)->c_ops->bind (C_CTX (context), decl, 1 /* is_global */);
}

void
gcc_convert_symbol (void *datum,
		    struct gcc_c_context *gcc_context,
		    enum gcc_c_oracle_request request,
		    const char *identifier)
{
  struct compile_c_instance *context = datum;
  domain_enum domain;
  volatile struct gdb_exception e;
  int found = 0;

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

  /* We can't allow exceptions to escape out of this callback.  Safest
     is to simply emit a gcc error.  */
  TRY_CATCH (e, RETURN_MASK_ERROR)
    {
      struct symbol *sym;

      sym = lookup_symbol (identifier, context->base.block, domain, NULL);
      if (sym != NULL)
	{
	  convert_symbol_sym (context, identifier, sym, domain);
	  found = 1;
	}
      else if (domain == VAR_DOMAIN)
	{
	  struct bound_minimal_symbol bmsym;

	  bmsym = lookup_minimal_symbol (identifier, NULL, NULL);
	  if (bmsym.minsym != NULL)
	    {
	      convert_symbol_bmsym (context, bmsym);
	      found = 1;
	    }
	}
    }

  if (e.reason < 0)
    C_CTX (context)->c_ops->error (C_CTX (context), e.message);

  if (compile_debug && !found)
    fprintf_unfiltered (gdb_stdout,
			"gcc_convert_symbol \"%s\": lookup_symbol failed\n",
			identifier);
  return;
}

gcc_address
gcc_symbol_address (void *datum, struct gcc_c_context *gcc_context,
		    const char *identifier)
{
  struct compile_c_instance *context = datum;
  volatile struct gdb_exception e;
  gcc_address result = 0;
  int found = 0;

  /* We can't allow exceptions to escape out of this callback.  Safest
     is to simply emit a gcc error.  */
  TRY_CATCH (e, RETURN_MASK_ERROR)
    {
      struct symbol *sym;

      /* We only need global functions here.  */
      sym = lookup_symbol (identifier, NULL, VAR_DOMAIN, NULL);
      if (sym != NULL && SYMBOL_CLASS (sym) == LOC_BLOCK)
	{
	  if (compile_debug)
	    fprintf_unfiltered (gdb_stdout,
				"gcc_symbol_address \"%s\": full symbol\n",
				identifier);
	  result = BLOCK_START (SYMBOL_BLOCK_VALUE (sym));
	  found = 1;
	}
      else
	{
	  struct bound_minimal_symbol msym;

	  msym = lookup_bound_minimal_symbol (identifier);
	  if (msym.minsym != NULL)
	    {
	      if (compile_debug)
		fprintf_unfiltered (gdb_stdout,
				    "gcc_symbol_address \"%s\": minimal "
				    "symbol\n",
				    identifier);
	      result = BMSYMBOL_VALUE_ADDRESS (msym);
	      found = 1;
	    }
	}
    }

  if (e.reason < 0)
    C_CTX (context)->c_ops->error (C_CTX (context), e.message);

  if (compile_debug && !found)
    fprintf_unfiltered (gdb_stdout,
			"gcc_symbol_address \"%s\": failed\n",
			identifier);
  return result;
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
