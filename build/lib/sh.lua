local log = require("lib.log")

local M = {}

--- Quote a string for /bin/sh.
function M.q(s)
	return "'" .. tostring(s):gsub("'", [['\'']]) .. "'"
end

local function env_prefix(env)
	if not env then return "" end
	local keys = {}
	for k in pairs(env) do keys[#keys + 1] = k end
	table.sort(keys)
	local parts = {}
	for _, k in ipairs(keys) do
		parts[#parts + 1] = k .. "=" .. M.q(env[k])
	end
	return "env " .. table.concat(parts, " ") .. " "
end

--- Run a shell command, streaming its output.
-- opts: cwd, env (table), allow_fail (bool), quiet (bool)
-- returns true on success, false + exit code on failure.
function M.run(cmd, opts)
	opts = opts or {}
	local full = cmd
	if opts.env then full = env_prefix(opts.env) .. full end
	if opts.cwd then full = "cd " .. M.q(opts.cwd) .. " && " .. full end
	if not opts.quiet then log.cmd(full) end
	io.stdout:flush(); io.stderr:flush()
	local ok, how, code = os.execute(full)
	if ok then return true end
	if opts.allow_fail then return false, code end
	log.die("command failed (%s %s): %s", tostring(how), tostring(code), cmd)
end

--- Run a command and capture stdout. Returns string (trimmed), exit ok.
function M.capture(cmd, opts)
	opts = opts or {}
	local full = cmd
	if opts.cwd then full = "cd " .. M.q(opts.cwd) .. " && " .. full end
	if opts.stderr_null then full = full .. " 2>/dev/null" end
	local p = io.popen(full, "r")
	if not p then return "", false end
	local out = p:read("a") or ""
	local ok = p:close()
	return (out:gsub("%s+$", "")), ok and true or false
end

--- Is `name` an executable on PATH?
function M.have(name)
	local _, ok = M.capture("command -v " .. M.q(name), { stderr_null = true })
	return ok
end

function M.exists(path)
	return os.execute("test -e " .. M.q(path)) == true
end

function M.is_dir(path)
	return os.execute("test -d " .. M.q(path)) == true
end

function M.mkdir_p(path)
	M.run("mkdir -p " .. M.q(path), { quiet = true })
end

function M.rm_rf(path)
	M.run("rm -rf " .. M.q(path), { quiet = true })
end

--- Copy directory contents: cp -R src/. dst/
function M.copy_tree(src, dst)
	M.mkdir_p(dst)
	M.run("cp -R " .. M.q(src .. "/.") .. " " .. M.q(dst), { quiet = true })
end


--- Mirror src -> dst, copying only files whose contents changed and removing
-- files that no longer exist in src.  Keeps make timestamps stable, so an
-- incremental GCC rebuild only recompiles what really changed.
function M.sync_tree(src, dst)
	M.mkdir_p(dst)
	local files = M.capture("cd " .. M.q(src) .. " && find . -type f | sort")
	local wanted = {}
	for rel in files:gmatch("[^\n]+") do
		wanted[rel] = true
		local from, to = src .. "/" .. rel, dst .. "/" .. rel
		if os.execute("cmp -s " .. M.q(from) .. " " .. M.q(to)) ~= true then
			M.run("mkdir -p " .. M.q(to:match("^(.*)/[^/]+$")) .. " && cp -p " ..
				M.q(from) .. " " .. M.q(to), { quiet = true })
		end
	end
	local existing = M.capture("cd " .. M.q(dst) .. " && find . -type f | sort")
	for rel in existing:gmatch("[^\n]+") do
		if not wanted[rel] then M.rm_rf(dst .. "/" .. rel) end
	end
end

function M.read_file(path)
	local f = io.open(path, "rb")
	if not f then return nil end
	local s = f:read("a")
	f:close()
	return s
end

function M.write_file(path, data)
	local f, err = io.open(path, "wb")
	if not f then log.die("cannot write %s: %s", path, tostring(err)) end
	f:write(data)
	f:close()
end

--- Sorted list of entries (names) in a directory matching a Lua pattern.
function M.list(dir, pattern)
	local out = M.capture("ls -1 " .. M.q(dir), { stderr_null = true })
	local res = {}
	for name in out:gmatch("[^\n]+") do
		if not pattern or name:match(pattern) then res[#res + 1] = name end
	end
	table.sort(res)
	return res
end

function M.nproc()
	local n = tonumber((M.capture("nproc", { stderr_null = true })))
	return n or 1
end

function M.abspath(path)
	local out = M.capture("cd " .. M.q(path) .. " 2>/dev/null && pwd")
	if out ~= "" then return out end
	-- path does not exist yet: resolve its parent
	local dir, base = path:match("^(.*)/([^/]+)$")
	if not dir then return M.capture("pwd") .. "/" .. path end
	if dir == "" then dir = "/" end
	return M.abspath(dir) .. "/" .. base
end

return M
