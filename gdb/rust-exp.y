/* Bison parser for Rust expressions, for GDB.
   Copyright (C) 2016 Free Software Foundation, Inc.

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

/* Bison is way nicer than the old #define approach.  */
%define api.prefix {rust}

%{

#include "defs.h"
#include "gdb_obstack.h"
#include "gdb_regex.h"
#include "rust-lang.h"
#include "parser-defs.h"
#include "vec.h"

#ifndef RUSTDEBUG
#define	RUSTDEBUG 1		/* Default to debug support */
#endif

#define YYFPRINTF parser_fprintf

extern initialize_file_ftype _initialize_rust_exp;

static int rustlex (void);
static const char *rust_copy_name (const char *, int);
static const char *rust_concat3 (const char *, const char *, const char *);

/* The state of the parser, used internally when we are parsing the
   expression.  */
static struct parser_state *pstate = NULL;

/* A regular expression for matching Rust numbers.  This is split up
   since it is very long and this gives us a way to comment the
   sections.  */
static const char *number_regex_text =
  /* subexpression 1: allows use of alternation, otherwise uninteresting */
  "^("
  /* First comes floating point.
     "23." is a valid floating point number, but "23.e5" and
     "23.f32" are not.  So, handle the trailing-. case
     separately.  */
  "[0-9_]+\\."
  "|"
  /* Recognize number after the decimal point, with optional
     exponent and optional type suffix.
     subexpression 2: allows "?", otherwise uninteresting
     subexpression 3: if present, type suffix
  */
  "[0-9]+\\.[0-9_]+([eE][-+]?[0-9_]+)?(f32|f64)?"
#define FLOAT_TYPE1 3
  "|"
  /* Recognize exponent without decimal point, with optional type
     suffix.
     subexpression 4: if present, type suffix
  */
#define FLOAT_TYPE2 4
  "[0-9]+[eE][-+]?[0-9_]+(f32|f64)?"
  "|"
  /* First come integers.
     subexpression 5: text of integer
     subexpression 6: if present, type suffix
     subexpression 7: allows use of alternation, otherwise uninteresting
  */
#define INT_TEXT 5
#define INT_TYPE 6
  "(0x[a-fA-F0-9_]+|0o[0-7_]+|0b[01_]+|[0-9_]+)"
  "([iu](size|8|16|32|64))?"
  ")";

/* The compiled number-matching regex.  */
static regex_t number_regex;

/* True if we're running unit tests.  */
static int unit_testing;

/* Obstack for names temporarily allocated during parsing.  */
static struct obstack name_obstack;

%}

%union
{
  struct
  {
    LONGEST val;
    struct type *type;
  } typed_val_int;

  struct
  {
    DOUBLEST dval;
    struct type *type;
  } typed_val_float;

  const char *sval;

  enum exp_opcode opcode;

  struct type *type;
}

%token <sval> IDENT
%token <sval> COMPLETE
%token <typed_val_int> INTEGER
%token <typed_val_float> FLOAT
%token <opcode> COMPOUND_ASSIGN

/* Keyword tokens.  */
%token <voidval> KW_AS
%token <voidval> KW_IF
%token <voidval> KW_TRUE
%token <voidval> KW_FALSE
%token <voidval> KW_SUPER
%token <voidval> KW_SELF

/* Operator tokens.  */
%token <voidval> DOTDOT
%token <voidval> OROR
%token <voidval> ANDAND
%token <voidval> EQEQ
%token <voidval> NOTEQ
%token <voidval> LTEQ
%token <voidval> GTEQ
%token <voidval> LSH RSH
%token <voidval> COLONCOLON

%type <type> type

%type <sval> path
%type <sval> identifier_path
%type <sval> self_path
%type <sval> super_path

/* Precedence.  */
%right '='
%left DOTDOT
%left OROR
%left ANDAND
%left EQEQ NOTEQ '<' '>' LTEQ GTEQ
%left '|'
%left '^'
%left '&'
%left LSH RSH
%left '@'
%left '+' '-'
%left '*' '/' '%'
%left KW_AS

%%

start:
	expr
