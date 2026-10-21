local sh    = require("lib.sh")
local log   = require("lib.log")
local state = require("lib.state")

local M = {}

local ARCH = {
	x86_64 = "amd64", aarch64 = "arm64", armv7l = "armhf", i686 = "i386",
	ppc64le = "ppc64el", riscv64 = "riscv64", s390x = "s390x",
}

-- Things H# does not need (it has its own cc1-equivalent, hsharp1).
local PRUNE = {
	"share/info", "share/man", "share/locale", "include",
	"lib/gcc/*/*/plugin", "lib/gcc/*/*/install-tools",
	"libexec/gcc/*/*/cc1", "libexec/gcc/*/*/cc1plus", "libexec/gcc/*/*/install-tools",
	"libexec/gcc/*/*/plugin", "libexec/gcc/*/*/g++-mapper-server",
	"libexec/gcc/*/*/lto-dump",
	"bin/cpp", "bin/gcc", "bin/gcc-ar", "bin/gcc-nm", "bin/gcc-ranlib",
	"bin/gcov", "bin/gcov-dump", "bin/gcov-tool", "bin/lto-dump",
	"bin/*-gcc", "bin/*-gcc-*", "bin/c++", "bin/g++", "bin/*-g++", "bin/*-c++",
}

local function is_elf(path)
	local f = io.open(path, "rb")
	if not f then return false end
	local magic = f:read(4)
	f:close()
	return magic == "\127ELF"
end

