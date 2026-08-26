#!/usr/bin/env bash
# Update (or fresh-install) ExpressVPN from the newest
# expressvpn-linux-universal-*_release.run found nearby.
#
# Usage:
#   expressvpn-update.sh                 # search default dirs
#   expressvpn-update.sh /path/to/file.run   # use this package explicitly

set -euo pipefail

# Downloads dir comes from the XDG user-dirs spec, not from a hard-coded
# folder name -- the localized name ("Завантажене", "Descargas", ...) lives in
# ~/.config/user-dirs.dirs and differs per user locale.
xdg_download_dir() {
    local d=""
    if command -v xdg-user-dir >/dev/null 2>&1; then
        d=$(xdg-user-dir DOWNLOAD 2>/dev/null || true)
    fi
    # xdg-user-dir falls back to $HOME when the entry is unset; so does a
    # missing binary -- in both cases read the config file ourselves.
    if [[ -z "$d" || "$d" == "$HOME" ]]; then
        local f="${XDG_CONFIG_HOME:-$HOME/.config}/user-dirs.dirs"
        [[ -r "$f" ]] && d=$(
            . "$f" >/dev/null 2>&1
            printf '%s' "${XDG_DOWNLOAD_DIR:-}"
        )
    fi
    printf '%s' "$d"
}

SEARCH_DIRS=()
for d in "$(xdg_download_dir)" "$HOME/Downloads" "$HOME"; do
    [[ -n "$d" && "$d" != "$HOME/" ]] || continue
    [[ " ${SEARCH_DIRS[*]-} " == *" $d "* ]] || SEARCH_DIRS+=("$d")
done

WORKDIR="/tmp/expressvpn_update"

if [[ $# -ge 1 ]]; then
    PKG="$1"
else
    PKG=""
    for d in "${SEARCH_DIRS[@]}"; do
        [[ -d "$d" ]] || continue
        candidate=$(find "$d" -maxdepth 1 -name 'expressvpn-linux-universal*_release.run' -printf '%T@ %p\n' 2>/dev/null \
            | sort -rn | head -1 | cut -d' ' -f2-)
        if [[ -n "$candidate" ]]; then
            PKG="$candidate"
            break
        fi
    done
fi

if [[ -z "$PKG" || ! -f "$PKG" ]]; then
    echo "No expressvpn-linux-universal*_release.run found. Pass a path explicitly." >&2
    exit 1
fi

echo "Using package: $PKG"

OLD_VERSION=$(expressvpnctl --version 2>/dev/null || echo "not installed")
echo "Currently installed: $OLD_VERSION"

rm -rf "$WORKDIR"
chmod +x "$PKG"
"$PKG" --noexec --keep --target "$WORKDIR"

echo "Extracted OK. Applying update (sudo required)..."

sudo systemctl stop expressvpn 2>/dev/null || true

sudo groupadd -f expressvpn
sudo groupadd -f expressvpnhnsd

sudo mkdir -p /opt/expressvpn/{bin,etc,var,share,lib,plugins,qml}
sudo cp -r "$WORKDIR"/x64/expressvpnfiles/* /opt/expressvpn/
sudo cp "$WORKDIR"/x64/installfiles/*.sh /opt/expressvpn/bin/
sudo chmod +x /opt/expressvpn/bin/*.sh

sudo setcap 'cap_net_bind_service=+ep' /opt/expressvpn/bin/expressvpn-unbound

mkdir -p "$HOME/.local/share/applications" "$HOME/.local/share/icons/hicolor/256x256/apps"
cp "$WORKDIR"/x64/installfiles/expressvpn.desktop "$HOME/.local/share/applications/expressvpn.desktop"
cp "$WORKDIR"/x64/installfiles/app-icon.png "$HOME/.local/share/icons/hicolor/256x256/apps/expressvpn.png"

sudo mkdir -p /etc/NetworkManager/conf.d
echo -e "[keyfile]\nunmanaged-devices=interface-name:wgexpressvpn*" | sudo tee /etc/NetworkManager/conf.d/wgexpressvpn.conf > /dev/null

if [[ ! -e /usr/local/bin/expressvpnctl ]]; then
    sudo ln -s /opt/expressvpn/bin/expressvpnctl /usr/local/bin/expressvpnctl
fi

sudo cp "$WORKDIR"/x64/installfiles/expressvpn-service.service /etc/systemd/system/expressvpn.service

sudo systemctl daemon-reload
sudo systemctl enable expressvpn
sudo systemctl start expressvpn

sleep 2
echo "---"
echo "Was: $OLD_VERSION"
echo "Now: $(expressvpnctl --version 2>/dev/null || echo 'FAILED TO QUERY VERSION')"
sudo systemctl status expressvpn --no-pager | head -6
