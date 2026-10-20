/* lang-specs.h -- contribution to the `default_compilers' array in gcc.cc
   for the H# language.  H# sources use the ".h#" suffix.  */

{".h#", "@hsharp", 0, 1, 0},
{"@hsharp",
 "%{!E:hsharp1 %i %(cc1_options) %{I*} %{!fsyntax-only:%(invoke_as)}}",
 0, 1, 0},
