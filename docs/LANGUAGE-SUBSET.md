# H# subset accepted by hhc (stage 0)

```
program  := item*
item     := 'using' STRING                      ;; edition marker, ignored
          | ['pub'] 'fn' IDENT '(' params ')' ['->' type] ('is' block 'end' | '=' expr)
type     := int | uint | i8 | i16 | i32 | i64 | u8 | u16 | u32 | u64
          | f32 | f64 | bool | string | void
stmt     := ('let'|'const') ['mut'] IDENT [':' type] '=' expr
          | IDENT ('=' | '+=' | '-=' | '*=' | '/=' | '%=') expr
          | 'if' expr ('is'|'then') block {'elsif' expr ('is'|'then') block}
            ['else' ['is'] block] 'end'
          | 'while' expr ['is'] block 'end'
          | 'loop' ['is'] block 'end'
          | 'for' IDENT 'in' expr ('..'|'..=') expr ['is'] block 'end'
          | 'break' | 'continue' | 'return' [expr]
          | 'write' '(' expr ')'
          | call
expr     := || && == != < <= > >= + - * / %  unary - !   literals, calls, ( )
```

Comments: `;; line`, `/// doc`, `// block ... \\`, legacy `# line`.

Semantics worth knowing:

* `int` = `i64`, `uint` = `u64`. Integer literals adopt the type the context
  needs (`let x: u8 = 200`); there is no implicit conversion between typed
  values - a mismatch is a compile error.
* `fn main()` becomes C `int main(void)` and returns 0.
* `write(x)` prints any scalar and a newline. `bool` prints `true` / `false`.
* `"a" + to_string(n) + "b"` and `"x = {n}"` (identifier interpolation) are
  lowered to a chain of prints. String concatenation anywhere else is
  rejected with a clear error until `libhsharp` exists.
* Everything outside this list is a diagnostic, never a crash.