|	type
		{
		  write_exp_elt_opcode (pstate, OP_TYPE);
		  write_exp_elt_type (pstate, $1);
		  write_exp_elt_opcode (pstate, OP_TYPE);
		}
;

// FIXME
expr:
	literal
|	path_expr /* | tuple_expr | unit_expr | struct_expr */
|	method_call_expr
|	field_expr /* | array_expr */
|	idx_expr /* | range_expr */
|	unop_expr
|	binop_expr
|	paren_expr
|	call_expr
;

literal:
	INTEGER
		{
		  write_exp_elt_opcode (pstate, OP_LONG);
		  write_exp_elt_type (pstate, $1.type);
		  write_exp_elt_longcst (pstate, $1.val);
		  write_exp_elt_opcode (pstate, OP_LONG);
		}
|	FLOAT
		{
		  write_exp_elt_opcode (pstate, OP_DOUBLE);
		  write_exp_elt_type (pstate, $1.type);
		  write_exp_elt_dblcst (pstate, $1.dval);
		  write_exp_elt_opcode (pstate, OP_DOUBLE);
		}
|	KW_TRUE
		{
		  struct type *bool_type
		    = language_bool_type (parse_language (pstate),
					  parse_gdbarch (pstate));

		  write_exp_elt_opcode (pstate, OP_LONG);
		  write_exp_elt_type (pstate, bool_type);
		  write_exp_elt_longcst (pstate, 1);
		  write_exp_elt_opcode (pstate, OP_LONG);
		}
|	KW_FALSE
		{
		  struct type *bool_type
		    = language_bool_type (parse_language (pstate),
					  parse_gdbarch (pstate));

		  write_exp_elt_opcode (pstate, OP_LONG);
		  write_exp_elt_type (pstate, bool_type);
		  write_exp_elt_longcst (pstate, 0);
		  write_exp_elt_opcode (pstate, OP_LONG);
		}
;

field_expr:
	expr '.' IDENT
		{
		  /* gdb needs this.  */
		  struct stoken st;

		  write_exp_elt_opcode (pstate, STRUCTOP_STRUCT);
		  st.ptr = $3;
		  st.length = strlen (st.ptr);
		  write_exp_string (pstate, st);
		  write_exp_elt_opcode (pstate, STRUCTOP_STRUCT);
		}
;

method_call_expr:
	field_expr paren_expr_list
;

idx_expr:
	expr '[' expr ']'
		{ write_exp_elt_opcode (pstate, BINOP_SUBSCRIPT); }
;

unop_expr:
	'+' expr
		{ write_exp_elt_opcode (pstate, UNOP_PLUS); }

|	'-' expr
		{ write_exp_elt_opcode (pstate, UNOP_NEG); }

|	'!' expr
		{ write_exp_elt_opcode (pstate, UNOP_LOGICAL_NOT); }
;

binop_expr:
	binop_expr_expr
|	type_cast_expr
|	assignment_expr
|	compound_assignment_expr
;

binop_expr_expr:
	expr '*' expr
		{ write_exp_elt_opcode (pstate, BINOP_MUL); }

|	expr '@' expr
		{ write_exp_elt_opcode (pstate, BINOP_REPEAT); }

|	expr '/' expr
		{ write_exp_elt_opcode (pstate, BINOP_DIV); }

|	expr '%' expr
		{ write_exp_elt_opcode (pstate, BINOP_REM); }

|	expr '<' expr
		{ write_exp_elt_opcode (pstate, BINOP_LESS); }

|	expr '>' expr
		{ write_exp_elt_opcode (pstate, BINOP_GTR); }

|	expr '&' expr
		{ write_exp_elt_opcode (pstate, BINOP_BITWISE_AND); }

|	expr '|' expr
		{ write_exp_elt_opcode (pstate, BINOP_BITWISE_IOR); }

|	expr '^' expr
		{ write_exp_elt_opcode (pstate, BINOP_BITWISE_XOR); }

|	expr '+' expr
		{ write_exp_elt_opcode (pstate, BINOP_ADD); }

|	expr '-' expr
		{ write_exp_elt_opcode (pstate, BINOP_SUB); }

|	expr OROR expr
		{ write_exp_elt_opcode (pstate, BINOP_LOGICAL_OR); }

