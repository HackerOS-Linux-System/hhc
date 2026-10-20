local root = (arg[0]:match("^(.*)/tests/[^/]+$")) or "."
package.path = root .. "/build/?.lua;" .. package.path

local gccver = require("lib.gccver")
local sh = require("lib.sh")
local args = require("lib.args")

local failed = 0
local function check(name, cond)
	if cond then
		print("ok   " .. name)
	else
		print("FAIL " .. name)
		failed = failed + 1
	end
end

-- gccver
local v = gccver.parse_tag("releases/gcc-16.2.0")
check("parse_tag", v and v[1] == 16 and v[2] == 2 and v[3] == 0)
check("parse_tag rejects branch", gccver.parse_tag("releases/gcc-16") == nil)
check("parse_tag rejects rc", gccver.parse_tag("releases/gcc-16.2.0-rc1") == nil)
check("label", gccver.label("releases/gcc-16.2.0") == "16.2.0")

-- sh.q
check("quote plain", sh.q("abc") == "'abc'")
check("quote apostrophe", sh.q("a'b") == [['a'\''b']])
check("capture", sh.capture("printf hi") == "hi")

-- args
local p = args.parse({ "build", "--jobs=4", "--bootstrap", "extra" })
check("args command", p.command == "build")
check("args kv", p.flags.jobs == "4")
check("args flag", p.flags.bootstrap == true)
check("args positional", p.positional[1] == "extra")

local cfg = { gcc = { ref = "latest" }, languages = { "c", "hsharp" }, workdir = "/w",
	prefix = "/p", jobs = 1 }
args.apply(cfg, { ref = "master", languages = "c,hsharp,fortran", jobs = "8" })
check("apply ref", cfg.gcc.ref == "master")
check("apply languages", #cfg.languages == 3)
check("apply jobs", cfg.jobs == 8)

os.exit(failed == 0 and 0 or 1)
