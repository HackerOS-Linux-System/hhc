local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

function M.run(cfg)
	log.group("install -> " .. cfg.prefix)
	sh.run(cfg.strip and "make install-strip" or "make install", { cwd = cfg.builddir })

	local hhc = cfg.prefix .. "/bin/hhc"
	if not sh.exists(hhc) then
		log.die("build finished but %s was not installed", hhc)
	end
	local ver = sh.capture(sh.q(hhc) .. " --version 2>&1 | head -n1")
	log.ok("installed: %s", ver)
	log.endgroup()
end

return M