|	expr ANDAND expr
		{ write_exp_elt_opcode (pstate, BINOP_LOGICAL_AND); }

|	expr EQEQ expr
		{ write_exp_elt_opcode (pstate, BINOP_EQUAL); }

|	expr NOTEQ expr
		{ write_exp_elt_opcode (pstate, BINOP_NOTEQUAL); }

|	expr LTEQ expr
		{ write_exp_elt_opcode (pstate, BINOP_LEQ); }

|	expr GTEQ expr
		{ write_exp_elt_opcode (pstate, BINOP_GEQ); }

|	expr LSH expr
		{ write_exp_elt_opcode (pstate, BINOP_LSH); }

|	expr RSH expr
		{ write_exp_elt_opcode (pstate, BINOP_RSH); }
;

type_cast_expr:
	expr KW_AS type
		{
		  write_exp_elt_opcode (pstate, UNOP_CAST);
		  write_exp_elt_type (pstate, $3);
		  write_exp_elt_opcode (pstate, UNOP_CAST);
		}
;

assignment_expr:
	expr '=' expr
		{ write_exp_elt_opcode (pstate, BINOP_ASSIGN); }
;

compound_assignment_expr:
	expr COMPOUND_ASSIGN expr
		{
		  write_exp_elt_opcode (pstate, BINOP_ASSIGN_MODIFY);
		  write_exp_elt_opcode (pstate, $2);
		  write_exp_elt_opcode (pstate, BINOP_ASSIGN_MODIFY);
		}
;

paren_expr:
	'(' expr ')'
;

expr_list:
	expr_list ',' expr
		{ ++arglist_len; }
|	expr
		{ ++arglist_len; }
;

paren_expr_list:
	'('
		{ start_arglist (); }
	expr_list
	')'
		{
		  write_exp_elt_opcode (pstate, OP_FUNCALL);
		  write_exp_elt_longcst (pstate, end_arglist ());
		  write_exp_elt_longcst (pstate, OP_FUNCALL);
		}
;

call_expr:
	expr paren_expr_list
;

path_expr:
	path
		{ /* FIXME */ }
;

path:
	identifier_path
|	self_path
		{ error (_("paths starting with self:: not supported yet")); }
|	super_path
		{ error (_("paths starting with super:: not supported yet")); }
|	COLONCOLON identifier_path
		{ $$ = rust_concat3 ("::", $2, NULL); }
;

identifier_path:
	IDENT
|	IDENT COLONCOLON identifier_path
		{ $$ = rust_concat3 ($1, "::", $3); }
;

self_path:
	KW_SELF COLONCOLON identifier_path
		{ $$ = rust_concat3 ("self::", $3, NULL); }
|	KW_SELF COLONCOLON super_path identifier_path
		{ $$ = rust_concat3 ("self::", $3, $4); }
;

super_path:
	KW_SUPER COLONCOLON
		{ $$ = "super::"; }
|	KW_SUPER COLONCOLON super_path
		{ $$ = rust_concat3 ("super::", $3, NULL); }
;

type:
	IDENT
		{
		  $$ = lookup_typename (parse_language (pstate),
					parse_gdbarch (pstate),
					$1, NULL, 0);
		}
;


%%

/* A struct of this type is used to describe a token.  */
struct token_info
{
  const char *name;
  int value;
  enum exp_opcode opcode;
};

/* Identifier tokens.  */
static const struct token_info identifier_tokens[] =
{
  { "if", 0, OP_NULL },
  { "true", KW_TRUE, OP_NULL },
  { "false", KW_FALSE, OP_NULL },
  { "as", KW_AS, OP_NULL },
  { "super", KW_SUPER, OP_NULL },
  { "self", KW_SELF, OP_NULL }
};

