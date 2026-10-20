#ifndef HSHARP_H
#define HSHARP_H

/* The including file must define INCLUDE_VECTOR and INCLUDE_STRING before
   config.h (GCC poisons the C++ standard headers otherwise), and include
   tree.h before this header.  */

/* ------------------------------------------------------------------ */
/* Lexer                                                               */
/* ------------------------------------------------------------------ */

enum hs_tok
{
  T_EOF, T_IDENT, T_INT, T_FLOAT, T_STRING,
  /* keywords */
  K_FN, K_IS, K_END, K_LET, K_CONST, K_MUT, K_RETURN, K_IF, K_ELSIF, K_ELSE,
  K_THEN, K_WHILE, K_LOOP, K_FOR, K_IN, K_BREAK, K_CONTINUE, K_PUB, K_TRUE,
  K_FALSE, K_WRITE, K_USING, K_MOD, K_USE, K_FROM,
  /* punctuation / operators */
  P_LPAREN, P_RPAREN, P_COMMA, P_COLON, P_ARROW, P_ASSIGN, P_EQ, P_NE,
  P_LT, P_LE, P_GT, P_GE, P_PLUS, P_MINUS, P_STAR, P_SLASH, P_PERCENT,
  P_PLUS_EQ, P_MINUS_EQ, P_STAR_EQ, P_SLASH_EQ, P_PERCENT_EQ,
  P_ANDAND, P_OROR, P_BANG, P_DOTDOT, P_DOTDOTEQ, P_SEMI,
  /* anything the lexer did not understand */
  T_ERROR
};

struct hs_token
{
  hs_tok kind;
  location_t loc;
  std::string text;		/* identifier / decoded string literal */
  unsigned long long ival;	/* integer literal value */
  double fval;			/* float literal value */
};

/* Tokenise the whole file.  Returns false (after diagnosing) on a hard
   error such as an unreadable file.  */
bool hs_lex_file (const char *filename, std::vector<hs_token> &out);
const char *hs_tok_name (hs_tok k);

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

enum hs_type
{
  HT_ERR, HT_VOID, HT_BOOL,
  HT_I8, HT_I16, HT_I32, HT_I64,
  HT_U8, HT_U16, HT_U32, HT_U64,
  HT_F32, HT_F64, HT_STRING
};

inline bool hs_is_int (hs_type t) { return t >= HT_I8 && t <= HT_U64; }
inline bool hs_is_signed (hs_type t) { return t >= HT_I8 && t <= HT_I64; }
inline bool hs_is_float (hs_type t) { return t == HT_F32 || t == HT_F64; }
inline bool hs_is_num (hs_type t) { return hs_is_int (t) || hs_is_float (t); }

/* ------------------------------------------------------------------ */
/* AST                                                                 */
/* ------------------------------------------------------------------ */

enum hs_ekind
{
  E_INT, E_FLOAT, E_BOOL, E_STR, E_VAR, E_UNARY, E_BINARY, E_CALL
};

struct hs_decl;			/* variable or function symbol, see gen */
struct hs_func;

struct hs_expr
{
  hs_ekind kind;
  location_t loc;
  hs_tok op;			/* E_UNARY / E_BINARY operator token */
  unsigned long long ival;
  double fval;
  bool bval;
  std::string text;		/* E_STR value, E_VAR / E_CALL name */
  hs_expr *lhs, *rhs;		/* operands; lhs only for unary */
  std::vector<hs_expr *> args;	/* E_CALL */
  /* filled in by the checker */
  hs_type type;
  hs_func *callee;
  hs_decl *var;
  hs_expr (hs_ekind k, location_t l)
    : kind (k), loc (l), op (T_EOF), ival (0), fval (0), bval (false),
      lhs (0), rhs (0), type (HT_ERR), callee (0), var (0) {}
};

enum hs_skind
{
  S_LET, S_ASSIGN, S_EXPR, S_WRITE, S_RETURN, S_IF, S_WHILE, S_LOOP,
  S_FOR, S_BREAK, S_CONTINUE
};

struct hs_stmt
{
  hs_skind kind;
  location_t loc;
  std::string name;		/* S_LET / S_FOR variable */
  bool has_type;		/* S_LET: explicit annotation */
  hs_type declared;		/* S_LET: annotated type */
  hs_tok op;			/* S_ASSIGN: P_ASSIGN or P_PLUS_EQ ... */
  bool inclusive;		/* S_FOR: a..=b */
  hs_expr *e1, *e2;		/* init / cond / value; e2 = range end */
  std::vector<hs_stmt *> body;	/* then-branch / loop body */
  std::vector<hs_stmt *> orelse;/* else-branch (elsif nests an S_IF) */
  hs_decl *var;			/* filled in by the checker */
  hs_stmt (hs_skind k, location_t l)
    : kind (k), loc (l), has_type (false), declared (HT_ERR), op (T_EOF),
      inclusive (false), e1 (0), e2 (0), var (0) {}
};

struct hs_param { std::string name; hs_type type; location_t loc; };

struct hs_decl;

struct hs_func
{
  std::string name;
  location_t loc;
  std::vector<hs_param> params;
  hs_type ret;
  std::vector<hs_stmt *> body;
  std::vector<hs_decl *> param_decls;	/* filled in by the checker */
  tree decl;			/* FUNCTION_DECL, set by gen (GC-rooted there) */
  hs_func () : ret (HT_VOID), decl (NULL_TREE) {}
};

struct hs_program { std::vector<hs_func *> funcs; };

/* Parse the token stream into PROG.  Returns the number of errors.  */
int hs_parse (const std::vector<hs_token> &toks, hs_program &prog);

/* Check types and lower PROG to GENERIC / cgraph.  */
void hs_generate (hs_program &prog);
void hs_gen_init_types (void);

extern bool hs_dump_ast;
void hs_dump_program (const hs_program &prog);

#endif /* HSHARP_H */
