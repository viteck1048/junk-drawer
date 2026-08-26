#!/usr/bin/env bash
# Full ExpressVPN setup from scratch on Bazzite (rpm-ostree based).
# Installs required layered packages with immediate (live) activation,
# then delegates the actual ExpressVPN install/update to expressvpn-update.sh.
# No reboot is performed automatically — see the note printed at the end.

set -euo pipefail

REQUIRED_PKGS=(
    libnl3
    iptables
    psmisc
    libatomic
    brotli
    iproute
    ca-certificates
    libxkbcommon
    xterm
    libglvnd-opengl
    procps-ng
    glib2
)

echo "=== Installing required packages via rpm-ostree (--apply-live) ==="
sudo rpm-ostree install --apply-live --idempotent "${REQUIRED_PKGS[@]}"

echo
echo "=== Packages active, proceeding with ExpressVPN install/update ==="
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
"$SCRIPT_DIR/expressvpn-update.sh" "$@"

cat <<'EOF'

=== Done ===
Packages were applied live (no reboot needed for them to work right now).
NOTE: a reboot is still recommended when convenient, so the rpm-ostree
deployment is fully finalized and everything (kernel/systemd state, etc.)
is consistent on next boot:

    sudo systemctl reboot
EOF
