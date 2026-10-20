#define INCLUDE_VECTOR
#define INCLUDE_STRING
#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "tree.h"
#include "diagnostic-core.h"
#include "input.h"

#include "hsharp.h"

/* Escaped braces inside string literals are stored as these placeholders so
   the parser can tell them apart from `{name}` interpolation markers.  */
#define HS_ESC_LBRACE '\x01'
#define HS_ESC_RBRACE '\x02'

namespace {

struct keyword { const char *text; hs_tok kind; };

const keyword keywords[] = {
  {"fn", K_FN}, {"is", K_IS}, {"end", K_END}, {"let", K_LET},
  {"const", K_CONST}, {"mut", K_MUT}, {"return", K_RETURN}, {"if", K_IF},
  {"elsif", K_ELSIF}, {"else", K_ELSE}, {"then", K_THEN},
  {"while", K_WHILE}, {"loop", K_LOOP}, {"for", K_FOR}, {"in", K_IN},
  {"break", K_BREAK}, {"continue", K_CONTINUE}, {"pub", K_PUB},
  {"true", K_TRUE}, {"false", K_FALSE}, {"write", K_WRITE},
  {"using", K_USING}, {"mod", K_MOD}, {"use", K_USE}, {"from", K_FROM},
};

struct lexer
{
  const char *src;
  size_t len, pos;
  int line, col;		/* 1-based position of src[pos] */
  std::vector<hs_token> &out;

  lexer (const char *s, size_t n, std::vector<hs_token> &o)
    : src (s), len (n), pos (0), line (1), col (1), out (o) {}

  int cur () const { return pos < len ? (unsigned char) src[pos] : -1; }
  int peek (size_t k = 1) const
  { return pos + k < len ? (unsigned char) src[pos + k] : -1; }

  location_t here () const
  { return linemap_position_for_column (line_table, col); }

  void advance ()
  {
    if (pos >= len)
      return;
    if (src[pos] == '\n')
      {
	line++;
	col = 1;
	linemap_line_start (line_table, line, 120);
      }
    else
      col++;
    pos++;
  }

  void push (hs_tok k, location_t loc, const std::string &text = "")
  {
    hs_token t;
    t.kind = k;
    t.loc = loc;
    t.text = text;
    t.ival = 0;
    t.fval = 0;
    out.push_back (t);
  }