--- Detect runtime package dependencies of every dynamic ELF under `root`.
-- Returns a sorted list like { "libc6 (>= 2.36)", "libgmp10", ... } or nil.
local function detect_deps(root)
	if not (sh.have("ldd") and sh.have("dpkg-query")) then return nil end

	local files = sh.capture("find " .. sh.q(root) .. " -type f")
	local libpaths, seen = {}, {}
	for f in files:gmatch("[^\n]+") do
		if is_elf(f) then
			local out = sh.capture("ldd " .. sh.q(f), { stderr_null = true })
			for line in out:gmatch("[^\n]+") do
				local path = line:match("=>%s*(/%S+)")
				if path and path:sub(1, #root) ~= root and not seen[path] then
					seen[path] = true
					libpaths[#libpaths + 1] = path
				end
			end
		end
	end

	local function owner(path)
		local candidates = { path }
		local real = sh.capture("readlink -f " .. sh.q(path), { stderr_null = true })
		if real ~= "" then candidates[#candidates + 1] = real end
		candidates[#candidates + 1] = (path:gsub("^/lib/", "/usr/lib/"))
		candidates[#candidates + 1] = (path:gsub("^/usr/lib/", "/lib/"))
		for _, c in ipairs(candidates) do
			local out, ok = sh.capture("dpkg-query -S " .. sh.q(c), { stderr_null = true })
			if ok and out ~= "" then
				local pkg = out:match("^([^:%s,]+)")
				if pkg then return pkg end
			end
		end
	end

	local pkgs = {}
	for _, p in ipairs(libpaths) do
		local pkg = owner(p)
		if pkg then
			local ver = sh.capture("dpkg-query -W -f='${Version}' " .. sh.q(pkg), { stderr_null = true })
			local upstream = ver:gsub("%-[^%-]*$", "")	-- drop the Debian revision
			if not pkgs[pkg] or pkgs[pkg] < upstream then pkgs[pkg] = upstream end
		else
			log.warn("no Debian package owns %s", p)
		end
	end

	local list = {}
	for pkg, ver in pairs(pkgs) do
		if pkg == "libc6" and ver ~= "" then
			list[#list + 1] = string.format("%s (>= %s)", pkg, ver)
		else
			list[#list + 1] = pkg
		end
	end
	table.sort(list)
	return list
end

function M.run(cfg, flags)
	log.group("deb: Debian package")
	if not sh.have("dpkg-deb") then
		log.die("dpkg-deb not found (this step needs a Debian/Ubuntu host)")
	end
	if not sh.exists(cfg.prefix .. "/bin/hhc") then
		log.die("%s/bin/hhc not found (run 'install' first)", cfg.prefix)
	end

	local st = state.load(cfg)
	local gccver = st.version or "unknown"
	local machine = sh.capture("uname -m")
	local arch = ARCH[machine]
	if not arch then log.die("unsupported architecture for .deb: %s", machine) end

	local version = string.format("%s+gcc%s-%s", cfg.hhc_version, gccver, cfg.deb_revision)
	local stage = cfg.workdir .. "/deb-stage"
	local libdir = stage .. "/usr/lib/hhc"

	sh.rm_rf(stage)
	sh.mkdir_p(libdir)
	sh.mkdir_p(stage .. "/usr/bin")
	sh.mkdir_p(stage .. "/usr/share/man/man1")
	sh.mkdir_p(stage .. "/usr/share/doc/hhc")
	sh.mkdir_p(stage .. "/DEBIAN")

	-- 1. the GCC prefix, pruned
	sh.run("cp -a " .. sh.q(cfg.prefix .. "/.") .. " " .. sh.q(libdir), { quiet = true })
	for _, pat in ipairs(PRUNE) do
		sh.run("rm -rf " .. sh.q(libdir) .. "/" .. pat, { quiet = true })
	end
	-- keep only the driver (and its target-prefixed twin) in bin/
	sh.run("rm -f " .. sh.q(libdir) .. "/lib/libcc1.so* " .. sh.q(libdir) .. "/lib64/libcc1.so*", { quiet = true })

	-- 2. command on PATH
	sh.run("ln -s ../lib/hhc/bin/hhc " .. sh.q(stage .. "/usr/bin/hhc"), { quiet = true })

	-- 3. man page + docs
	local pk = cfg.root .. "/packaging/debian"
	sh.run(string.format("gzip -9n -c %s > %s", sh.q(pk .. "/hhc.1"),
		sh.q(stage .. "/usr/share/man/man1/hhc.1.gz")), { quiet = true })
	sh.run("cp " .. sh.q(pk .. "/copyright") .. " " .. sh.q(stage .. "/usr/share/doc/hhc/copyright"), { quiet = true })
	local changelog = string.format(
		"hhc (%s) unstable; urgency=medium\n\n  * Built from GCC %s with the H# frontend.\n\n -- %s  %s\n",
		version, gccver, cfg.maintainer, sh.capture("date -R"))
	local clpath = stage .. "/usr/share/doc/hhc/changelog.gz"
	sh.write_file(stage .. "/changelog.tmp", changelog)
	sh.run(string.format("gzip -9n -c %s > %s && rm %s", sh.q(stage .. "/changelog.tmp"),
		sh.q(clpath), sh.q(stage .. "/changelog.tmp")), { quiet = true })

	-- 4. permissions
	sh.run("find " .. sh.q(stage) .. " -type d -exec chmod 0755 {} +", { quiet = true })
	sh.run("chmod -R go-w " .. sh.q(stage), { quiet = true })

	-- 5. control file
	local deps = detect_deps(libdir)
	local depends = { "binutils", "libc6-dev | libc-dev" }
	if deps then
		for _, d in ipairs(deps) do depends[#depends + 1] = d end
	else
		log.warn("could not detect shared library dependencies; using a generic list")
		for _, d in ipairs({ "libc6", "libgmp10", "libmpfr6", "libmpc3", "libstdc++6", "libgcc-s1", "zlib1g" }) do
			depends[#depends + 1] = d
		end
	end
	local size_kb = tonumber((sh.capture("du -sk --exclude=DEBIAN " .. sh.q(stage)):match("^(%d+)"))) or 0

	local control = table.concat({
		"Package: hhc",
		"Version: " .. version,
		"Architecture: " .. arch,
		"Maintainer: " .. cfg.maintainer,
		"Installed-Size: " .. size_kb,
		"Depends: " .. table.concat(depends, ", "),
		"Section: devel",
		"Priority: optional",
		"Homepage: https://github.com/HackerOS-Linux-System/hhc",
		"Description: GCC-based compiler for the H# programming language",
		" hhc is the GNU Compiler Collection driver for H#, the language of",
		" HackerOS. It is a GCC frontend (like gdc for D), so H# programs are",
		" optimised and compiled to native code by GCC's own middle and back",
		" end.",
		" .",
		" This package bundles GCC " .. gccver .. " with the H# frontend under",
		" /usr/lib/hhc and installs only the `hhc` command on PATH; it does not",
		" replace or conflict with the system gcc.",
		"",
	}, "\n")
	sh.write_file(stage .. "/DEBIAN/control", control)

	-- 6. build
	sh.mkdir_p(cfg.distdir)
	local name = string.format("hhc_%s_%s.deb", version, arch)
	local out = cfg.distdir .. "/" .. name
	sh.run(string.format("dpkg-deb --root-owner-group -Zxz --build %s %s",
		sh.q(stage), sh.q(out)))
	sh.run("sha256sum " .. sh.q(name) .. " > " .. sh.q(name .. ".sha256"), { cwd = cfg.distdir })

	sh.run("dpkg-deb --info " .. sh.q(out))
	log.ok("%s (%d kB installed)", out, size_kb)
	state.gha_output("deb", out)
	state.gha_output("deb_name", name)
	log.endgroup()
end

return M
