#!/bin/sh
# install.sh -- everything a bare Debian 12 (i386) needs to run the tools in
# this directory: ./maho432, ./send, ./recv and ./sniff.
#
# The package list is short on purpose: the tools are plain Python 3 and use
# the standard library only (termios, fcntl, select, struct). Nothing here is
# tied to one particular adapter. The FTDI USB converter used on the
# development laptop needs no package -- the in-kernel ftdi_sio driver already
# handles it -- and a port on the motherboard or on an expansion card needs
# nothing at all.
set -e

# Everything is resolved from the script itself, not from the current
# directory: the installer gets run from wherever (sh /media/usb/maho432/install.sh)
# and "./" would then point somewhere else entirely.
HERE="$(cd "$(dirname "$0")" && pwd)"

sudo apt-get update

# One apt-get per package, never a shared list.
#
# A shared list is all-or-nothing: if a single name cannot be resolved on this
# release, apt installs NOTHING from that command, and under "set -e" the whole
# script dies there -- so an optional extra takes the essentials down with it.
# (An already-installed package is not the problem; apt just skips those.)
# One call per package isolates that, and the failures are collected and shown
# at the end instead of aborting.
FAILED=""
pkg() {
    echo "-- apt-get install $1"
    if sudo apt-get install -y "$1"; then
        return 0
    fi
    FAILED="$FAILED $1"
    echo "   !! $1 did not install"
    return 0
}

# The only package the tools themselves need: plain Python 3, standard library
# only (termios, fcntl, select, struct).
pkg python3

# Serial port access. The device nodes belong to group "dialout":
#   crw-rw---- 1 root dialout /dev/ttyS0
# Without this membership the tools only work as root.
sudo usermod -aG dialout "$USER"

# Fail here rather than in front of the machine. Debian 12 ships Python 3.11,
# and the tools are written for it; this checks they actually parse.
python3 -c 'import sys
for f in sys.argv[1:]:
    compile(open(f, encoding="utf-8").read(), f, "exec")
print("syntax OK on python " + ".".join(map(str, sys.version_info[:3])))' "$HERE/maho432" "$HERE/send" "$HERE/recv" "$HERE/sniff"

# setserial is NOT installed and is not needed for autodetection: the tools
# read /sys/class/tty/ttySN/type, which the kernel fills in by itself for
# motherboard and PCI/PCIe cards. It only becomes useful for a legacy ISA card
# that the kernel cannot probe -- there the port stays type=0 and the tools
# will not see it. In that case:  sudo apt-get install -y setserial
#
# NOTE: dmesg | grep ttyS  shows which ports the kernel actually found.

echo
if [ -n "$FAILED" ]; then
    echo "!! these packages did NOT install:$FAILED"
    echo "   everything else was installed; fix these by hand and re-run."
    echo
fi
echo "Done. Log out and back in for the dialout group to take effect,"
echo "then list the serial ports with:   $HERE/maho432 ports"
