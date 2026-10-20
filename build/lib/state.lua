local sh = require("lib.sh")

local M = {}

local function path(cfg) return cfg.workdir .. "/.hhc-state" end

function M.load(cfg)
	local data = sh.read_file(path(cfg))
	local st = {}
	if data then
		for k, v in data:gmatch("([%w_]+)=([^\n]*)") do st[k] = v end
	end
	return st
end

function M.save(cfg, st)
	sh.mkdir_p(cfg.workdir)
	local keys = {}
	for k in pairs(st) do keys[#keys + 1] = k end
	table.sort(keys)
	local lines = {}
	for _, k in ipairs(keys) do lines[#lines + 1] = k .. "=" .. tostring(st[k]) end
	sh.write_file(path(cfg), table.concat(lines, "\n") .. "\n")
end

--- Append KEY=VALUE to $GITHUB_OUTPUT when running on GitHub Actions.
function M.gha_output(key, value)
	local f = os.getenv("GITHUB_OUTPUT")
	if not f then return end
	local h = io.open(f, "a")
	if h then h:write(key .. "=" .. value .. "\n"); h:close() end
end

return M
