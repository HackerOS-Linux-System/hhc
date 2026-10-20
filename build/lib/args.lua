local M = {}

function M.parse(argv)
	local res = { command = nil, flags = {}, positional = {} }
	for _, a in ipairs(argv) do
		local key, val = a:match("^%-%-([%w%-]+)=(.*)$")
		if key then
			res.flags[key] = val
		else
			local flag = a:match("^%-%-([%w%-]+)$")
			if flag then
				res.flags[flag] = true
			elseif not res.command then
				res.command = a
			else
				res.positional[#res.positional + 1] = a
			end
		end
	end
	return res
end

--- Apply recognised flags onto cfg.
function M.apply(cfg, flags)
	if flags["ref"] then cfg.gcc.ref = flags["ref"] end
	if flags["prefix"] then cfg.prefix = flags["prefix"] end
	if flags["workdir"] then
		cfg.workdir = flags["workdir"]
		cfg.srcdir = cfg.workdir .. "/gcc-src"
		cfg.builddir = cfg.workdir .. "/gcc-build"
	end
	if flags["jobs"] then cfg.jobs = tonumber(flags["jobs"]) or cfg.jobs end
	if flags["languages"] then
		cfg.languages = {}
		for l in flags["languages"]:gmatch("[^,]+") do cfg.languages[#cfg.languages + 1] = l end
	end
	if flags["with-cxx"] then
		local have = false
		for _, l in ipairs(cfg.languages) do if l == "c++" then have = true end end
		if not have then table.insert(cfg.languages, 2, "c++") end
	end
	if flags["bootstrap"] then cfg.bootstrap = true end
	if flags["no-strip"] then cfg.strip = false end
	if flags["ccache"] then cfg.ccache = true end
	if flags["mirror"] then cfg.gcc.prefer_mirror = true end
	if flags["prereqs"] then cfg.prereqs = flags["prereqs"] end
	return cfg
end

return M