  static bool ident_start (int c)
  { return c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
  static bool digit (int c) { return c >= '0' && c <= '9'; }

  void lex_number ()
  {
    location_t loc = here ();
    std::string digits;
    bool is_float = false;
    int base = 10;

    if (cur () == '0' && (peek () == 'x' || peek () == 'X'))
      {
	base = 16;
	advance (); advance ();
      }
    else if (cur () == '0' && (peek () == 'b' || peek () == 'B'))
      {
	base = 2;
	advance (); advance ();
      }

    while (true)
      {
	int c = cur ();
	if (c == '_')
	  { advance (); continue; }
	bool ok = base == 16 ? (digit (c) || (c >= 'a' && c <= 'f')
				|| (c >= 'A' && c <= 'F'))
	  : base == 2 ? (c == '0' || c == '1') : digit (c);
	if (ok)
	  { digits += (char) c; advance (); continue; }
	if (base == 10 && c == '.' && digit (peek ()) && !is_float)
	  {
	    is_float = true;
	    digits += '.';
	    advance ();
	    continue;
	  }
	if (base == 10 && (c == 'e' || c == 'E')
	    && (digit (peek ()) || ((peek () == '-' || peek () == '+')
				    && digit (peek (2)))))
	  {
	    is_float = true;
	    digits += 'e';
	    advance ();
	    if (cur () == '-' || cur () == '+')
	      { digits += (char) cur (); advance (); }
	    continue;
	  }
	break;
      }

    hs_token t;
    t.loc = loc;
    t.ival = 0;
    t.fval = 0;
    if (is_float)
      {
	t.kind = T_FLOAT;
	t.fval = strtod (digits.c_str (), NULL);
      }
    else
      {
	t.kind = T_INT;
	t.ival = strtoull (digits.c_str (), NULL, base);
      }
    out.push_back (t);
  }

  void lex_string ()
  {
    location_t loc = here ();
    std::string s;
    advance ();			/* opening quote */
    while (true)
      {
	int c = cur ();
	if (c < 0 || c == '\n')
	  {
	    error_at (loc, "unterminated string literal");
	    push (T_ERROR, loc);
	    return;
	  }
	if (c == '"')
	  { advance (); break; }
	if (c == '\\')
	  {
	    advance ();
	    int e = cur ();
	    advance ();
	    switch (e)
	      {
	      case 'n': s += '\n'; break;
	      case 't': s += '\t'; break;
	      case 'r': s += '\r'; break;
	      case '0': s += '\0'; break;
	      case '{': s += HS_ESC_LBRACE; break;
	      case '}': s += HS_ESC_RBRACE; break;
	      case 'x':
		{
		  int v = 0;
		  for (int i = 0; i < 2; i++)
		    {
		      int h = cur ();
		      if (digit (h)) v = v * 16 + (h - '0');
		      else if (h >= 'a' && h <= 'f') v = v * 16 + (h - 'a' + 10);
		      else if (h >= 'A' && h <= 'F') v = v * 16 + (h - 'A' + 10);
		      else break;
		      advance ();
		    }
		  s += (char) v;
		  break;
		}
	      default:
		if (e >= 0)
		  s += (char) e;
		break;
	      }
	    continue;
	  }
	s += (char) c;
	advance ();
      }
    push (T_STRING, loc, s);
  }

  void run ()
  {
    linemap_line_start (line_table, 1, 120);
    while (pos < len)
      {
	int c = cur ();

	if (c == ' ' || c == '\t' || c == '\r' || c == '\n')
	  { advance (); continue; }

	/* ";;" line comment.  A lone ';' is a statement separator.  */
	if (c == ';' && peek () == ';')
	  {
	    while (cur () >= 0 && cur () != '\n')
	      advance ();
	    continue;
	  }
	/* "///" doc comment and "////" ordinary comment: both to end of line;
	   "// ... \\" is a block comment (everything up to two backslashes).  */
	if (c == '/' && peek () == '/')
	  {
	    location_t loc = here ();
	    bool doc = peek (2) == '/';
	    advance (); advance ();
	    if (doc)
	      {
		while (cur () >= 0 && cur () != '\n')
		  advance ();
		continue;
	      }
	    bool closed = false;
	    while (cur () >= 0)
	      {
		if (cur () == '\\' && peek () == '\\')
		  { advance (); advance (); closed = true; break; }
		advance ();
	      }
	    if (!closed)
	      error_at (loc, "unterminated block comment "
			"(close it with %<\\\\%> or use %<;;%>)");
	    continue;
	  }
	/* Legacy '#' comment.  */
	if (c == '#')
	  {
	    while (cur () >= 0 && cur () != '\n')
	      advance ();
	    continue;
	  }

	if (digit (c))
	  { lex_number (); continue; }
	if (c == '"')
	  { lex_string (); continue; }

	if (ident_start (c))
	  {
	    location_t loc = here ();
	    std::string id;
	    while (ident_start (cur ()) || digit (cur ()))
	      { id += (char) cur (); advance (); }
	    hs_tok kind = T_IDENT;
	    for (const keyword &k : keywords)
	      if (id == k.text)
		{ kind = k.kind; break; }
	    push (kind, loc, id);
	    continue;
	  }

	location_t loc = here ();
	int n = peek ();
#define OP1(ch, tok) if (c == ch) { advance (); push (tok, loc); continue; }
#define OP2(ch, ch2, tok) \
	if (c == ch && n == ch2) { advance (); advance (); push (tok, loc); continue; }
	if (c == '.' && n == '.' && peek (2) == '=')
	  { advance (); advance (); advance (); push (P_DOTDOTEQ, loc); continue; }
	OP2 ('.', '.', P_DOTDOT)
	OP2 ('-', '>', P_ARROW)
	OP2 ('=', '=', P_EQ)
	OP2 ('!', '=', P_NE)
	OP2 ('<', '=', P_LE)
	OP2 ('>', '=', P_GE)
	OP2 ('+', '=', P_PLUS_EQ)
	OP2 ('-', '=', P_MINUS_EQ)
	OP2 ('*', '=', P_STAR_EQ)
	OP2 ('/', '=', P_SLASH_EQ)
	OP2 ('%', '=', P_PERCENT_EQ)
	OP2 ('&', '&', P_ANDAND)
	OP2 ('|', '|', P_OROR)
	OP1 ('(', P_LPAREN)
	OP1 (')', P_RPAREN)
	OP1 (',', P_COMMA)
	OP1 (':', P_COLON)
	OP1 ('=', P_ASSIGN)
	OP1 ('<', P_LT)
	OP1 ('>', P_GT)
	OP1 ('+', P_PLUS)
	OP1 ('-', P_MINUS)
	OP1 ('*', P_STAR)
	OP1 ('/', P_SLASH)
	OP1 ('%', P_PERCENT)
	OP1 ('!', P_BANG)
	OP1 (';', P_SEMI)
#undef OP1
#undef OP2
	error_at (loc, "unexpected character %qc", (char) c);
	advance ();
	push (T_ERROR, loc);
      }
    push (T_EOF, here ());
  }
};

} /* anonymous namespace */

const char *
hs_tok_name (hs_tok k)
{
  switch (k)
    {
    case T_EOF: return "end of file";
    case T_IDENT: return "identifier";
    case T_INT: return "integer literal";
    case T_FLOAT: return "float literal";
    case T_STRING: return "string literal";
    case K_IS: return "'is'";
    case K_END: return "'end'";
    case K_THEN: return "'then'";
    case K_IN: return "'in'";
    case P_LPAREN: return "'('";
    case P_RPAREN: return "')'";
    case P_COMMA: return "','";
    case P_COLON: return "':'";
    case P_ASSIGN: return "'='";
    default: return "token";
    }
}

bool
hs_lex_file (const char *filename, std::vector<hs_token> &out)
{
  FILE *f = fopen (filename, "rb");
  if (!f)
    {
      fatal_error (UNKNOWN_LOCATION, "cannot open %qs: %m", filename);
      return false;
    }
  std::string data;
  char buf[4096];
  size_t n;
  while ((n = fread (buf, 1, sizeof buf, f)) > 0)
    data.append (buf, n);
  fclose (f);

  linemap_add (line_table, LC_ENTER, false, filename, 1);
  lexer lx (data.data (), data.size (), out);
  lx.run ();
  linemap_add (line_table, LC_LEAVE, false, NULL, 0);
  return true;
}
