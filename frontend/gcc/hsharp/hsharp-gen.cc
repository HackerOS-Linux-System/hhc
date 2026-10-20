#define INCLUDE_VECTOR
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "diagnostic-core.h"
#include "stringpool.h"
#include "stor-layout.h"
#include "fold-const.h"
#include "tree-iterator.h"
#include "gimplify.h"
#include "cgraph.h"
#include "function.h"
#include "langhooks.h"
#include "toplev.h"
#include "attribs.h"

#include <map>

#include "hsharp.h"

/* GC roots: every tree that must survive a ggc_collect.  */
static GTY(()) tree hs_string_type;
static GTY(()) tree hs_printf_decl;
static GTY(()) tree hs_putchar_decl;
static GTY(()) vec<tree, va_gc> *hs_keep;	/* all FUNCTION_DECLs */

struct hs_decl
{
  std::string name;
  hs_type type;
  tree decl;			/* VAR_DECL or PARM_DECL */
  hs_decl (const std::string &n, hs_type t) : name (n), type (t), decl (NULL_TREE) {}
};

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

static const char *
type_name (hs_type t)
{
  switch (t)
    {
    case HT_VOID: return "void";
    case HT_BOOL: return "bool";
    case HT_I8: return "i8";
    case HT_I16: return "i16";
    case HT_I32: return "i32";
    case HT_I64: return "int";
    case HT_U8: return "u8";
    case HT_U16: return "u16";
    case HT_U32: return "u32";
    case HT_U64: return "uint";
    case HT_F32: return "f32";
    case HT_F64: return "f64";
    case HT_STRING: return "string";
    default: return "<error>";
    }
}

static tree
tree_of (hs_type t)
{
  switch (t)
    {
    case HT_VOID: return void_type_node;
    case HT_BOOL: return boolean_type_node;
    case HT_I8: return intQI_type_node;
    case HT_I16: return intHI_type_node;
    case HT_I32: return intSI_type_node;
    case HT_I64: return intDI_type_node;
    case HT_U8: return unsigned_intQI_type_node;
    case HT_U16: return unsigned_intHI_type_node;
    case HT_U32: return unsigned_intSI_type_node;
    case HT_U64: return unsigned_intDI_type_node;
    case HT_F32: return float_type_node;
    case HT_F64: return double_type_node;
    case HT_STRING: return hs_string_type;
    default: return error_mark_node;
    }
}

void
hs_gen_init_types (void)
{
  hs_string_type
    = build_pointer_type (build_qualified_type (char_type_node,
						TYPE_QUAL_CONST));
}

static tree
declare_extern (const char *name, tree fntype)
{
  tree d = build_decl (BUILTINS_LOCATION, FUNCTION_DECL,
		       get_identifier (name), fntype);
  DECL_EXTERNAL (d) = 1;
  TREE_PUBLIC (d) = 1;
  DECL_ARTIFICIAL (d) = 1;
  return d;
}

static void
init_runtime_decls (void)
{
  tree fmt = hs_string_type;
  hs_printf_decl
    = declare_extern ("printf",
		      build_varargs_function_type_list (integer_type_node,
							fmt, NULL_TREE));
  hs_putchar_decl
    = declare_extern ("putchar",
		      build_function_type_list (integer_type_node,
						integer_type_node, NULL_TREE));
}

/* ------------------------------------------------------------------ */
/* Semantic checking                                                   */
/* ------------------------------------------------------------------ */

namespace {

struct scope
{
  std::map<std::string, hs_decl *> vars;
};

struct checker
{
  hs_program &prog;
  std::map<std::string, hs_func *> funcs;
  std::vector<scope> scopes;
  hs_func *cur;
  int loop_depth;
  int errors;

  checker (hs_program &p) : prog (p), cur (0), loop_depth (0), errors (0) {}

  void err (location_t loc, const char *fmt, const char *a = NULL)
  {
    errors++;
    if (a)
      error_at (loc, fmt, a);
    else
      error_at (loc, "%s", fmt);
  }

