local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

--- Parse "releases/gcc-16.2.0" -> {16,2,0} or nil.
function M.parse_tag(tag)
	local a, b, c = tag:match("^releases/gcc%-(%d+)%.(%d+)%.(%d+)$")
	if not a then return nil end
	return { tonumber(a), tonumber(b), tonumber(c) }
end

local function newer(x, y)
	for i = 1, 3 do
		if x[i] ~= y[i] then return x[i] > y[i] end
	end
	return false
end

--- Newest stable release tag + its commit, asking `repo` via git ls-remote.
local function latest_from(repo)
	local out, ok = sh.capture(
		"git ls-remote --tags --refs " .. sh.q(repo) .. " 'refs/tags/releases/gcc-*'",
		{ stderr_null = true })
	if not ok or out == "" then return nil end
	local best, best_tag, best_sha
	for sha, tag in out:gmatch("(%x+)%s+refs/tags/(releases/gcc%-[%d%.]+)") do
		local v = M.parse_tag(tag)
		if v and (not best or newer(v, best)) then
			best, best_tag, best_sha = v, tag, sha
		end
	end
	return best_tag, best_sha
end

--- Resolve cfg.gcc.ref to a concrete git ref.
-- Returns ref (string), commit (string|nil), repo_used (string).
function M.resolve(cfg)
	local ref = cfg.gcc.ref
	local repos = { cfg.gcc.repo, cfg.gcc.mirror }
	if cfg.gcc.prefer_mirror then repos = { cfg.gcc.mirror, cfg.gcc.repo } end

	for _, repo in ipairs(repos) do
		if ref == "latest" then
			local tag, sha = latest_from(repo)
			if tag then return tag, sha, repo end
			log.warn("could not list tags from %s, trying next source", repo)
		else
			-- explicit branch / tag / "master": just make sure the repo answers
			local _, ok = sh.capture("git ls-remote --exit-code " .. sh.q(repo) ..
				" HEAD", { stderr_null = true })
			if ok then return ref, nil, repo end
			log.warn("%s is not reachable, trying next source", repo)
		end
	end
	log.die("cannot reach any GCC git repository (network?)")
end

--- Short human label: "releases/gcc-16.2.0" -> "16.2.0".
function M.label(ref)
	return ref:match("gcc%-([%d%.]+)$") or ref:gsub("/", "-")
end

return M
