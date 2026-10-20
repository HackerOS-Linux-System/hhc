local sh      = require("lib.sh")
local log     = require("lib.log")
local gccver  = require("lib.gccver")
local state   = require("lib.state")

local M = {}

function M.run(cfg, flags)
	log.group("fetch: GCC sources")
	local ref, commit, repo = gccver.resolve(cfg)
	log.info("GCC ref: %s (from %s)", ref, repo)

	local st = state.load(cfg)
	local have = sh.is_dir(cfg.srcdir .. "/gcc") and st.ref == ref
	if have and not flags["refetch"] then
		log.ok("sources for %s already present in %s", ref, cfg.srcdir)
	else
		sh.rm_rf(cfg.srcdir)
		sh.mkdir_p(cfg.workdir)
		local ok = sh.run(string.format("git clone --depth 1 --branch %s %s %s",
			sh.q(ref), sh.q(repo), sh.q(cfg.srcdir)), { allow_fail = true })
		if not ok then
			-- try the other source once
			local other = (repo == cfg.gcc.repo) and cfg.gcc.mirror or cfg.gcc.repo
			log.warn("clone from %s failed, retrying with %s", repo, other)
			sh.rm_rf(cfg.srcdir)
			sh.run(string.format("git clone --depth 1 --branch %s %s %s",
				sh.q(ref), sh.q(other), sh.q(cfg.srcdir)))
		end
	end

	if cfg.prereqs == "download" then
		sh.run("./contrib/download_prerequisites", { cwd = cfg.srcdir })
	end

	local version = (sh.read_file(cfg.srcdir .. "/gcc/BASE-VER") or "unknown"):gsub("%s+", "")
	st.ref = ref
	st.commit = commit or sh.capture("git rev-parse HEAD", { cwd = cfg.srcdir })
	st.version = version
	state.save(cfg, st)
	log.ok("GCC %s ready (%s)", version, st.commit:sub(1, 12))
	log.endgroup()
	return st
end

return M
