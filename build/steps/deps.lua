local sh  = require("lib.sh")
local log = require("lib.log")

local M = {}

local PACKAGES = {
	apt = { "build-essential", "git", "flex", "bison", "texinfo", "m4", "perl",
	        "gawk", "xz-utils", "libgmp-dev", "libmpfr-dev", "libmpc-dev",
	        "libisl-dev", "zlib1g-dev", "libzstd-dev", "ccache" },
	dnf = { "gcc", "gcc-c++", "make", "git", "flex", "bison", "texinfo", "m4",
	        "perl", "gawk", "xz", "gmp-devel", "mpfr-devel", "libmpc-devel",
	        "isl-devel", "zlib-devel", "libzstd-devel", "ccache" },
	pacman = { "base-devel", "git", "flex", "bison", "texinfo", "m4", "perl",
	           "gawk", "xz", "gmp", "mpfr", "libmpc", "isl", "zlib", "zstd",
	           "ccache" },
	zypper = { "gcc", "gcc-c++", "make", "git", "flex", "bison", "texinfo", "m4",
	           "perl", "gawk", "xz", "gmp-devel", "mpfr-devel", "mpc-devel",
	           "isl-devel", "zlib-devel", "libzstd-devel", "ccache" },
}

local function detect()
	for _, pm in ipairs({ "apt-get", "dnf", "pacman", "zypper" }) do
		if sh.have(pm) then return (pm == "apt-get") and "apt" or pm end
	end
end

function M.run(cfg, flags)
	local pm = detect()
	if not pm then
		log.die("no supported package manager found (apt/dnf/pacman/zypper); install GCC build deps manually")
	end
	local pkgs = table.concat(PACKAGES[pm], " ")
	local sudo = (sh.capture("id -u") == "0") and "" or "sudo "
	local cmd
	if pm == "apt" then
		cmd = string.format("%sapt-get update && %sapt-get install -y --no-install-recommends %s", sudo, sudo, pkgs)
	elseif pm == "dnf" then
		cmd = string.format("%sdnf install -y %s", sudo, pkgs)
	elseif pm == "pacman" then
		cmd = string.format("%spacman -S --needed --noconfirm %s", sudo, pkgs)
	else
		cmd = string.format("%szypper --non-interactive install %s", sudo, pkgs)
	end
	log.group("deps: " .. pm)
	if flags["dry-run"] then
		print(cmd)
	else
		sh.run(cmd)
	end
	log.endgroup()
end

return M
