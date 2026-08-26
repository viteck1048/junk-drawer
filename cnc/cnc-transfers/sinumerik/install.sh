#!/bin/sh
# install.sh -- everything a bare Debian 12 (i386) needs to run the tools in
# this directory: ./sinumerik, ./sniff and the RV001-restore wizard.
#
# The package list is short on purpose: the tools are plain Python 3 and use
# the standard library only (termios, fcntl, select, struct). Nothing here is
# tied to one particular adapter. The FTDI USB converter used on the
# development laptop needs no package -- the in-kernel ftdi_sio driver already
# handles it -- and a port on the motherboard or on an expansion card needs
# nothing at all.
set -e

# Everything is resolved from the script itself, not from the current
# directory: the installer gets run from wherever (sh /media/usb/sinumerik/install.sh)
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

# Optional, for the desktop shortcut below: xdg-user-dir reports the real name
# of the desktop folder, which is localised -- on a Bulgarian system it is
# "Работен плот", not "Desktop" -- so it must not be guessed. If this one fails
# the shortcut still gets installed into the applications menu.
pkg xdg-user-dirs

# Optional, provides "gio". Needed on Cinnamon: its file manager (nemo) keeps a
# separate "trusted" flag and refuses to start a launcher without it. XFCE and
# KDE do not care -- for them the executable bit is enough. Usually already
# present as a dependency of the desktop itself; named here so a minimal
# install does not end up with a dead icon.
pkg libglib2.0-bin

# Serial port access. The device nodes belong to group "dialout":
#   crw-rw---- 1 root dialout /dev/ttyS0
# Without this membership the tools only work as root.
sudo usermod -aG dialout "$USER"

# Fail here rather than in front of the machine. Debian 12 ships Python 3.11,
# and the tools are written for it; this checks they actually parse.
python3 -c 'import sys
for f in sys.argv[1:]:
    compile(open(f, encoding="utf-8").read(), f, "exec")
print("syntax OK on python " + ".".join(map(str, sys.version_info[:3])))' \
    "$HERE/sinumerik" "$HERE/sniff" "$HERE/RV001-restore/restore_with_instructions.py"

# ---------------------------------------------------------------------------
# Shortcut for the RV-001 restore wizard.
#
# The desktop environment is not chosen yet -- most likely XFCE or Cinnamon on
# the shop machines, KDE on Viktor's own -- and definitely not GNOME. So nothing
# here may depend on one. The entry is written in the freedesktop format that
# XFCE, KDE, LXQt, MATE and Cinnamon all read, and it is installed twice:
#
#   ~/.local/share/applications/   the application menu -- works in every
#                                  environment, and survives a DE switch
#   <desktop folder>/              the icon on the desktop itself, if that
#                                  folder exists at all
#
# The path is derived from where this script actually sits, so the shortcut is
# correct wherever the directory was copied to -- nothing is hard-coded.
WIZARD="$HERE/RV001-restore/restore_with_instructions.py"

if [ ! -x "$WIZARD" ]; then
    echo "no $WIZARD -- shortcut skipped"
else
    # Terminal=true: the wizard is an interactive full-screen dialogue with the
    # operator, it cannot run detached. Which terminal gets opened is up to the
    # environment -- that is exactly why this is a flag and not a hard-coded
    # "xfce4-terminal -e ...".
    # The visible texts are Bulgarian on purpose: that is who reads them.
    ENTRY="$(cat <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=РВ-001 · възстановяване
Comment=Пълно възстановяване на Sinumerik РВ-001 след изтриване на EPROM
Exec="$WIZARD"
Path=$HERE/RV001-restore
Terminal=true
Icon=utilities-terminal
Categories=Utility;
EOF
)"

    APPS="$HOME/.local/share/applications"
    mkdir -p "$APPS"
    printf '%s\n' "$ENTRY" > "$APPS/rv001-restore.desktop"
    echo "menu entry:  $APPS/rv001-restore.desktop"
    # Refresh the menu cache where the tool exists; no environment needs it to
    # succeed, the entry is picked up on next login regardless.
    update-desktop-database "$APPS" 2>/dev/null || true

    DESKTOP="$(xdg-user-dir DESKTOP 2>/dev/null || true)"
    [ -n "$DESKTOP" ] || DESKTOP="$HOME/Desktop"

    if [ ! -d "$DESKTOP" ]; then
        # No graphical session, or a DE that keeps no desktop folder: not an
        # error. The menu entry above is already in place.
        echo "no desktop folder ($DESKTOP) -- menu entry only"
    else
        LAUNCHER="$DESKTOP/rv001-restore.desktop"
        printf '%s\n' "$ENTRY" > "$LAUNCHER"
        # Two different mechanisms, because the two likely environments differ:
        #   XFCE (xfdesktop), KDE, LXQt -- the executable bit is what makes this
        #     a launcher rather than a text file;
        #   Cinnamon (nemo), MATE (caja) -- additionally keep their own
        #     "trusted" flag, and without it the icon appears as an untrusted
        #     file that will not start.
        # Both are applied; neither breaks the other.
        chmod +x "$LAUNCHER"
        if gio set "$LAUNCHER" metadata::trusted true 2>/dev/null; then
            echo "desktop icon: $LAUNCHER  (+trusted flag for nemo/caja)"
        else
            # No gio: fine on XFCE/KDE/LXQt. On Cinnamon the icon will need
            # "Allow Launching" from its right-click menu once.
            echo "desktop icon: $LAUNCHER"
            echo "   note: gio missing -- on Cinnamon, right-click the icon"
            echo "         once and choose Allow Launching."
        fi
    fi
fi

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
echo "then list the serial ports with:   $HERE/sinumerik ports"
