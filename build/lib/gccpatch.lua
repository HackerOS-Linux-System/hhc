local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

local SHIFTED = {
	"CL_PARAMS", "CL_WARNING", "CL_OPTIMIZATION", "CL_DRIVER", "CL_TARGET",
	"CL_COMMON", "CL_JOINED", "CL_SEPARATE", "CL_UNDOCUMENTED",
	"CL_NO_DWARF_RECORD", "CL_PCH_IGNORE",
}

--- Number of `Language` stanzas across all option files of the tree.
local function count_languages(srcdir)
	local files = sh.capture(string.format(
		"cd %s/gcc && { find . -name lang.opt; echo ./c-family/c.opt; }", sh.q(srcdir)))
	local n = 0
	for f in files:gmatch("[^\n]+") do
		local data = sh.read_file(srcdir .. "/gcc/" .. f)
		if data then
			for _ in data:gmatch("\nLanguage\n") do n = n + 1 end
			if data:sub(1, 9) == "Language\n" then n = n + 1 end
		end
	end
	return n
end

function M.ensure_language_capacity(cfg)
	local path = cfg.srcdir .. "/gcc/opts.h"
	local data = sh.read_file(path)
	if not data then log.die("cannot read %s", path) end

	local first = tonumber(data:match("#define%s+CL_PARAMS%s+%(1U << (%d+)%)"))
	if not first then
		log.warn("opts.h layout not recognised; skipping language-mask widening")
		return
	end

	local langs = count_languages(cfg.srcdir)
	if langs <= first then
		log.info("language mask: %d languages fit in %d bits, nothing to do", langs, first)
		return
	end

	local shift = langs - first
	local top = 0
	for _, name in ipairs(SHIFTED) do
		local bit = tonumber(data:match("#define%s+" .. name .. "%s+%(1U << (%d+)%)"))
		if not bit then
			log.warn("%s not found in opts.h; skipping language-mask widening", name)
			return
		end
		if bit + shift > top then top = bit + shift end
	end
	if top > 31 then
		log.die("language mask: cannot widen (%d bits needed, 32 available)", top + 1)
	end

	for _, name in ipairs(SHIFTED) do
		data = data:gsub("(#define%s+" .. name .. "%s+%(1U << )(%d+)(%))",
			function(a, bit, c) return a .. (tonumber(bit) + shift) .. c end)
	end
	sh.write_file(path, data)
	log.ok("language mask widened: %d languages, option class bits shifted by %d", langs, shift)
end

return M
