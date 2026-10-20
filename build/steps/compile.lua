local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

function M.run(cfg)
	log.group("build: make -j" .. cfg.jobs)
	if not sh.exists(cfg.builddir .. "/Makefile") then
		log.die("not configured (run the 'configure' step first)")
	end
	sh.run("make -j" .. cfg.jobs, { cwd = cfg.builddir })
	log.endgroup()
end

return M
