local sh = require("lib.sh")

return function(root)
	local workdir = os.getenv("HHC_WORKDIR") or (root .. "/_work")

	local cfg = {
		root = root,
		hhc_version = "0.1.0",

		gcc = {
			-- "latest"   newest stable release tag (e.g. releases/gcc-16.2.0)
			-- "master"   development trunk
			-- anything else is passed to `git clone --branch`
			ref = os.getenv("HHC_GCC_REF") or "latest",
			repo = "https://gcc.gnu.org/git/gcc.git",
			mirror = "https://github.com/gcc-mirror/gcc.git",
			prefer_mirror = os.getenv("HHC_PREFER_MIRROR") == "1",
		},

		workdir = workdir,
		srcdir = workdir .. "/gcc-src",
		builddir = workdir .. "/gcc-build",
		prefix = os.getenv("HHC_PREFIX") or (root .. "/_install"),
		distdir = os.getenv("HHC_DISTDIR") or (root .. "/dist"),

		frontend_dir = root .. "/frontend/gcc/hsharp",
		patches_dir = root .. "/frontend/patches",
		tests_dir = root .. "/tests/cases",

		-- Languages built into the toolchain.  "c" is required (libgcc is
		-- built with it); "hsharp" is the H# frontend.  Add "c++" if you
		-- want a complete GCC.
		languages = { "c", "hsharp" },

		jobs = tonumber(os.getenv("HHC_JOBS")) or sh.nproc(),
		bootstrap = os.getenv("HHC_BOOTSTRAP") == "1",
		checking = os.getenv("HHC_CHECKING") or "release",
		strip = os.getenv("HHC_NO_STRIP") ~= "1",
		ccache = os.getenv("HHC_CCACHE") == "1",
		-- "system": use libgmp/libmpfr/libmpc from the distro (default)
		-- "download": run contrib/download_prerequisites inside the tree
		prereqs = os.getenv("HHC_PREREQS") or "system",

		-- Extra arguments appended verbatim to GCC's ./configure.
		extra_configure = {},
		-- Optimisation flags for the *host* build of GCC itself.
		host_cflags = os.getenv("HHC_HOST_CFLAGS") or "-O2",

		bugurl = "https://github.com/HackerOS-Linux-System/hhc/issues",
	}

	return cfg
end