  hs_decl *lookup (const std::string &n)
  {
    for (size_t k = scopes.size (); k-- > 0;)
      {
	auto it = scopes[k].vars.find (n);
	if (it != scopes[k].vars.end ())
	  return it->second;
      }
    return NULL;
  }

  hs_decl *declare (const std::string &n, hs_type t)
  {
    hs_decl *d = new hs_decl (n, t);
    scopes.back ().vars[n] = d;
    return d;
  }

  /* Give E a type.  HINT (may be HT_ERR) is the type the context wants:
     it lets untyped integer literals become i8/u32/f64/... as needed.  */
  hs_type expr (hs_expr *e, hs_type hint)
  {
    switch (e->kind)
      {
      case E_INT:
	if (hs_is_num (hint))
	  e->type = hint;
	else
	  e->type = HT_I64;
	return e->type;
      case E_FLOAT:
	e->type = hs_is_float (hint) ? hint : HT_F64;
	return e->type;
      case E_BOOL:
	return e->type = HT_BOOL;
      case E_STR:
	return e->type = HT_STRING;
      case E_VAR:
	{
	  hs_decl *d = lookup (e->text);
	  if (!d)
	    {
	      err (e->loc, "undefined variable %qs", e->text.c_str ());
	      return e->type = HT_ERR;
	    }
	  e->var = d;
	  return e->type = d->type;
	}
      case E_UNARY:
	{
	  hs_type t = expr (e->lhs, e->op == P_BANG ? HT_BOOL : hint);
	  if (t == HT_ERR)
	    return e->type = HT_ERR;
	  if (e->op == P_BANG)
	    {
	      if (t != HT_BOOL)
		err (e->loc, "operand of '!' must be bool");
	      return e->type = HT_BOOL;
	    }
	  if (!hs_is_num (t) || (hs_is_int (t) && !hs_is_signed (t)))
	    err (e->loc, "operand of unary '-' must be a signed number");
	  return e->type = t;
	}
      case E_BINARY:
	return binary (e, hint);
      case E_CALL:
	return call (e, hint);
      }
    return HT_ERR;
  }

  hs_type binary (hs_expr *e, hs_type hint)
  {
    hs_tok op = e->op;
    bool logical = op == P_ANDAND || op == P_OROR;
    bool cmp = op == P_EQ || op == P_NE || op == P_LT || op == P_LE
	       || op == P_GT || op == P_GE;
    hs_type sub_hint = (logical || cmp) ? HT_ERR : hint;

    /* Let a literal on the left adopt the type of the right operand.  */
    hs_type lt, rt;
    if (e->lhs->kind == E_INT || e->lhs->kind == E_FLOAT)
      {
	rt = expr (e->rhs, sub_hint);
	lt = expr (e->lhs, hs_is_num (rt) ? rt : sub_hint);
      }
    else
      {
	lt = expr (e->lhs, sub_hint);
	rt = expr (e->rhs, hs_is_num (lt) ? lt : sub_hint);
      }
    if (lt == HT_ERR || rt == HT_ERR)
      return e->type = HT_ERR;

    if (logical)
      {
	if (lt != HT_BOOL || rt != HT_BOOL)
	  err (e->loc, "operands of '&&' / '||' must be bool");
	return e->type = HT_BOOL;
      }
    if (lt != rt)
      {
	error_at (e->loc, "type mismatch: %qs and %qs",
		  type_name (lt), type_name (rt));
	errors++;
	return e->type = HT_ERR;
      }
    if (cmp)
      {
	if (lt == HT_STRING || lt == HT_VOID)
	  err (e->loc, "cannot compare values of this type");
	return e->type = HT_BOOL;
      }
    if (op == P_PLUS && lt == HT_STRING)
      return e->type = HT_STRING;	/* concatenation: only legal in write() */
    if (!hs_is_num (lt))
      {
	err (e->loc, "arithmetic needs numeric operands");
	return e->type = HT_ERR;
      }
    if (op == P_PERCENT && hs_is_float (lt))
      err (e->loc, "'%' is not defined for floating point values");
    return e->type = lt;
  }