/* Operator tokens, sorted longest first.  */
static const struct token_info operator_tokens[] =
{
  { ">>=", COMPOUND_ASSIGN, BINOP_RSH },
  { "<<=", COMPOUND_ASSIGN, BINOP_LSH },

  { "<<", LSH, OP_NULL },
  { ">>", RSH, OP_NULL },
  { "&&", ANDAND, OP_NULL },
  { "||", OROR, OP_NULL },
  { "==", EQEQ, OP_NULL },
  { "!=", NOTEQ, OP_NULL },
  { "<=", LTEQ, OP_NULL },
  { ">=", GTEQ, OP_NULL },
  { "+=", COMPOUND_ASSIGN, BINOP_ADD },
  { "-=", COMPOUND_ASSIGN, BINOP_SUB },
  { "*=", COMPOUND_ASSIGN, BINOP_MUL },
  { "/=", COMPOUND_ASSIGN, BINOP_DIV },
  { "%=", COMPOUND_ASSIGN, BINOP_REM },
  { "&=", COMPOUND_ASSIGN, BINOP_BITWISE_AND },
  { "|=", COMPOUND_ASSIGN, BINOP_BITWISE_IOR },
  { "^=", COMPOUND_ASSIGN, BINOP_BITWISE_XOR },

  { "::", COLONCOLON, OP_NULL }
};

/* Helper function to copy to the name obstack.  */
static const char *
rust_copy_name (const char *name, int len)
{
  return obstack_copy0 (&name_obstack, name, len);
}

/* Helper function to concatenate three strings on the name
   obstack.  */
static const char *
rust_concat3 (const char *s1, const char *s2, const char *s3)
{
  return obconcat (&name_obstack, s1, s2, s3, (char *) NULL);
}

/* Lex a hex number with at least MIN digits and at most MAX
   digits.  */
static uint32_t
lex_hex (int min, int max)
{
  uint32_t result = 0;
  int len = 0;

  while ((lexptr[0] >= 'a' && lexptr[0] <= 'f')
	 || (lexptr[0] >= 'A' && lexptr[0] <= 'F')
	 || (lexptr[0] >= '0' && lexptr[0] <= '9'))
    {
      result *= 16;
      if (lexptr[0] >= 'a' && lexptr[0] <= 'f')
	result = result + 10 + lexptr[0] - 'a';
      else if (lexptr[0] >= 'A' && lexptr[0] <= 'F')
	result = result + 10 + lexptr[0] - 'A';
      else
	result = result + lexptr[0] - '0';
      ++lexptr;
      ++len;
    }

  if (len < min)
    error (_("Not enough hex digits seen"));
  if (len > max)
    error (_("Overlong hex number"));

  return result;
}

/* Lex an escape.  IS_BYTE is true if we're lexing a byte escape;
   otherwise we're lexing a character escape.  */
static uint32_t
lex_escape (int is_byte)
{
  uint32_t result;

  gdb_assert (lexptr[0] == '\\');
  ++lexptr;
  switch (lexptr[0])
    {
    case 'x':
      ++lexptr;
      result = lex_hex (2, 2);
      break;

    case 'u':
      if (is_byte)
	error (_("Unicode escape in byte literal"));
      ++lexptr;
      if (lexptr[0] != '{')
	error (_("Missing '{' in Unicode escape"));
      ++lexptr;
      result = lex_hex (1, 6);
      /* FIXME check surrogate, other range stuff */
      if (lexptr[0] != '}')
	error (_("Missing '}' in Unicode escape"));
      ++lexptr;
      break;

    case 'n':
      result = '\n';
      break;
    case 'r':
      result = '\r';
      break;
    case 't':
      result = '\t';
      break;
    case '\\':
      result = '\\';
      break;
    case '\0':
      result = '\0';
      break;
    case '\'':
      result = '\'';
      break;
    case '"':
      result = '"';
      break;

    default:
      error (_("Invalid escape \\%c in literal"), lexptr[0]);
    }

  return result;
}

/* Lex a character constant.  */
static int
lex_character (void)
{
  int is_byte = 0;
  uint32_t value;

  if (lexptr[0] == 'b')
    {
      is_byte = 1;
      ++lexptr;
    }
  gdb_assert (lexptr[0] == '\'');
  ++lexptr;
  /* FIXME: in character case, read a whole UTF-8 character here --
     but really at a higher level we need to convert from the host
     charset to UTF-8 or maybe UTF-32.  */
  if (lexptr[0] == '\\')
    value = lex_escape (is_byte);
  else
    {
      value = lexptr[0];
      ++lexptr;
    }

  if (lexptr[0] != '\'')
    error (_("Unterminated character literal"));
  ++lexptr;

  rustlval.typed_val_int.val = value;
  rustlval.typed_val_int.type = NULL; /* FIXME */

  return INTEGER;
}

