#define INCLUDE_VECTOR
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "diagnostic-core.h"

#include "hsharp.h"

#define HS_ESC_LBRACE '\x01'
#define HS_ESC_RBRACE '\x02'

namespace {

struct parser
{
  const std::vector<hs_token> &t;
  size_t i;
  int errors;

  parser (const std::vector<hs_token> &toks) : t (toks), i (0), errors (0) {}

  const hs_token &cur () const { return t[i]; }
  hs_tok kind () const { return t[i].kind; }
  hs_tok kind_at (size_t k) const
  { return i + k < t.size () ? t[i + k].kind : T_EOF; }
  void next () { if (t[i].kind != T_EOF) i++; }
  bool accept (hs_tok k) { if (kind () == k) { next (); return true; } return false; }

  void fail (location_t loc, const char *msg)
  {
    errors++;
    error_at (loc, "%s", msg);
  }

  bool expect (hs_tok k, const char *what)
  {
    if (accept (k))
      return true;
    errors++;
    error_at (cur ().loc, "expected %s", what);
    return false;
  }

  /* ---------------- types ---------------- */

  bool parse_type (hs_type &out)
  {
    if (kind () != T_IDENT)
      {
	fail (cur ().loc, "expected a type name");
	return false;
      }
    const std::string &n = cur ().text;
    struct { const char *name; hs_type type; } tbl[] = {
      {"int", HT_I64}, {"uint", HT_U64}, {"i8", HT_I8}, {"i16", HT_I16},
      {"i32", HT_I32}, {"i64", HT_I64}, {"u8", HT_U8}, {"u16", HT_U16},
      {"u32", HT_U32}, {"u64", HT_U64}, {"f32", HT_F32}, {"f64", HT_F64},
      {"bool", HT_BOOL}, {"string", HT_STRING}, {"void", HT_VOID},
    };
    for (auto &e : tbl)
      if (n == e.name)
	{
	  out = e.type;
	  next ();
	  return true;
	}
    errors++;
    error_at (cur ().loc, "unknown or unsupported type %qs", n.c_str ());
    next ();
    return false;
  }

  /* ---------------- expressions ---------------- */

  hs_expr *mk (hs_ekind k, location_t loc) { return new hs_expr (k, loc); }

  hs_expr *binary (hs_tok op, hs_expr *l, hs_expr *r, location_t loc)
  {
    hs_expr *e = mk (E_BINARY, loc);
    e->op = op;
    e->lhs = l;
    e->rhs = r;
    return e;
  }

