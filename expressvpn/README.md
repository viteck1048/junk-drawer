# ExpressVPN on Fedora Atomic (Bazzite, Bluefin, Aurora, Silverblue, Kinoite)

Two scripts that install and update the official ExpressVPN Linux client on an
image-based Fedora system, which the vendor's own installer does not support.

Unofficial, not affiliated with ExpressVPN. Tested on Bazzite in August 2026,
installing 14.1.1+13156 and upgrading it in place to 14.2.1+13658. x86_64 only —
the scripts use the `x64/` tree inside the package.

Dedicated to the public domain under CC0 1.0 (see `LICENSE` in this directory):
copy, modify or republish this content freely, with or without attribution.

## Why these scripts exist

ExpressVPN no longer publishes `.rpm` or `.deb` packages. The only thing a Linux
user can download today is `expressvpn-linux-universal-X.Y.Z.NNNNN_release.run` —
a self-extracting archive with the vendor's own installer inside.

That installer assumes a traditional distribution and writes the client into
`/usr`. On an rpm-ostree system `/usr` is read-only and belongs to the image, so
running the `.run` the way it expects to be run does not work — and there is no
packaged alternative left to fall back on. Hence these two scripts.

The payload itself is fine. Unpacked with `--noexec`, everything it contains can be
placed in directories that are *not* part of the image, so the client lives outside
the OS entirely:

| path | on Fedora Atomic | who writes it |
|---|---|---|
| `/opt/expressvpn` | symlink → `/var/opt/expressvpn` | the app itself |
| `/usr/local/bin/expressvpnctl` | symlink → `/var/usrlocal/bin/…` | CLI entry point |
| `/etc/systemd/system/expressvpn.service` | writable, machine-local | the daemon unit |
| `/etc/NetworkManager/conf.d/wgexpressvpn.conf` | writable, machine-local | NM opt-out for `wgexpressvpn*` |
| `~/.local/share/applications`, `~/.local/share/icons` | your home | launcher + icon |

Nothing lands in `/usr`, so the client survives `rpm-ostree upgrade` untouched and
updating it is a script run, not a deployment.

What *does* still need layering is the handful of runtime libraries the daemon and
GUI link against and the base image does not carry. That is the one reboot-worthy
part, and `expressvpn-full-install.sh` handles it with `--apply-live` so you can
finish the install in the same session.

## Quick start

1. Download `expressvpn-linux-universal-X.Y.Z.NNNNN_release.run` from your
   ExpressVPN account page into your usual downloads folder.
2. Run the full installer:

       ./expressvpn-full-install.sh

3. Sign in (`expressvpnctl --help` shows the activation and connect commands), then
   reboot when convenient so the rpm-ostree deployment is finalized.

## The two scripts

### `expressvpn-full-install.sh` — from scratch

Layers the runtime dependencies with

    sudo rpm-ostree install --apply-live --idempotent <packages>

`--apply-live` makes them usable immediately instead of only after a reboot;
`--idempotent` means re-running the script on an already-prepared system is a no-op
rather than an error. The packages:

    libnl3  iptables  psmisc  libatomic  brotli  iproute  ca-certificates
    libxkbcommon  xterm  libglvnd-opengl  procps-ng  glib2

That list is empirical — it is what the daemon and the GUI turned out to need on a
Bazzite base. On a different image some of them may already be present, which is
exactly why `--idempotent` is there.

It then hands over to `expressvpn-update.sh`, and prints a reminder that a reboot is
still worth doing so the deployment is fully finalized — not because anything is
broken without it.

### `expressvpn-update.sh` — install or update the client

Run with no arguments and it finds the newest matching `.run` itself:

    ./expressvpn-update.sh
    ./expressvpn-update.sh /path/to/expressvpn-linux-universal-14.2.1.13658_release.run

Where it looks, in order: the XDG download directory (`xdg-user-dir DOWNLOAD`, i.e.
whatever the folder is actually called in your locale — `Downloads`, `Descargas`,
`Загрузки`, `Завантажене`…), then `~/Downloads`, then `~`. If `xdg-user-dir` is not
installed it parses `~/.config/user-dirs.dirs` itself. Only the top level of each
directory is searched, and the newest file by mtime wins.

Then, in order:

1. records the currently installed version, so the run ends with a before/after line;
2. extracts the package with `--noexec --keep --target /tmp/expressvpn_update` —
   `--noexec` is the important flag: it unpacks without running the vendor's own
   installer, which is what keeps the install off `/usr`;
3. stops `expressvpn.service` before overwriting anything (harmless on a fresh install);
4. creates the `expressvpn` and `expressvpnhnsd` groups, copies the app tree into
   `/opt/expressvpn`, and grants `cap_net_bind_service` to `expressvpn-unbound` so the
   bundled resolver can bind its port without running as root;
5. installs the `.desktop` file and icon into *your* `~/.local/share` — per-user, so
   repeat this step for a second account on the same machine;
6. tells NetworkManager to leave `wgexpressvpn*` interfaces alone, so it does not
   fight the client over the WireGuard link;
7. symlinks `expressvpnctl` into `/usr/local/bin`, installs and enables the systemd
   unit, starts it, and prints the old version, the new version and the unit status.

Steps 3-7 need `sudo`, so the script will prompt.

## Updating later

Download the new `.run`, run `./expressvpn-update.sh`. It stops the daemon, overwrites
`/opt/expressvpn`, restarts, and prints:

    Was: 14.1.1+13156
    Now: 14.2.1+13658

No reboot, no rpm-ostree deployment, nothing to re-layer.

## Checking it works

    systemctl status expressvpn
    expressvpnctl --version
    expressvpnctl --help

Note that the daemon is comparatively memory-hungry (~675 MB resident in the sample
run recorded in `install_expressvpn.txt`) — expected, not a symptom.

## Uninstalling

The reverse of the install; nothing is registered with the package manager, so it is
all manual:

    sudo systemctl disable --now expressvpn
    sudo rm /etc/systemd/system/expressvpn.service
    sudo rm /etc/NetworkManager/conf.d/wgexpressvpn.conf
    sudo rm /usr/local/bin/expressvpnctl
    sudo rm -rf /opt/expressvpn /var/opt/expressvpn
    rm -f ~/.local/share/applications/expressvpn.desktop \
          ~/.local/share/icons/hicolor/256x256/apps/expressvpn.png
    sudo systemctl daemon-reload

The layered packages are ordinary Fedora libraries and can stay; drop them with
`sudo rpm-ostree uninstall <pkg>` if you want the deployment clean.

## Caveats

* The scripts do not verify a signature or checksum of the `.run`. Download it from
  your ExpressVPN account page and nowhere else.
* `rpm-ostree reset` (or a rebase that drops layering) removes the layered
  dependencies; the client in `/opt` stays, but re-run `expressvpn-full-install.sh`
  to put them back.
* `install_expressvpn.txt` next to these scripts is the same procedure written out as
  plain manual commands, kept for reference and for auditing what the scripts do.