  hs_type call (hs_expr *e, hs_type hint)
  {
    (void) hint;
    if (e->text == "to_string")
      {
	if (e->args.size () != 1)
	  {
	    err (e->loc, "to_string takes exactly one argument");
	    return e->type = HT_ERR;
	  }
	hs_type t = expr (e->args[0], HT_ERR);
	if (t == HT_VOID)
	  err (e->loc, "cannot convert this value to string");
	return e->type = HT_STRING;
      }
    auto it = funcs.find (e->text);
    if (it == funcs.end ())
      {
	err (e->loc, "call to undefined function %qs", e->text.c_str ());
	for (hs_expr *a : e->args)
	  expr (a, HT_ERR);
	return e->type = HT_ERR;
      }
    hs_func *f = it->second;
    e->callee = f;
    if (f->params.size () != e->args.size ())
      {
	error_at (e->loc, "%qs expects %u argument(s), got %u",
		  f->name.c_str (), (unsigned) f->params.size (),
		  (unsigned) e->args.size ());
	errors++;
      }
    for (size_t k = 0; k < e->args.size (); k++)
      {
	hs_type want = k < f->params.size () ? f->params[k].type : HT_ERR;
	hs_type got = expr (e->args[k], want);
	if (k < f->params.size () && got != HT_ERR && want != HT_ERR
	    && got != want)
	  {
	    error_at (e->args[k]->loc,
		      "argument %u of %qs: expected %qs, got %qs",
		      (unsigned) k + 1, f->name.c_str (),
		      type_name (want), type_name (got));
	    errors++;
	  }
      }
    return e->type = f->ret;
  }

  void stmts (std::vector<hs_stmt *> &v)
  {
    scopes.push_back (scope ());
    for (hs_stmt *s : v)
      stmt (s);
    scopes.pop_back ();
  }

  void stmt (hs_stmt *s)
  {
    switch (s->kind)
      {
      case S_LET:
	{
	  hs_type t = expr (s->e1, s->has_type ? s->declared : HT_ERR);
	  if (s->has_type && t != HT_ERR && s->declared != HT_ERR
	      && t != s->declared)
	    {
	      error_at (s->loc, "cannot initialise %qs with a value of type %qs",
			type_name (s->declared), type_name (t));
	      errors++;
	    }
	  hs_type vt = s->has_type ? s->declared : t;
	  if (vt == HT_VOID)
	    err (s->loc, "variable cannot have type void");
	  if (s->e1->kind == E_BINARY && t == HT_STRING)
	    err (s->loc, "string concatenation is only supported inside write()");
	  s->var = declare (s->name, vt);
	  break;
	}
      case S_ASSIGN:
	{
	  hs_decl *d = lookup (s->name);
	  if (!d)
	    {
	      err (s->loc, "assignment to undefined variable %qs",
		   s->name.c_str ());
	      expr (s->e1, HT_ERR);
	      break;
	    }
	  s->var = d;
	  hs_type t = expr (s->e1, d->type);
	  if (t != HT_ERR && d->type != HT_ERR && t != d->type)
	    {
	      error_at (s->loc, "cannot assign %qs to variable of type %qs",
			type_name (t), type_name (d->type));
	      errors++;
	    }
	  if (s->op != P_ASSIGN && !hs_is_num (d->type))
	    err (s->loc, "compound assignment needs a numeric variable");
	  if (d->type == HT_STRING && s->e1->kind == E_BINARY)
	    err (s->loc, "string concatenation is only supported inside write()");
	  break;
	}
      case S_EXPR:
	expr (s->e1, HT_ERR);
	break;
      case S_WRITE:
	{
	  hs_type t = expr (s->e1, HT_ERR);
	  if (t == HT_VOID)
	    err (s->loc, "write() needs a value");
	  break;
	}
      case S_RETURN:
	{
	  if (s->e1)
	    {
	      hs_type t = expr (s->e1, cur->ret);
	      if (cur->ret == HT_VOID)
		err (s->loc, "returning a value from a void function");
	      else if (t != HT_ERR && t != cur->ret)
		{
		  error_at (s->loc, "return type mismatch: expected %qs, got %qs",
			    type_name (cur->ret), type_name (t));
		  errors++;
		}
	      if (t == HT_STRING && s->e1->kind == E_BINARY)
		err (s->loc, "string concatenation is only supported inside write()");
	    }
	  else if (cur->ret != HT_VOID && cur->name != "main")
	    err (s->loc, "missing return value");
	  break;
	}
      case S_IF:
	{
	  hs_type t = expr (s->e1, HT_BOOL);
	  if (t != HT_BOOL && t != HT_ERR)
	    err (s->e1->loc, "condition must be bool");
	  stmts (s->body);
	  stmts (s->orelse);
	  break;
	}
      case S_WHILE:
	{
	  hs_type t = expr (s->e1, HT_BOOL);
	  if (t != HT_BOOL && t != HT_ERR)
	    err (s->e1->loc, "condition must be bool");
	  loop_depth++;
	  stmts (s->body);
	  loop_depth--;
	  break;
	}
      case S_LOOP:
	loop_depth++;
	stmts (s->body);
	loop_depth--;
	break;
      case S_FOR:
	{
	  hs_type a = expr (s->e1, HT_ERR);
	  hs_type b = expr (s->e2, hs_is_int (a) ? a : HT_ERR);
	  if (s->e1->kind == E_INT && hs_is_int (b))
	    a = expr (s->e1, b);
	  if (a != HT_ERR && b != HT_ERR && (a != b || !hs_is_int (a)))
	    err (s->loc, "range bounds must be integers of the same type");
	  scopes.push_back (scope ());
	  s->var = declare (s->name, hs_is_int (a) ? a : HT_I64);
	  loop_depth++;
	  stmts (s->body);
	  loop_depth--;
	  scopes.pop_back ();
	  break;
	}
      case S_BREAK:
      case S_CONTINUE:
	if (loop_depth == 0)
	  err (s->loc, "break/continue outside of a loop");
	break;
      }
  }