  hs_expr *string_literal (const hs_token &tok)
  {
    /* Split "text {name} text" into a '+' chain of E_STR and
       to_string(name) calls.  */
    std::vector<hs_expr *> parts;
    std::string lit;
    const std::string &s = tok.text;
    for (size_t k = 0; k < s.size (); k++)
      {
	char c = s[k];
	if (c == HS_ESC_LBRACE) lit += '{';
	else if (c == HS_ESC_RBRACE) lit += '}';
	else if (c == '{')
	  {
	    size_t close = s.find ('}', k);
	    std::string name = close == std::string::npos
	      ? "" : s.substr (k + 1, close - k - 1);
	    bool ok = !name.empty ();
	    for (char ch : name)
	      if (!(ch == '_' || (ch >= 'a' && ch <= 'z')
		    || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
		ok = false;
	    if (!ok)
	      {
		fail (tok.loc, "only {identifier} interpolation is supported "
		      "in string literals (escape a brace with \\{)");
		return mk (E_STR, tok.loc);
	      }
	    if (!lit.empty ())
	      {
		hs_expr *e = mk (E_STR, tok.loc);
		e->text = lit;
		parts.push_back (e);
		lit.clear ();
	      }
	    hs_expr *v = mk (E_VAR, tok.loc);
	    v->text = name;
	    hs_expr *call = mk (E_CALL, tok.loc);
	    call->text = "to_string";
	    call->args.push_back (v);
	    parts.push_back (call);
	    k = close;
	  }
	else
	  lit += c;
      }
    if (!lit.empty () || parts.empty ())
      {
	hs_expr *e = mk (E_STR, tok.loc);
	e->text = lit;
	parts.push_back (e);
      }
    hs_expr *res = parts[0];
    for (size_t k = 1; k < parts.size (); k++)
      res = binary (P_PLUS, res, parts[k], tok.loc);
    return res;
  }

  hs_expr *primary ()
  {
    const hs_token &tok = cur ();
    location_t loc = tok.loc;
    switch (tok.kind)
      {
      case T_INT:
	{
	  hs_expr *e = mk (E_INT, loc);
	  e->ival = tok.ival;
	  next ();
	  return e;
	}
      case T_FLOAT:
	{
	  hs_expr *e = mk (E_FLOAT, loc);
	  e->fval = tok.fval;
	  next ();
	  return e;
	}
      case K_TRUE:
      case K_FALSE:
	{
	  hs_expr *e = mk (E_BOOL, loc);
	  e->bval = tok.kind == K_TRUE;
	  next ();
	  return e;
	}
      case T_STRING:
	{
	  hs_expr *e = string_literal (tok);
	  next ();
	  return e;
	}
      case P_LPAREN:
	{
	  next ();
	  hs_expr *e = expr ();
	  expect (P_RPAREN, "')'");
	  return e;
	}
      case T_IDENT:
	{
	  std::string name = tok.text;
	  next ();
	  if (kind () == P_LPAREN)
	    {
	      next ();
	      hs_expr *c = mk (E_CALL, loc);
	      c->text = name;
	      if (kind () != P_RPAREN)
		do
		  c->args.push_back (expr ());
		while (accept (P_COMMA));
	      expect (P_RPAREN, "')' after call arguments");
	      return c;
	    }
	  hs_expr *v = mk (E_VAR, loc);
	  v->text = name;
	  return v;
	}
      default:
	fail (loc, "expected an expression");
	if (tok.kind != T_EOF && tok.kind != K_END)
	  next ();
	return mk (E_INT, loc);
      }
  }

  hs_expr *unary ()
  {
    if (kind () == P_MINUS || kind () == P_BANG)
      {
	hs_expr *e = mk (E_UNARY, cur ().loc);
	e->op = kind ();
	next ();
	e->lhs = unary ();
	return e;
      }
    return primary ();
  }

  hs_expr *mul ()
  {
    hs_expr *l = unary ();
    while (kind () == P_STAR || kind () == P_SLASH || kind () == P_PERCENT)
      {
	hs_tok op = kind ();
	location_t loc = cur ().loc;
	next ();
	l = binary (op, l, unary (), loc);
      }
    return l;
  }

  hs_expr *add ()
  {
    hs_expr *l = mul ();
    while (kind () == P_PLUS || kind () == P_MINUS)
      {
	hs_tok op = kind ();
	location_t loc = cur ().loc;
	next ();
	l = binary (op, l, mul (), loc);
      }
    return l;
  }

  hs_expr *rel ()
  {
    hs_expr *l = add ();
    while (kind () == P_LT || kind () == P_LE || kind () == P_GT
	   || kind () == P_GE)
      {
	hs_tok op = kind ();
	location_t loc = cur ().loc;
	next ();
	l = binary (op, l, add (), loc);
      }
    return l;
  }

  hs_expr *eq ()
  {
    hs_expr *l = rel ();
    while (kind () == P_EQ || kind () == P_NE)
      {
	hs_tok op = kind ();
	location_t loc = cur ().loc;
	next ();
	l = binary (op, l, rel (), loc);
      }
    return l;
  }

  hs_expr *land ()
  {
    hs_expr *l = eq ();
    while (kind () == P_ANDAND)
      {
	location_t loc = cur ().loc;
	next ();
	l = binary (P_ANDAND, l, eq (), loc);
      }
    return l;
  }

  hs_expr *expr ()
  {
    hs_expr *l = land ();
    while (kind () == P_OROR)
      {
	location_t loc = cur ().loc;
	next ();
	l = binary (P_OROR, l, land (), loc);
      }
    return l;
  }

  /* ---------------- statements ---------------- */

  bool starts_expr () const
  {
    switch (kind ())
      {
      case T_INT: case T_FLOAT: case T_STRING: case T_IDENT: case K_TRUE:
      case K_FALSE: case P_LPAREN: case P_MINUS: case P_BANG:
	return true;
      default:
	return false;
      }
  }

  bool block_end () const
  {
    hs_tok k = kind ();
    return k == K_END || k == K_ELSE || k == K_ELSIF || k == T_EOF;
  }

  void block (std::vector<hs_stmt *> &out)
  {
    while (!block_end ())
      {
	size_t before = i;
	if (hs_stmt *s = stmt ())
	  out.push_back (s);
	if (i == before)
	  next ();		/* guarantee progress after an error */
      }
  }

  bool is_or_then ()
  {
    if (accept (K_IS) || accept (K_THEN))
      return true;
    fail (cur ().loc, "expected 'is' (or 'then') after the condition");
    return false;
  }

  hs_stmt *parse_if ()
  {
    /* current token is 'if' or 'elsif' */
    hs_stmt *s = new hs_stmt (S_IF, cur ().loc);
    next ();
    s->e1 = expr ();
    is_or_then ();
    block (s->body);
    if (kind () == K_ELSIF)
      {
	s->orelse.push_back (parse_if ());	/* consumes the shared 'end' */
	return s;
      }
    if (accept (K_ELSE))
      {
	accept (K_IS);
	block (s->orelse);
      }
    expect (K_END, "'end' to close 'if'");
    return s;
  }

  hs_stmt *stmt ()
  {
    location_t loc = cur ().loc;
    switch (kind ())
      {
      case P_SEMI:
	next ();
	return NULL;

      case K_LET:
      case K_CONST:
	{
	  hs_stmt *s = new hs_stmt (S_LET, loc);
	  next ();
	  accept (K_MUT);
	  if (kind () != T_IDENT)
	    {
	      fail (cur ().loc, "expected a variable name");
	      return NULL;
	    }
	  s->name = cur ().text;
	  next ();
	  if (accept (P_COLON))
	    {
	      s->has_type = true;
	      parse_type (s->declared);
	    }
	  expect (P_ASSIGN, "'=' in variable declaration");
	  s->e1 = expr ();
	  return s;
	}

      case K_RETURN:
	{
	  hs_stmt *s = new hs_stmt (S_RETURN, loc);
	  next ();
	  if (starts_expr ())
	    s->e1 = expr ();
	  return s;
	}

      case K_IF:
	return parse_if ();

      case K_WHILE:
	{
	  hs_stmt *s = new hs_stmt (S_WHILE, loc);
	  next ();
	  s->e1 = expr ();
	  accept (K_IS);
	  block (s->body);
	  expect (K_END, "'end' to close 'while'");
	  return s;
	}

      case K_LOOP:
	{
	  hs_stmt *s = new hs_stmt (S_LOOP, loc);
	  next ();
	  accept (K_IS);
	  block (s->body);
	  expect (K_END, "'end' to close 'loop'");
	  return s;
	}

      case K_FOR:
	{
	  hs_stmt *s = new hs_stmt (S_FOR, loc);
	  next ();
	  if (kind () != T_IDENT)
	    {
	      fail (cur ().loc, "expected a loop variable name");
	      return NULL;
	    }
	  s->name = cur ().text;
	  next ();
	  expect (K_IN, "'in'");
	  s->e1 = expr ();
	  if (kind () == P_DOTDOT || kind () == P_DOTDOTEQ)
	    {
	      s->inclusive = kind () == P_DOTDOTEQ;
	      next ();
	      s->e2 = expr ();
	    }
	  else
	    fail (cur ().loc, "only range loops (a..b, a..=b) are supported");
	  accept (K_IS);
	  block (s->body);
	  expect (K_END, "'end' to close 'for'");
	  return s;
	}

      case K_BREAK:
	next ();
	return new hs_stmt (S_BREAK, loc);
      case K_CONTINUE:
	next ();
	return new hs_stmt (S_CONTINUE, loc);

      case K_WRITE:
	{
	  hs_stmt *s = new hs_stmt (S_WRITE, loc);
	  next ();
	  expect (P_LPAREN, "'(' after write");
	  s->e1 = expr ();
	  expect (P_RPAREN, "')' after write argument");
	  return s;
	}

      case T_IDENT:
	{
	  hs_tok k1 = kind_at (1);
	  if (k1 == P_ASSIGN || k1 == P_PLUS_EQ || k1 == P_MINUS_EQ
	      || k1 == P_STAR_EQ || k1 == P_SLASH_EQ || k1 == P_PERCENT_EQ)
	    {
	      hs_stmt *s = new hs_stmt (S_ASSIGN, loc);
	      s->name = cur ().text;
	      next ();
	      s->op = kind ();
	      next ();
	      s->e1 = expr ();
	      return s;
	    }
	  hs_stmt *s = new hs_stmt (S_EXPR, loc);
	  s->e1 = expr ();
	  if (s->e1->kind != E_CALL)
	    fail (loc, "expression statement has no effect (only calls allowed)");
	  return s;
	}

      default:
	fail (loc, "unexpected token at start of statement");
	return NULL;
      }
  }

  /* ---------------- items ---------------- */

  void skip_to_item ()
  {
    while (kind () != T_EOF && kind () != K_FN && kind () != K_PUB)
      next ();
  }

  void item (hs_program &prog)
  {
    if (kind () == K_USING)
      {			/* using "2026"  -- edition marker, accepted */
	next ();
	if (kind () == T_STRING)
	  next ();
	return;
      }
    if (kind () == K_USE || kind () == K_MOD)
      {
	fail (cur ().loc, "modules and imports are not supported by hhc yet");
	skip_to_item ();
	return;
      }
    accept (K_PUB);
    if (kind () != K_FN)
      {
	fail (cur ().loc, "expected a function definition ('fn')");
	skip_to_item ();
	if (kind () != T_EOF && kind () != K_FN)
	  next ();
	return;
      }
    hs_func *f = new hs_func ();
    f->loc = cur ().loc;
    next ();
    if (kind () != T_IDENT)
      {
	fail (cur ().loc, "expected a function name");
	skip_to_item ();
	return;
      }
    f->name = cur ().text;
    f->loc = cur ().loc;
    next ();
    expect (P_LPAREN, "'(' after function name");
    if (kind () != P_RPAREN)
      do
	{
	  hs_param p;
	  p.loc = cur ().loc;
	  if (kind () != T_IDENT)
	    {
	      fail (cur ().loc, "expected a parameter name");
	      break;
	    }
	  p.name = cur ().text;
	  next ();
	  expect (P_COLON, "':' after parameter name");
	  p.type = HT_ERR;
	  parse_type (p.type);
	  f->params.push_back (p);
	}
      while (accept (P_COMMA));
    expect (P_RPAREN, "')' after parameters");
    if (accept (P_ARROW))
      parse_type (f->ret);

    if (accept (P_ASSIGN))
      {				/* fn f(x: int) -> int = x * 2 */
	hs_stmt *r = new hs_stmt (S_RETURN, cur ().loc);
	r->e1 = expr ();
	f->body.push_back (r);
      }
    else
      {
	expect (K_IS, "'is' before the function body");
	block (f->body);
	expect (K_END, "'end' to close the function");
      }
    prog.funcs.push_back (f);
  }
};

} /* anonymous namespace */

int
hs_parse (const std::vector<hs_token> &toks, hs_program &prog)
{
  parser p (toks);
  while (p.kind () != T_EOF)
    {
      size_t before = p.i;
      p.item (prog);
      if (p.i == before)
	p.next ();
    }
  return p.errors;
}
