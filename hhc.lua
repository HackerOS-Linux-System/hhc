#!/usr/bin/env lua5.5
-- hhc.lua -- build driver for hhc, the GCC-based H# compiler.
--
--   lua5.5 hhc.lua all                 # everything, newest stable GCC
--   lua5.5 hhc.lua build --jobs=8      # a single step
--   lua5.5 hhc.lua help
--
-- Pure Lua, no external Lua modules.  Written for Lua 5.5, still runs on 5.4.

-- Locate the repository root from this script's own path.
local script = arg and arg[0] or "hhc.lua"
local root = script:match("^(.*)/[^/]+$") or "."
local function abs(path)
	local p = io.popen("cd '" .. path:gsub("'", "'\\''") .. "' && pwd")
	local out = p:read("l")
	p:close()
	return out
end
root = abs(root)

package.path = root .. "/build/?.lua;" .. package.path

local log    = require("lib.log")
local args   = require("lib.args")
local state  = require("lib.state")
local gccver = require("lib.gccver")
local sh     = require("lib.sh")

local cfg = require("config")(root)

local parsed = args.parse(arg)
args.apply(cfg, parsed.flags)
local flags = parsed.flags

local STEPS = { "doctor", "fetch", "overlay", "configure", "build", "install", "test", "package" }
local step_module = { build = "compile" }

local function run_step(name)
	local mod = require("steps." .. (step_module[name] or name))
	return mod.run(cfg, flags)
end

local HELP = [[
hhc build driver  --  GCC + H# frontend, binary name: hhc

usage: lua5.5 hhc.lua <command> [options]

commands:
  all          doctor, fetch, overlay, configure, build, install, test, package
  doctor       check host tools and libraries
  deps         install build dependencies (apt / dnf / pacman / zypper)
  resolve      print the GCC ref that would be built (--github-output for CI)
  fetch        clone GCC sources (newest stable release by default)
  overlay      copy the H# frontend into the GCC tree (+ optional patches)
  configure    run GCC's configure (out-of-tree)
  build        make -jN
  install      make install-strip into the prefix, verify bin/hhc
  test         compile and run tests/cases with the installed hhc
  package      create dist/hhc-<ver>-gcc<ver>-<os>-<arch>.tar.xz
  clean        remove the build dir (--all: sources, prefix, dist too)
  version      print versions
  selfcheck    syntax-check every Lua file of this build system

options:
  --ref=REF         latest (default) | master | releases/gcc-16.2.0 | any branch/tag
  --prefix=DIR      install prefix            (default: ./_install)
  --workdir=DIR     sources and build dir     (default: ./_work)
  --jobs=N          parallel jobs             (default: nproc)
  --languages=LIST  default: c,hsharp         (--with-cxx adds c++)
  --bootstrap       three-stage GCC bootstrap (slow, off by default)
  --ccache          build with ccache
  --mirror          prefer the GitHub mirror over gcc.gnu.org
  --prereqs=MODE    system (default) | download
  --reconfigure     redo configure even if a Makefile exists
  --refetch         re-clone the GCC sources
  --no-strip        use make install instead of install-strip

environment: HHC_GCC_REF HHC_PREFIX HHC_WORKDIR HHC_JOBS HHC_CCACHE HHC_BOOTSTRAP ...
]]

local commands = {}

function commands.help() io.write(HELP) end

function commands.version()
	local st = state.load(cfg)
	print("hhc build system " .. cfg.hhc_version)
	print("lua            " .. _VERSION)
	print("gcc ref        " .. (st.ref or cfg.gcc.ref) .. " (configured: " .. cfg.gcc.ref .. ")")
	print("gcc version    " .. (st.version or "not fetched yet"))
end

function commands.resolve()
	local ref, commit = gccver.resolve(cfg)
	print(ref)
	if flags["github-output"] then
		state.gha_output("gcc_ref", ref)
		state.gha_output("gcc_commit", commit or "")
		state.gha_output("gcc_label", gccver.label(ref))
	end
end

function commands.deps() require("steps.deps").run(cfg, flags) end
function commands.clean() require("steps.clean").run(cfg, flags) end

function commands.selfcheck()
	local files = sh.capture("cd " .. sh.q(root) .. " && find hhc.lua build tests -name '*.lua' | sort")
	local bad = 0
	for f in files:gmatch("[^\n]+") do
		local fn, err = loadfile(root .. "/" .. f)
		if fn then
			log.ok("%s", f)
		else
			log.err("%s: %s", f, err)
			bad = bad + 1
		end
	end
	if bad > 0 then os.exit(1) end
end

for _, name in ipairs(STEPS) do
	commands[name] = function() run_step(name) end
end

function commands.all()
	for _, name in ipairs(STEPS) do run_step(name) end
	log.ok("done: %s/bin/hhc", cfg.prefix)
end

local cmd = parsed.command or "help"
local fn = commands[cmd]
if not fn then
	log.err("unknown command '%s'", cmd)
	io.write(HELP)
	os.exit(2)
end
fn()
