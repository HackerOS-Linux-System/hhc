#define INCLUDE_MEMORY
#define INCLUDE_VECTOR
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "toplev.h"
#include "langhooks.h"
#include "langhooks-def.h"
#include "target.h"
#include "stringpool.h"
#include "debug.h"
#include "diagnostic.h"
#include "opts.h"
#include "machmode.h"
#include "stor-layout.h"
#include "fold-const.h"
#include "vec.h"
#include "gimplify.h"
#include "options.h"

#include "hsharp.h"

bool hs_dump_ast = false;

/* ------------------------------------------------------------------ */
/* Types expected by GCC's garbage collector.  H# does not attach any  */
/* frontend specific data to trees (yet), but the structures must      */
/* exist so gengtype can build the tree node union.                    */
/* ------------------------------------------------------------------ */

struct GTY(()) lang_type
{
  int dummy;
};

struct GTY(()) lang_decl
{
  int dummy;
};

/* Language-specific identifier information.  */
struct GTY(()) lang_identifier
{
  struct tree_identifier common;
};

struct GTY(()) language_function
{
  int dummy;
};

union GTY((desc ("TREE_CODE (&%h.generic) == IDENTIFIER_NODE"),
	   chain_next ("CODE_CONTAINS_STRUCT (TREE_CODE (&%h.generic), "
		       "TS_COMMON) ? ((union lang_tree_node *) TREE_CHAIN "
		       "(&%h.generic)) : NULL"))) lang_tree_node
{
  union tree_node GTY((tag ("0"),
		       desc ("tree_node_structure (&%h)"))) generic;
  struct lang_identifier GTY((tag ("1"))) identifier;
};

/* ------------------------------------------------------------------ */
/* Hooks                                                               */
/* ------------------------------------------------------------------ */

#undef LANG_HOOKS_NAME
#define LANG_HOOKS_NAME "GNU H#"

/* Called once after option processing.  */
static bool
hs_langhook_init (void)
{
  build_common_tree_nodes (false);

  if (TYPE_MODE (long_unsigned_type_node) == ptr_mode)
    size_type_node = long_unsigned_type_node;
  else if (TYPE_MODE (long_long_unsigned_type_node) == ptr_mode)
    size_type_node = long_long_unsigned_type_node;
  else
    size_type_node = long_unsigned_type_node;

  targetm.init_builtins ();
  build_common_builtin_nodes ();
  hs_gen_init_types ();
  return true;
}
#undef LANG_HOOKS_INIT
#define LANG_HOOKS_INIT hs_langhook_init

static unsigned int
hs_option_lang_mask (void)
{
  return CL_HSharp;
}
#undef LANG_HOOKS_OPTION_LANG_MASK
#define LANG_HOOKS_OPTION_LANG_MASK hs_option_lang_mask

static bool
hs_handle_option (size_t scode, const char *arg ATTRIBUTE_UNUSED,
		  HOST_WIDE_INT value, int kind ATTRIBUTE_UNUSED,
		  location_t loc ATTRIBUTE_UNUSED,
		  const struct cl_option_handlers *handlers ATTRIBUTE_UNUSED)
{
  opt_code code = (opt_code) scode;
  switch (code)
    {
    case OPT_fhsharp_dump_ast:
      hs_dump_ast = value != 0;
      break;
    default:
      break;
    }
  return true;
}
#undef LANG_HOOKS_HANDLE_OPTION
#define LANG_HOOKS_HANDLE_OPTION hs_handle_option

static bool
hs_post_options (const char **filename ATTRIBUTE_UNUSED)
{
  /* No psABI change warnings for H#.  */
  warn_psabi = 0;
  return false;
}
#undef LANG_HOOKS_POST_OPTIONS
#define LANG_HOOKS_POST_OPTIONS hs_post_options

/* Print a short outline of the parsed program (-fhsharp-dump-ast).  */
void
hs_dump_program (const hs_program &prog)
{
  for (const hs_func *f : prog.funcs)
    fprintf (stderr, "fn %s(%u params) -> %d statements\n", f->name.c_str (),
	     (unsigned) f->params.size (), (int) f->body.size ());
}

/* Parse the single input file and hand the program to the middle end.  */
static void
hs_parse_file (void)
{
  if (num_in_fnames != 1)
    fatal_error (UNKNOWN_LOCATION,
		 "exactly one H# source file must be given");

  std::vector<hs_token> toks;
  if (!hs_lex_file (in_fnames[0], toks))
    return;

  hs_program prog;
  int errs = hs_parse (toks, prog);
  if (errs > 0 || seen_error ())
    return;

  if (hs_dump_ast)
    hs_dump_program (prog);

  hs_generate (prog);
}
#undef LANG_HOOKS_PARSE_FILE
#define LANG_HOOKS_PARSE_FILE hs_parse_file

