local sh    = require("lib.sh")
local log   = require("lib.log")
local state = require("lib.state")
local gccpatch = require("lib.gccpatch")

local M = {}

local function apply_patches(cfg, dir)
	for _, name in ipairs(sh.list(dir, "%.patch$")) do
		local file = dir .. "/" .. name
		local already = sh.run(string.format("patch -p1 -R --dry-run -s -d %s < %s",
			sh.q(cfg.srcdir), sh.q(file)), { quiet = true, allow_fail = true })
		if already then
			log.info("patch %s already applied", name)
		else
			log.info("applying %s", name)
			sh.run(string.format("patch -p1 -d %s < %s", sh.q(cfg.srcdir), sh.q(file)))
		end
	end
end

function M.run(cfg)
	log.group("overlay: H# frontend -> GCC tree")
	if not sh.exists(cfg.srcdir .. "/gcc/configure.ac") then
		log.die("no GCC sources in %s (run the 'fetch' step first)", cfg.srcdir)
	end
	if not sh.exists(cfg.frontend_dir .. "/config-lang.in") then
		log.die("frontend not found at %s", cfg.frontend_dir)
	end

	local dst = cfg.srcdir .. "/gcc/hsharp"
	sh.sync_tree(cfg.frontend_dir, dst)
	log.ok("frontend synced into %s", dst)

	-- H# is a new language slot; make sure GCC's option mask has room for it.
	gccpatch.ensure_language_capacity(cfg)

	local st = state.load(cfg)
	local major = (st.version or ""):match("^(%d+)")
	apply_patches(cfg, cfg.patches_dir .. "/gcc-all")
	if major then apply_patches(cfg, cfg.patches_dir .. "/gcc-" .. major) end
	log.endgroup()
end

return M