/* Return true if STR looks like the start of a raw string.  */
static int
starts_raw_string (const char *str)
{
  if (str[0] != 'r')
    return 0;
  ++str;
  while (str[0] == '#')
    ++str;
  return str[0] == '"';
}

/* Lex a string constant.  */
static int
lex_string (void)
{
  error (_("string lexing unimplemented"));
}

/* Return true if STRING starts with whitespace followed by a digit.  */
static int
space_then_number (const char *string)
{
  const char *p = string;

  while (p[0] == ' ' || p[0] == '\t')
    ++p;
  if (p == string)
    return 0;

  return *p >= '0' && *p <= '9';
}

/* Lex an identifier.  */
static int
lex_identifier (void)
{
  const char *start = lexptr;
  unsigned int length;
  const struct token_info *token;
  int i;

  gdb_assert ((lexptr[0] >= 'a' && lexptr[0] <= 'z')
	      || (lexptr[0] >= 'A' && lexptr[0] <= 'Z')
	      || lexptr[0] == '_');

  ++lexptr;

  /* FIXME Unicode rules */
  while ((lexptr[0] >= 'a' && lexptr[0] <= 'z')
	 || (lexptr[0] >= 'A' && lexptr[0] <= 'Z')
	 || lexptr[0] == '_'
	 || (lexptr[0] >= '0' && lexptr[0] <= '9'))
    ++lexptr;


  length = lexptr - start;
  token = NULL;
  for (i = 0; i < ARRAY_SIZE (identifier_tokens); ++i)
    {
      if (length == strlen (identifier_tokens[i].name)
	  && strncmp (identifier_tokens[i].name, start, length) == 0)
	{
	  token = &identifier_tokens[i];
	  break;
	}
    }

  if (token == NULL)
    {
      if ((strncmp (start, "thread", length) == 0
	   || strncmp (start, "task", length) == 0)
	  && space_then_number (lexptr))
	{
	  /* "task" or "thread" followed by a number terminates the
	     parse, per gdb rules.  */
	  lexptr = start;
	  return 0;
	}
    }
  else
    {
      if (token->value == 0)
	{
	  /* Leave the terminating token alone.  */
	  lexptr = start;
	}

      return token->value;
    }

  rustlval.sval = rust_copy_name (start, length);

  /* Slightly weird that we don't allow completion if the text happens
     to be a token.  */
  if (parse_completion && lexptr[0] == '\0')
    return COMPLETE;
  return IDENT;
}

/* Lex an operator.  */
static int
lex_operator (void)
{
  const struct token_info *token = NULL;
  int i;

  for (i = 0; i < ARRAY_SIZE (operator_tokens); ++i)
    {
      if (strncmp (operator_tokens[i].name, lexptr,
		   strlen (operator_tokens[i].name)) == 0)
	{
	  lexptr += strlen (operator_tokens[i].name);
	  token = &operator_tokens[i];
	  break;
	}
    }

  if (token != NULL)
    {
      rustlval.opcode = token->opcode;
      return token->value;
    }

  return *lexptr++;
}