/* Return a type with machine mode MODE.  */
static tree
hs_type_for_mode (machine_mode mode, int unsignedp)
{
  if (mode == QImode)
    return unsignedp ? unsigned_intQI_type_node : intQI_type_node;
  if (mode == HImode)
    return unsignedp ? unsigned_intHI_type_node : intHI_type_node;
  if (mode == SImode)
    return unsignedp ? unsigned_intSI_type_node : intSI_type_node;
  if (mode == DImode)
    return unsignedp ? unsigned_intDI_type_node : intDI_type_node;
  if (mode == TImode)
    return unsignedp ? unsigned_intTI_type_node : intTI_type_node;
  if (mode == TYPE_MODE (float_type_node))
    return float_type_node;
  if (mode == TYPE_MODE (double_type_node))
    return double_type_node;
  if (mode == TYPE_MODE (long_double_type_node))
    return long_double_type_node;
  if (mode == TYPE_MODE (build_pointer_type (char_type_node)))
    return build_pointer_type (char_type_node);
  if (mode == TYPE_MODE (build_pointer_type (integer_type_node)))
    return build_pointer_type (integer_type_node);
  for (int i = 0; i < NUM_INT_N_ENTS; i++)
    if (int_n_enabled_p[i] && mode == int_n_data[i].m)
      return unsignedp ? int_n_trees[i].unsigned_type
		       : int_n_trees[i].signed_type;
  return NULL_TREE;
}
#undef LANG_HOOKS_TYPE_FOR_MODE
#define LANG_HOOKS_TYPE_FOR_MODE hs_type_for_mode

/* Return an integer type with BITS bits of precision.  */
static tree
hs_type_for_size (unsigned int bits, int unsignedp)
{
  if (bits == TYPE_PRECISION (intTI_type_node))
    return unsignedp ? unsigned_intTI_type_node : intTI_type_node;
  if (bits <= TYPE_PRECISION (intQI_type_node))
    return unsignedp ? unsigned_intQI_type_node : intQI_type_node;
  if (bits <= TYPE_PRECISION (intHI_type_node))
    return unsignedp ? unsigned_intHI_type_node : intHI_type_node;
  if (bits <= TYPE_PRECISION (intSI_type_node))
    return unsignedp ? unsigned_intSI_type_node : intSI_type_node;
  if (bits <= TYPE_PRECISION (intDI_type_node))
    return unsignedp ? unsigned_intDI_type_node : intDI_type_node;
  return NULL_TREE;
}
#undef LANG_HOOKS_TYPE_FOR_SIZE
#define LANG_HOOKS_TYPE_FOR_SIZE hs_type_for_size

static bool
hs_global_bindings_p (void)
{
  return current_function_decl == NULL_TREE;
}
#undef LANG_HOOKS_GLOBAL_BINDINGS_P
#define LANG_HOOKS_GLOBAL_BINDINGS_P hs_global_bindings_p

static tree
hs_getdecls (void)
{
  return NULL_TREE;
}
#undef LANG_HOOKS_GETDECLS
#define LANG_HOOKS_GETDECLS hs_getdecls

static tree
hs_pushdecl (tree decl)
{
  /* The frontend keeps its own symbol tables.  */
  return decl;
}
#undef LANG_HOOKS_PUSHDECL
#define LANG_HOOKS_PUSHDECL hs_pushdecl

/* No H# specific tree codes need gimplifying.  */
static int
hs_gimplify_expr (tree *expr_p ATTRIBUTE_UNUSED,
		  gimple_seq *pre_p ATTRIBUTE_UNUSED,
		  gimple_seq *post_p ATTRIBUTE_UNUSED)
{
  return GS_UNHANDLED;
}
#undef LANG_HOOKS_GIMPLIFY_EXPR
#define LANG_HOOKS_GIMPLIFY_EXPR hs_gimplify_expr

static const char *
hs_printable_name (tree decl, int kind ATTRIBUTE_UNUSED)
{
  tree name = DECL_NAME (decl);
  return name ? IDENTIFIER_POINTER (name) : "<unnamed>";
}
#undef LANG_HOOKS_DECL_PRINTABLE_NAME
#define LANG_HOOKS_DECL_PRINTABLE_NAME hs_printable_name

static bool
hs_complain_wrong_lang_p (const struct cl_option *option ATTRIBUTE_UNUSED)
{
  return false;
}
#undef LANG_HOOKS_COMPLAIN_WRONG_LANG_P
#define LANG_HOOKS_COMPLAIN_WRONG_LANG_P hs_complain_wrong_lang_p

static alias_set_type
hs_get_alias_set (tree)
{
  return 0;
}
#undef LANG_HOOKS_GET_ALIAS_SET
#define LANG_HOOKS_GET_ALIAS_SET hs_get_alias_set

static const char *
hs_get_sarif_source_language (const char *)
{
  return "hsharp";
}
#undef LANG_HOOKS_GET_SARIF_SOURCE_LANGUAGE
#define LANG_HOOKS_GET_SARIF_SOURCE_LANGUAGE hs_get_sarif_source_language

/* Required by the middle end: convert EXPR to TYPE.  */
tree
convert (tree type, tree expr)
{
  return fold_convert (type, expr);
}

struct lang_hooks lang_hooks = LANG_HOOKS_INITIALIZER;

#include "gt-hsharp-hsharp-lang.h"
#include "gtype-hsharp.h"
