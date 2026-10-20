# Patches for core GCC files

Normally empty. If a GCC release changes an interface the frontend uses and a
guard inside `frontend/gcc/hsharp` is not enough, add a unified diff here:

* `gcc-all/*.patch`   applied to every GCC version
* `gcc-16/*.patch`    applied only to GCC 16.x  (directory name = major version)

Patches are applied with `patch -p1` from the GCC source root, in file name
order, by `lua5.5 hhc.lua overlay`; already-applied patches are skipped.