  int run ()
  {
    for (hs_func *f : prog.funcs)
      {
	if (funcs.count (f->name))
	  err (f->loc, "function %qs is defined twice", f->name.c_str ());
	funcs[f->name] = f;
      }
    auto m = funcs.find ("main");
    if (m == funcs.end ())
      {
	error ("no 'main' function defined");
	errors++;
      }
    else if (!m->second->params.empty ())
      err (m->second->loc, "'main' must not take parameters");

    for (hs_func *f : prog.funcs)
      {
	cur = f;
	scopes.clear ();
	scopes.push_back (scope ());
	f->param_decls.clear ();
	for (hs_param &p : f->params)
	  f->param_decls.push_back (declare (p.name, p.type));
	/* Parameters live in the outermost scope; the body gets its own.  */
	for (hs_stmt *s : f->body)
	  stmt (s);
	scopes.pop_back ();
      }
    return errors;
  }
};

/* ------------------------------------------------------------------ */
/* Lowering                                                            */
/* ------------------------------------------------------------------ */

struct loop_labels { tree brk, cont; };

struct lowering
{
  hs_func *fn;
  tree fndecl;
  tree stmts_list;
  tree vars;			/* chain of local VAR_DECLs */
  std::vector<loop_labels> loops;
  unsigned tmp_counter;

  lowering () : fn (0), fndecl (NULL_TREE), stmts_list (NULL_TREE),
		vars (NULL_TREE), tmp_counter (0) {}

  void emit (tree t)
  {
    if (t != NULL_TREE && t != error_mark_node)
      append_to_statement_list (t, &stmts_list);
  }

  tree make_var (const char *name, hs_type t, location_t loc)
  {
    tree v = build_decl (loc, VAR_DECL, get_identifier (name), tree_of (t));
    DECL_CONTEXT (v) = fndecl;
    DECL_ARTIFICIAL (v) = 0;
    TREE_USED (v) = 1;
    DECL_CHAIN (v) = vars;
    vars = v;
    return v;
  }

