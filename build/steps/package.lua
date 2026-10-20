local sh    = require("lib.sh")
local log   = require("lib.log")
local state = require("lib.state")

local M = {}

function M.run(cfg)
	log.group("package")
	local st = state.load(cfg)
	local os_name = sh.capture("uname -s"):lower()
	local arch = sh.capture("uname -m")
	local name = string.format("hhc-%s-gcc%s-%s-%s.tar.xz",
		cfg.hhc_version, st.version or "unknown", os_name, arch)

	sh.mkdir_p(cfg.distdir)
	local out = cfg.distdir .. "/" .. name
	sh.run(string.format("tar -C %s -cf - . | xz -T0 -6 > %s", sh.q(cfg.prefix), sh.q(out)))
	sh.run("sha256sum " .. sh.q(name) .. " > " .. sh.q(name .. ".sha256"), { cwd = cfg.distdir })
	log.ok("%s", out)
	state.gha_output("package", out)
	state.gha_output("package_name", name)
	log.endgroup()
end

return M