/* Lex a number.  */
static int
lex_number (void)
{
  regmatch_t subexps[8];
  int match;
  int is_integer = 0;
  char *typename = NULL;
  struct type *type;
  int end_index;
  int type_index = -1;
  int i, out;
  char *number;
  struct cleanup *cleanup = make_cleanup (null_cleanup, NULL);

  match = regexec (&number_regex, lexptr, ARRAY_SIZE (subexps), subexps, 0);
  /* Failure means the regexp is broken.  */
  gdb_assert (!match);

  if (subexps[INT_TEXT].rm_so != -1)
    {
      /* Integer part matched.  */
      is_integer = 1;
      end_index = subexps[INT_TEXT].rm_eo;
      if (subexps[INT_TYPE].rm_so == -1)
	typename = "i32";
      else
	type_index = INT_TYPE;
    }
  else if (subexps[FLOAT_TYPE1].rm_so != -1)
    {
      /* Found floating point type suffix.  */
      end_index = subexps[FLOAT_TYPE1].rm_so;
      type_index = FLOAT_TYPE1;
    }
  else if (subexps[FLOAT_TYPE2].rm_so != -1)
    {
      /* Found floating point type suffix.  */
      end_index = subexps[FLOAT_TYPE2].rm_so;
      type_index = FLOAT_TYPE2;
    }
  else
    {
      /* Any other floating point match.  */
      end_index = subexps[0].rm_eo;
      typename = "f64";
    }

  /* Compute the type name if we haven't already.  */
  if (typename == NULL)
    {
      gdb_assert (type_index != -1);
      typename = xstrndup (lexptr + subexps[type_index].rm_so,
			   (subexps[type_index].rm_eo
			    - subexps[type_index].rm_so));
      make_cleanup (xfree, typename);
    }

  /* Look up the type.  */
  if (unit_testing)
    type = NULL;
  else
    {
      type = language_lookup_primitive_type (parse_language (pstate),
					     parse_gdbarch (pstate),
					     typename);
      if (type == NULL)
	error (_("could not find Rust type %s"), typename);
    }

  /* Copy the text of the number and remove the "_"s.  */
  number = xstrndup (lexptr, end_index);
  make_cleanup (xfree, number);
  for (i = out = 0; number[i]; ++i)
    {
      if (number[i] != '_')
	number[out++] = number[i];
    }
  number[out] = '\0';

  /* Advance past the match.  */
  lexptr += subexps[0].rm_eo;

  /* Parse the number.  */
  if (is_integer)
    {
      int radix = 10;
      if (number[0] == '0')
	{
	  if (number[1] == 'x')
	    radix = 16;
	  else if (number[1] == 'o')
	    radix = 8;
	  else if (number[1] == 'b')
	    radix = 2;
	  if (radix != 10)
	    number += 2;
	}
      rustlval.typed_val_int.val = strtoul (number, NULL, radix);
      rustlval.typed_val_int.type = type;
    }
  else
    {
      rustlval.typed_val_float.dval = strtod (number, NULL);
      rustlval.typed_val_float.type = type;
    }

  do_cleanups (cleanup);
  return is_integer ? INTEGER : FLOAT;
}

/* The lexer.  */
static int
rustlex (void)
{
  /* Skip all leading whitespace.  */
  while (lexptr[0] == ' ' || lexptr[0] == '\t' || lexptr[0] == '\r'
	 || lexptr[0] == '\n')
    ++lexptr;

  prev_lexptr = lexptr;
  if (lexptr[0] == 0)
    return 0;

  if (lexptr[0] >= '0' && lexptr[0] <= '9')
    return lex_number ();
  else if (lexptr[0] == 'b' && lexptr[1] == '\'')
    return lex_character ();
  else if (lexptr[0] == 'b' && lexptr[1] == '"')
    return lex_string ();
  else if (lexptr[0] == 'b' && starts_raw_string (lexptr + 1))
    return lex_string ();
  else if (starts_raw_string (lexptr))
    return lex_string ();
  else if ((lexptr[0] >= 'a' && lexptr[0] <= 'z')
	   || (lexptr[0] >= 'A' && lexptr[0] <= 'Z')
	   || lexptr[0] == '_')
    return lex_identifier ();
  else if (lexptr[0] == '"')
    return lex_string ();
  else if (lexptr[0] == '\'')
    return lex_character ();
  else if (lexptr[0] == '}' || lexptr[0] == ']')
    {
      /* Falls through to lex_operator.  */
      --paren_depth;
    }
  else if (lexptr[0] == '(' || lexptr[0] == '{')
    {
      /* Falls through to lex_operator.  */
      ++paren_depth;
    }
  else if (lexptr[0] == ',' && comma_terminates && paren_depth == 0)
    return 0;

  return lex_operator ();
}

/* The parser as exposed to gdb.  */
int
rust_parse (struct parser_state *state)
{
  int result;
  struct cleanup *cleanup;

  obstack_init (&name_obstack);
  cleanup = make_cleanup_obstack_free (&name_obstack);

  pstate = state;
  result = rustparse ();

  do_cleanups (cleanup);
  return result;
}