  tree temp (hs_type t, location_t loc)
  {
    char buf[32];
    snprintf (buf, sizeof buf, "__hs_tmp%u", tmp_counter++);
    tree v = make_var (buf, t, loc);
    DECL_ARTIFICIAL (v) = 1;
    return v;
  }

  tree new_label (location_t loc) { return create_artificial_label (loc); }

  tree label_expr (tree l) { return build1 (LABEL_EXPR, void_type_node, l); }
  tree goto_expr (tree l) { return build1 (GOTO_EXPR, void_type_node, l); }

  tree string_cst (const std::string &s)
  {
    tree str = build_string (s.size () + 1, s.c_str ());
    TREE_TYPE (str) = build_array_type (char_type_node,
					build_index_type (size_int (s.size ())));
    TREE_CONSTANT (str) = 1;
    TREE_READONLY (str) = 1;
    TREE_STATIC (str) = 1;
    return fold_convert (hs_string_type, build_fold_addr_expr (str));
  }

  /* -------- expressions -------- */

  tree rvalue (hs_expr *e)
  {
    tree ty = tree_of (e->type);
    switch (e->kind)
      {
      case E_INT:
	if (hs_is_float (e->type))
	  return build_real_from_int_cst (ty, build_int_cst (long_long_integer_type_node,
							      (HOST_WIDE_INT) e->ival));
	return build_int_cst_type (ty, (HOST_WIDE_INT) e->ival);
      case E_FLOAT:
	{
	  REAL_VALUE_TYPE r;
	  char buf[64];
	  snprintf (buf, sizeof buf, "%.17g", e->fval);
	  real_from_string (&r, buf);
	  return build_real (ty, r);
	}
      case E_BOOL:
	return e->bval ? boolean_true_node : boolean_false_node;
      case E_STR:
	return string_cst (e->text);
      case E_VAR:
	return e->var->decl;
      case E_UNARY:
	{
	  tree o = rvalue (e->lhs);
	  if (e->op == P_BANG)
	    return build1 (TRUTH_NOT_EXPR, boolean_type_node, o);
	  return fold_build1 (NEGATE_EXPR, ty, o);
	}
      case E_BINARY:
	return binary (e);
      case E_CALL:
	return call (e);
      }
    return error_mark_node;
  }

  tree binary (hs_expr *e)
  {
    tree a = rvalue (e->lhs);
    tree b = rvalue (e->rhs);
    tree ty = tree_of (e->type);
    bool flt = hs_is_float (e->lhs->type);
    switch (e->op)
      {
      case P_PLUS: return fold_build2 (PLUS_EXPR, ty, a, b);
      case P_MINUS: return fold_build2 (MINUS_EXPR, ty, a, b);
      case P_STAR: return fold_build2 (MULT_EXPR, ty, a, b);
      case P_SLASH:
	return fold_build2 (flt ? RDIV_EXPR : TRUNC_DIV_EXPR, ty, a, b);
      case P_PERCENT: return fold_build2 (TRUNC_MOD_EXPR, ty, a, b);
      case P_EQ: return fold_build2 (EQ_EXPR, boolean_type_node, a, b);
      case P_NE: return fold_build2 (NE_EXPR, boolean_type_node, a, b);
      case P_LT: return fold_build2 (LT_EXPR, boolean_type_node, a, b);
      case P_LE: return fold_build2 (LE_EXPR, boolean_type_node, a, b);
      case P_GT: return fold_build2 (GT_EXPR, boolean_type_node, a, b);
      case P_GE: return fold_build2 (GE_EXPR, boolean_type_node, a, b);
      case P_ANDAND:
	return build2 (TRUTH_ANDIF_EXPR, boolean_type_node, a, b);
      case P_OROR:
	return build2 (TRUTH_ORIF_EXPR, boolean_type_node, a, b);
      default:
	return error_mark_node;
      }
  }

