local M = {}

local use_color = os.getenv("NO_COLOR") == nil
	and (os.getenv("TERM") or "dumb") ~= "dumb"
	and (os.getenv("CI") == nil or os.getenv("FORCE_COLOR") ~= nil)

local in_gha = os.getenv("GITHUB_ACTIONS") == "true"

local function paint(code, s)
	if not use_color then return s end
	return string.format("\27[%sm%s\27[0m", code, s)
end

function M.info(fmt, ...)  io.stderr:write(paint("36", "[hhc] ") .. string.format(fmt, ...) .. "\n") end
function M.ok(fmt, ...)    io.stderr:write(paint("32", "[ ok] ") .. string.format(fmt, ...) .. "\n") end
function M.warn(fmt, ...)  io.stderr:write(paint("33", "[warn] ") .. string.format(fmt, ...) .. "\n") end
function M.err(fmt, ...)   io.stderr:write(paint("31", "[fail] ") .. string.format(fmt, ...) .. "\n") end
function M.cmd(s)          io.stderr:write(paint("90", "  $ " .. s) .. "\n") end

function M.die(fmt, ...)
	M.err(fmt, ...)
	os.exit(1)
end

--- Start a (foldable on CI) section.
function M.group(title)
	if in_gha then
		io.stdout:write("::group::" .. title .. "\n")
	else
		io.stderr:write("\n" .. paint("1;35", "==> " .. title) .. "\n")
	end
	io.stdout:flush()
end

function M.endgroup()
	if in_gha then io.stdout:write("::endgroup::\n") end
	io.stdout:flush()
end

return M