/* The parser error handler.  */
void
rusterror (char *msg)
{
  const char *where = prev_lexptr ? prev_lexptr : lexptr;
  error (_("A %s in expression, near `%s'."), (msg ? msg : "error"), where);
}


#define GDB_UNIT_TEST /* FIXME */
#ifdef GDB_UNIT_TEST

/* A test helper that lexes a string, expecting a single token.  It
   returns the lexer data for this token.  */
static RUSTSTYPE
rust_lex_test_one (const char *input, int expected)
{
  int token;
  RUSTSTYPE result;

  lexptr = input;
  paren_depth = 0;

  token = rustlex ();
  gdb_assert (token == expected);
  result = yylval;

  if (token)
    {
      token = rustlex ();
      gdb_assert (token == 0);
    }

  return result;
}

/* Test that INPUT lexes as the integer VALUE.  */
static void
rust_lex_int_test (const char *input, int value)
{
  RUSTSTYPE result = rust_lex_test_one (input, INTEGER);
  gdb_assert (result.typed_val_int.val == value);
}

/* Test that INPUT lexes as the identifier VALUE.  */
static void
rust_lex_ident_test (const char *input, const char *value)
{
  RUSTSTYPE result = rust_lex_test_one (input, IDENT);
  gdb_assert (strcmp (result.sval, value) == 0);
}

/* Unit test the lexer.  */
static void
rust_lex_tests (void)
{
  int i;

  rust_lex_test_one ("", 0);
  rust_lex_test_one ("thread 23", 0);
  rust_lex_test_one ("task 23", 0);
  rust_lex_test_one ("th 104", 0);
  rust_lex_test_one ("ta 97", 0);

  /* FIXME check error cases */
  rust_lex_int_test ("'z'", 'z');
  rust_lex_int_test ("'\\xff'", 0xff);
  rust_lex_int_test ("'\\u{1016f}'", 0x1016f);
  rust_lex_int_test ("b'z'", 'z');
  rust_lex_int_test ("b'\\xfe'", 0xfe);

  rust_lex_int_test ("23", 23);
  rust_lex_int_test ("2_344__29", 234429);
  rust_lex_int_test ("0x1f", 0x1f);
  rust_lex_int_test ("23usize", 23);
  rust_lex_int_test ("23i32", 23);
  rust_lex_int_test ("0x1_f", 0x1f);
  rust_lex_int_test ("0b1_101011__", 0x6b);
  rust_lex_int_test ("0o001177i64", 639);

  rust_lex_test_one ("23.", FLOAT);
  rust_lex_test_one ("23.99f32", FLOAT);
  rust_lex_test_one ("23e7", FLOAT);
  rust_lex_test_one ("23E-7", FLOAT);
  rust_lex_test_one ("23e+7", FLOAT);
  rust_lex_test_one ("23.99e+7f64", FLOAT);
  rust_lex_test_one ("23.82f32", FLOAT);

  rust_lex_ident_test ("hibob", "hibob");
  rust_lex_ident_test ("hibob__93", "hibob__93");
  rust_lex_ident_test ("thread", "thread");

  for (i = 0; i < ARRAY_SIZE (identifier_tokens); ++i)
    rust_lex_test_one (identifier_tokens[i].name, identifier_tokens[i].value);

  for (i = 0; i < ARRAY_SIZE (operator_tokens); ++i)
    rust_lex_test_one (operator_tokens[i].name, operator_tokens[i].value);
}

#endif

void
_initialize_rust_exp (void)
{
  int code = regcomp (&number_regex, number_regex_text, REG_EXTENDED);
  if (code != 0)
    {
      char *err = get_regcomp_error (code, &number_regex);

      make_cleanup (xfree, err);
      error (_("_initialize_rust_exp: could not compile regex: %s"), err);
    }

  /* It would be great if gdb had a "maint selftest" command; modules
     could register unit test functions and this command would simply
     invoke them, barfing on exceptions or checking return
     results.  */
#ifdef GDB_UNIT_TEST
  obstack_init (&name_obstack);
  unit_testing = 1;
  rust_lex_tests ();
  obstack_free (&name_obstack, NULL);
  unit_testing = 0;
#endif
}
