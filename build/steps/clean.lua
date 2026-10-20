local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

function M.run(cfg, flags)
	log.group("clean")
	sh.rm_rf(cfg.builddir)
	log.info("removed %s", cfg.builddir)
	if flags["all"] then
		for _, p in ipairs({ cfg.workdir, cfg.prefix, cfg.distdir }) do
			sh.rm_rf(p)
			log.info("removed %s", p)
		end
	end
	log.endgroup()
end

return M
