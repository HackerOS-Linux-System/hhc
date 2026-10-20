local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

local function trim(s) return (s:gsub("%s+$", "")) end

function M.run(cfg, flags)
	log.group("test: tests/cases")
	local hhc = flags["hhc"] or (cfg.prefix .. "/bin/hhc")
	if not sh.exists(hhc) then
		log.die("%s not found (run 'install' first or pass --hhc=PATH)", hhc)
	end

	local tmp = sh.capture("mktemp -d")
	local pass, fail = 0, 0

	for _, file in ipairs(sh.list(cfg.tests_dir, "%.h#$")) do
		local name = file:gsub("%.h#$", "")
		local src = cfg.tests_dir .. "/" .. file
		local exe = tmp .. "/" .. name
		local errfile = tmp .. "/" .. name .. ".stderr"
		local want_err = sh.read_file(cfg.tests_dir .. "/" .. name .. ".expected_error")
		local want_out = sh.read_file(cfg.tests_dir .. "/" .. name .. ".expected")

		local compiled = os.execute(string.format("%s %s -o %s 2> %s",
			sh.q(hhc), sh.q(src), sh.q(exe), sh.q(errfile))) == true
		local stderr = sh.read_file(errfile) or ""

		local good = false
		local why = ""
		if want_err then
			if compiled then
				why = "expected a compile error, but it compiled"
			elseif not stderr:find(trim(want_err), 1, true) then
				why = "error text not found: " .. trim(want_err) .. "\n" .. stderr
			else
				good = true
			end
		elseif not compiled then
			why = "compilation failed:\n" .. stderr
		else
			local out = sh.capture(sh.q(exe) .. " 2>&1")
			if want_out and trim(out) == trim(want_out) then
				good = true
			else
				why = "output mismatch\n--- expected\n" .. trim(want_out or "<missing .expected>") ..
					"\n--- got\n" .. out
			end
		end

		if good then
			pass = pass + 1
			log.ok("%s", name)
		else
			fail = fail + 1
			log.err("%s: %s", name, why)
		end
	end

	sh.rm_rf(tmp)
	log.info("%d passed, %d failed", pass, fail)
	log.endgroup()
	if fail > 0 or pass == 0 then os.exit(1) end
end

return M