  tree call (hs_expr *e)
  {
    gcc_assert (e->callee);
    unsigned n = e->args.size ();
    tree *argv = XALLOCAVEC (tree, n ? n : 1);
    for (unsigned k = 0; k < n; k++)
      argv[k] = rvalue (e->args[k]);
    tree fd = e->callee->decl;
    tree ce = build_call_array_loc (e->loc, tree_of (e->callee->ret),
				    build1 (ADDR_EXPR,
					    build_pointer_type (TREE_TYPE (fd)), fd),
				    n, argv);
    return ce;
  }

  /* -------- write() -------- */

  void printf_call (location_t loc, const char *fmt, tree arg)
  {
    tree args[2] = { string_cst (fmt), arg };
    tree fd = hs_printf_decl;
    emit (build_call_array_loc (loc, integer_type_node,
				build1 (ADDR_EXPR,
					build_pointer_type (TREE_TYPE (fd)), fd),
				2, args));
  }

  /* Print the value of E without a trailing newline.  String '+' chains and
     to_string() are flattened into a sequence of printf calls.  */
  void write_piece (hs_expr *e)
  {
    if (e->kind == E_BINARY && e->type == HT_STRING && e->op == P_PLUS)
      {
	write_piece (e->lhs);
	write_piece (e->rhs);
	return;
      }
    if (e->kind == E_CALL && e->text == "to_string" && !e->callee)
      {
	write_piece_value (e->args[0]);
	return;
      }
    write_piece_value (e);
  }

  void write_piece_value (hs_expr *e)
  {
    location_t loc = e->loc;
    tree v = rvalue (e);
    switch (e->type)
      {
      case HT_STRING:
	printf_call (loc, "%s", v);
	break;
      case HT_BOOL:
	printf_call (loc, "%s",
		     build3 (COND_EXPR, hs_string_type, v,
			     string_cst ("true"), string_cst ("false")));
	break;
      case HT_F32:
      case HT_F64:
	printf_call (loc, "%g", fold_convert (double_type_node, v));
	break;
      default:
	if (hs_is_signed (e->type))
	  printf_call (loc, "%lld", fold_convert (long_long_integer_type_node, v));
	else
	  printf_call (loc, "%llu",
		       fold_convert (long_long_unsigned_type_node, v));
	break;
      }
  }

  void write_stmt (hs_stmt *s)
  {
    write_piece (s->e1);
    tree args[1] = { build_int_cst (integer_type_node, '\n') };
    tree fd = hs_putchar_decl;
    emit (build_call_array_loc (s->loc, integer_type_node,
				build1 (ADDR_EXPR,
					build_pointer_type (TREE_TYPE (fd)), fd),
				1, args));
  }

  /* -------- statements -------- */

  void block (std::vector<hs_stmt *> &v)
  {
    for (hs_stmt *s : v)
      stmt (s);
  }

  tree compound_rhs (hs_stmt *s, tree cur, tree rhs)
  {
    tree ty = TREE_TYPE (cur);
    bool flt = hs_is_float (s->var->type);
    switch (s->op)
      {
      case P_PLUS_EQ: return fold_build2 (PLUS_EXPR, ty, cur, rhs);
      case P_MINUS_EQ: return fold_build2 (MINUS_EXPR, ty, cur, rhs);
      case P_STAR_EQ: return fold_build2 (MULT_EXPR, ty, cur, rhs);
      case P_SLASH_EQ:
	return fold_build2 (flt ? RDIV_EXPR : TRUNC_DIV_EXPR, ty, cur, rhs);
      case P_PERCENT_EQ: return fold_build2 (TRUNC_MOD_EXPR, ty, cur, rhs);
      default: return rhs;
      }
  }

