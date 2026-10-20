local sh    = require("lib.sh")
local log   = require("lib.log")
local state = require("lib.state")

local M = {}

function M.run(cfg, flags)
	log.group("configure: GCC + H# frontend")
	local st = state.load(cfg)
	if not sh.exists(cfg.srcdir .. "/gcc/hsharp/config-lang.in") then
		log.die("frontend is not in the GCC tree (run the 'overlay' step first)")
	end

	if sh.exists(cfg.builddir .. "/Makefile") and not flags["reconfigure"] then
		log.ok("already configured (%s); use --reconfigure to redo", cfg.builddir)
		log.endgroup()
		return
	end
	if flags["reconfigure"] then sh.rm_rf(cfg.builddir) end
	sh.mkdir_p(cfg.builddir)

	local args = {
		"--prefix=" .. cfg.prefix,
		"--enable-languages=" .. table.concat(cfg.languages, ","),
		"--disable-multilib",
		"--disable-nls",
		"--disable-werror",
		cfg.bootstrap and "--enable-bootstrap" or "--disable-bootstrap",
		"--enable-checking=" .. cfg.checking,
		"--enable-default-pie",
		"--with-pkgversion=hhc " .. cfg.hhc_version .. " (GCC " .. (st.version or "?") .. ")",
		"--with-bugurl=" .. cfg.bugurl,
		-- H# only needs libc + libgcc; skip the heavy optional runtimes.
		"--disable-libsanitizer", "--disable-libgomp", "--disable-libquadmath",
		"--disable-libssp", "--disable-libvtv", "--disable-libitm",
		"--disable-libatomic",
	}
	for _, a in ipairs(cfg.extra_configure) do args[#args + 1] = a end

	local quoted = {}
	for i, a in ipairs(args) do quoted[i] = sh.q(a) end

	local env = { CFLAGS = cfg.host_cflags, CXXFLAGS = cfg.host_cflags }
	if cfg.ccache then
		env.CC = "ccache gcc"
		env.CXX = "ccache g++"
	end

	sh.run(sh.q(cfg.srcdir .. "/configure") .. " " .. table.concat(quoted, " "),
		{ cwd = cfg.builddir, env = env })
	log.endgroup()
end

return M
