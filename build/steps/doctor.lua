local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

local TOOLS = {
	{ "git",    true },
	{ "make",   true },
	{ "gcc",    true },
	{ "g++",    true },
	{ "flex",   true },
	{ "bison",  true },
	{ "makeinfo", true },
	{ "m4",     true },
	{ "perl",   true },
	{ "tar",    true },
	{ "xz",     false },
	{ "ccache", false },
}

local HEADERS = { "gmp.h", "mpfr.h", "mpc.h" }

local function have_header(h)
	local cmd = string.format("printf '#include <%s>\\n' | cc -E -x c - >/dev/null 2>&1", h)
	return os.execute(cmd) == true
end

function M.run(cfg)
	log.group("doctor: host tools")
	local missing = 0
	for _, t in ipairs(TOOLS) do
		local ok = sh.have(t[1])
		if ok then
			log.ok("%-9s found", t[1])
		elseif t[2] then
			log.err("%-9s MISSING (required)", t[1])
			missing = missing + 1
		else
			log.warn("%-9s missing (optional)", t[1])
		end
	end
	if cfg.prereqs == "system" then
		for _, h in ipairs(HEADERS) do
			if have_header(h) then
				log.ok("%-9s found", h)
			else
				log.err("%-9s MISSING (install libgmp-dev libmpfr-dev libmpc-dev, or use --prereqs=download)", h)
				missing = missing + 1
			end
		end
	end
	log.info("Lua: %s", _VERSION)
	if _VERSION ~= "Lua 5.5" then
		log.warn("these scripts target Lua 5.5 (running %s); they stay 5.4-compatible", _VERSION)
	end
	log.endgroup()
	if missing > 0 then
		log.die("%d requirement(s) missing; try: lua5.5 hhc.lua deps", missing)
	end
end

return M