  void stmt (hs_stmt *s)
  {
    input_location = s->loc;
    switch (s->kind)
      {
      case S_LET:
	{
	  tree v = make_var (s->name.c_str (), s->var->type, s->loc);
	  s->var->decl = v;
	  tree init = rvalue (s->e1);
	  emit (build2 (INIT_EXPR, void_type_node, v, init));
	  break;
	}
      case S_ASSIGN:
	{
	  tree v = s->var->decl;
	  tree rhs = compound_rhs (s, v, rvalue (s->e1));
	  emit (build2 (MODIFY_EXPR, TREE_TYPE (v), v, rhs));
	  break;
	}
      case S_EXPR:
	emit (rvalue (s->e1));
	break;
      case S_WRITE:
	write_stmt (s);
	break;
      case S_RETURN:
	{
	  tree res = DECL_RESULT (fndecl);
	  if (s->e1)
	    {
	      tree set = build2 (MODIFY_EXPR, TREE_TYPE (res), res,
				 rvalue (s->e1));
	      emit (build1 (RETURN_EXPR, void_type_node, set));
	    }
	  else if (!VOID_TYPE_P (TREE_TYPE (res)))
	    {
	      tree set = build2 (MODIFY_EXPR, TREE_TYPE (res), res,
				 build_zero_cst (TREE_TYPE (res)));
	      emit (build1 (RETURN_EXPR, void_type_node, set));
	    }
	  else
	    emit (build1 (RETURN_EXPR, void_type_node, NULL_TREE));
	  break;
	}
      case S_IF:
	{
	  tree cond = rvalue (s->e1);
	  tree l_else = new_label (s->loc), l_end = new_label (s->loc);
	  tree not_cond = build1 (TRUTH_NOT_EXPR, boolean_type_node, cond);
	  emit (build3 (COND_EXPR, void_type_node, not_cond, goto_expr (l_else),
			build_empty_stmt (s->loc)));
	  block (s->body);
	  if (!s->orelse.empty ())
	    emit (goto_expr (l_end));
	  emit (label_expr (l_else));
	  if (!s->orelse.empty ())
	    {
	      block (s->orelse);
	      emit (label_expr (l_end));
	    }
	  break;
	}
      case S_WHILE:
      case S_LOOP:
	{
	  tree l_top = new_label (s->loc), l_exit = new_label (s->loc);
	  loops.push_back ({ l_exit, l_top });
	  emit (label_expr (l_top));
	  if (s->kind == S_WHILE)
	    emit (build3 (COND_EXPR, void_type_node,
			  build1 (TRUTH_NOT_EXPR, boolean_type_node,
				  rvalue (s->e1)),
			  goto_expr (l_exit), build_empty_stmt (s->loc)));
	  block (s->body);
	  emit (goto_expr (l_top));
	  emit (label_expr (l_exit));
	  loops.pop_back ();
	  break;
	}
      case S_FOR:
	{
	  hs_type t = s->var->type;
	  tree i = make_var (s->name.c_str (), t, s->loc);
	  s->var->decl = i;
	  tree end = temp (t, s->loc);
	  emit (build2 (INIT_EXPR, void_type_node, i, rvalue (s->e1)));
	  emit (build2 (INIT_EXPR, void_type_node, end, rvalue (s->e2)));
	  tree l_top = new_label (s->loc), l_cont = new_label (s->loc),
	       l_exit = new_label (s->loc);
	  loops.push_back ({ l_exit, l_cont });
	  emit (label_expr (l_top));
	  tree stay = fold_build2 (s->inclusive ? LE_EXPR : LT_EXPR,
				   boolean_type_node, i, end);
	  emit (build3 (COND_EXPR, void_type_node,
			build1 (TRUTH_NOT_EXPR, boolean_type_node, stay),
			goto_expr (l_exit), build_empty_stmt (s->loc)));
	  block (s->body);
	  emit (label_expr (l_cont));
	  emit (build2 (MODIFY_EXPR, TREE_TYPE (i), i,
			fold_build2 (PLUS_EXPR, TREE_TYPE (i), i,
				     build_int_cst (TREE_TYPE (i), 1))));
	  emit (goto_expr (l_top));
	  emit (label_expr (l_exit));
	  loops.pop_back ();
	  break;
	}
      case S_BREAK:
	emit (goto_expr (loops.back ().brk));
	break;
      case S_CONTINUE:
	emit (goto_expr (loops.back ().cont));
	break;
      }
  }
};

} /* anonymous namespace */

/* Create the FUNCTION_DECL for F (without a body yet).  */
static void
declare_function (hs_func *f, std::vector<tree> &parms_out)
{
  tree ret = f->name == "main" ? integer_type_node : tree_of (f->ret);
  tree args = NULL_TREE;
  for (size_t k = f->params.size (); k-- > 0;)
    args = tree_cons (NULL_TREE, tree_of (f->params[k].type), args);
  args = chainon (args, void_list_node);
  tree fntype = build_function_type (ret, args);

  tree fd = build_decl (f->loc, FUNCTION_DECL,
			get_identifier (f->name.c_str ()), fntype);
  TREE_PUBLIC (fd) = 1;
  TREE_STATIC (fd) = 1;
  DECL_EXTERNAL (fd) = 0;
  DECL_ARTIFICIAL (fd) = 0;
  f->decl = fd;
  vec_safe_push (hs_keep, fd);

  tree res = build_decl (f->loc, RESULT_DECL, NULL_TREE, ret);
  DECL_ARTIFICIAL (res) = 1;
  DECL_IGNORED_P (res) = 1;
  DECL_CONTEXT (res) = fd;
  DECL_RESULT (fd) = res;

  tree chain = NULL_TREE;
  parms_out.clear ();
  for (size_t k = 0; k < f->params.size (); k++)
    {
      tree p = build_decl (f->params[k].loc, PARM_DECL,
			   get_identifier (f->params[k].name.c_str ()),
			   tree_of (f->params[k].type));
      DECL_ARG_TYPE (p) = TREE_TYPE (p);
      DECL_CONTEXT (p) = fd;
      TREE_USED (p) = 1;
      parms_out.push_back (p);
    }
  for (size_t k = parms_out.size (); k-- > 0;)
    {
      DECL_CHAIN (parms_out[k]) = chain;
      chain = parms_out[k];
    }
  DECL_ARGUMENTS (fd) = chain;
}

static void
define_function (hs_func *f, const std::vector<tree> &parms)
{
  lowering lw;
  lw.fn = f;
  lw.fndecl = f->decl;
  lw.stmts_list = alloc_stmt_list ();

  /* Bind each parameter's symbol to its PARM_DECL.  */
  for (size_t k = 0; k < parms.size (); k++)
    f->param_decls[k]->decl = parms[k];

  push_struct_function (f->decl);
  lw.block (f->body);

  /* Fall off the end: "return 0" for main / non-void, plain return else.  */
  tree res = DECL_RESULT (f->decl);
  if (!VOID_TYPE_P (TREE_TYPE (res)))
    lw.emit (build1 (RETURN_EXPR, void_type_node,
		     build2 (MODIFY_EXPR, TREE_TYPE (res), res,
			     build_zero_cst (TREE_TYPE (res)))));

  tree vars = nreverse (lw.vars);
  tree block = make_node (BLOCK);
  BLOCK_VARS (block) = vars;
  BLOCK_SUPERCONTEXT (block) = f->decl;
  TREE_USED (block) = 1;
  DECL_INITIAL (f->decl) = block;
  tree bind = build3 (BIND_EXPR, void_type_node, vars, lw.stmts_list, block);
  TREE_SIDE_EFFECTS (bind) = 1;
  DECL_SAVED_TREE (f->decl) = bind;
  pop_cfun ();

  gimplify_function_tree (f->decl);
  cgraph_node::finalize_function (f->decl, true);
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

void
hs_generate (hs_program &prog)
{
  checker ck (prog);
  if (ck.run () > 0)
    return;

  init_runtime_decls ();

  std::vector<std::vector<tree> > parms (prog.funcs.size ());
  for (size_t k = 0; k < prog.funcs.size (); k++)
    declare_function (prog.funcs[k], parms[k]);
  for (size_t k = 0; k < prog.funcs.size (); k++)
    define_function (prog.funcs[k], parms[k]);
}

#include "gt-hsharp-hsharp-gen.h"
