#!/usr/bin/env bash
# Usage: sudo ./evpn-add-flatpak.sh [<flatpak-app-name-or-id> [bypass|vpnonly]]
# Example: sudo ./evpn-add-flatpak.sh firefox bypass
# With no arguments: interactive fzf picker for both the app and the mode.
#
# Resolves the given name against your installed flatpaks, builds+installs
# the split-tunnel shim on first run, registers the app with ExpressVPN,
# and restarts the daemon so it takes effect.
set -euo pipefail

SHIM_SO=/usr/local/lib/evpn_flatpak_shim.so
SHIM_CONF=/etc/evpn-flatpak-shim.conf
SYNTH_DIR=/opt/flatpak-apps
SYSTEMD_DROPIN_DIR=/etc/systemd/system/expressvpn.service.d
SYSTEMD_DROPIN="$SYSTEMD_DROPIN_DIR/override.conf"

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

REAL_USER="${SUDO_USER:-$(logname 2>/dev/null || echo root)}"

usage() {
    echo "Usage: $0 [<flatpak-app-name-or-id> [bypass|vpnonly]]" >&2
    echo "  Run with no arguments for an interactive picker (needs fzf)." >&2
    exit 1
}

if [ $# -eq 0 ]; then
    # --- interactive: pick the app, then the mode, both via fzf ---
    command -v fzf >/dev/null 2>&1 || {
        echo "No arguments given and fzf is not installed/in PATH." >&2
        echo "Either install fzf or pass the app name directly." >&2
        usage
    }

    APP_LIST="$(sudo -u "$REAL_USER" flatpak list --app --columns=application,name)"
    [ -n "$APP_LIST" ] || { echo "$REAL_USER has no flatpak apps installed." >&2; exit 1; }

    SELECTED="$(printf '%s\n' "$APP_LIST" | fzf --delimiter=$'\t' --with-nth=2,1 \
        --prompt='flatpak app> ' \
        --header='type to filter, arrows + Enter to pick, Esc to cancel')"
    [ -n "$SELECTED" ] || { echo "Nothing selected, aborting." >&2; exit 1; }

    APP_ID="$(printf '%s\n' "$SELECTED" | cut -f1)"
    APP_NAME="$(printf '%s\n' "$SELECTED" | cut -f2-)"
    echo "Selected: $APP_ID ($APP_NAME)"

    MODE_SELECTED="$(printf 'bypass\tBypass VPN (exclude from tunnel)\nvpnonly\tOnly VPN (require tunnel)\n' \
        | fzf --delimiter=$'\t' --with-nth=2 \
              --prompt='mode> ' --header="app: $APP_NAME")"
    [ -n "$MODE_SELECTED" ] || { echo "Nothing selected, aborting." >&2; exit 1; }
    MODE="$(printf '%s\n' "$MODE_SELECTED" | cut -f1)"
else
    # --- non-interactive: resolve the flatpak app id from what was typed ---
    QUERY="$1"
    MODE="${2:-bypass}"

    MATCHES="$(sudo -u "$REAL_USER" flatpak list --app --columns=application,name \
        | grep -i -F -- "$QUERY" || true)"

    if [ -z "$MATCHES" ]; then
        echo "No installed flatpak app matches '$QUERY'. Installed apps:" >&2
        sudo -u "$REAL_USER" flatpak list --app --columns=application,name >&2
        exit 1
    fi

    MATCH_COUNT="$(printf '%s\n' "$MATCHES" | wc -l)"
    if [ "$MATCH_COUNT" -gt 1 ]; then
        echo "Multiple matches for '$QUERY', be more specific:" >&2
        printf '%s\n' "$MATCHES" >&2
        exit 1
    fi

    APP_ID="$(printf '%s\n' "$MATCHES" | cut -f1)"
    APP_NAME="$(printf '%s\n' "$MATCHES" | cut -f2-)"
    echo "Resolved '$QUERY' -> $APP_ID ($APP_NAME)"
fi

case "$MODE" in
    bypass)  EVPN_MODE_PREFIX="bypass" ;;
    vpnonly) EVPN_MODE_PREFIX="vpn" ;;
    *) echo "Mode must be 'bypass' or 'vpnonly'" >&2; usage ;;
esac

SYNTH_PATH="$SYNTH_DIR/$APP_ID"
NEED_RESTART=0

# --- build+install the shim on first run ---
if [ ! -f "$SHIM_SO" ]; then
    echo "Building evpn_flatpak_shim.so..."
    TMPDIR="$(mktemp -d)"
    cat > "$TMPDIR/evpn_flatpak_shim.c" <<'SHIM_EOF'
/*
 * evpn_flatpak_shim.c -- LD_PRELOAD shim so ExpressVPN's Linux split
 * tunneling (kapps_net) can recognize flatpak (bubblewrap-sandboxed) apps.
 *
 * kapps_net matches a newly-exec'd process by comparing
 * readlink("/proc/<pid>/exe") against configured rule paths, then requires
 * readlink("/proc/<pid>/ns/mnt") to equal the daemon's own mount
 * namespace. Flatpak apps run in a private mount namespace with a
 * sandbox-internal exe path, so they can never match, by design
 * (upstream: pia-foss/desktop, kapps_net/src/linux/proc_tracker.cpp).
 *
 * This intercepts readlink() inside expressvpn-daemon only. For those two
 * specific paths, if the target pid is verified to be a genuine flatpak
 * sandbox process for an app-id listed in /etc/evpn-flatpak-shim.conf, it
 * answers with a configured synthetic host path (for /exe) and the
 * daemon's own namespace (for /ns/mnt). Every other call passes straight
 * through to the real libc readlink(). No ExpressVPN binary is modified;
 * removing the LD_PRELOAD env var fully reverts this.
 *
 * Verification: /proc/<pid>/root/.flatpak-info must exist with a
 * [Application] name= in our config, AND walking /proc/<pid>/status PPid
 * must reach a real /usr/bin/bwrap ancestor on the same uid within a few
 * hops. This is a userspace heuristic, not a hard security boundary: a
 * process already running as the same Linux user could in principle
 * fabricate these signals itself via its own bwrap invocation. Acceptable
 * on a single-user desktop -- anything running as you already has more
 * direct ways to defeat a personal VPN kill switch.
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dlfcn.h>
#include <limits.h>
#include <sys/types.h>

typedef ssize_t (*readlink_fn)(const char *, char *, size_t);
static readlink_fn real_readlink = NULL;

#define MAX_MAP_ENTRIES 64
#define MAX_ANCESTOR_HOPS 6

typedef struct {
    char app_id[128];
    char synth_path[PATH_MAX];
} MapEntry;

static MapEntry g_map[MAX_MAP_ENTRIES];
static int g_map_count = 0;

static char g_daemon_mnt_ns[64];
static size_t g_daemon_mnt_ns_len = 0;

static void ensure_real_readlink(void)
{
    if (!real_readlink)
        real_readlink = (readlink_fn)dlsym(RTLD_NEXT, "readlink");
}

__attribute__((constructor))
static void shim_init(void)
{
    ensure_real_readlink();

    ssize_t n = real_readlink("/proc/self/ns/mnt", g_daemon_mnt_ns,
                               sizeof(g_daemon_mnt_ns) - 1);
    if (n > 0)
        g_daemon_mnt_ns_len = (size_t)n;

    const char *cfg = getenv("EVPN_FLATPAK_MAP");
    if (!cfg)
        cfg = "/etc/evpn-flatpak-shim.conf";

    FILE *f = fopen(cfg, "r");
    if (!f)
        return;

    char line[512];
    while (fgets(line, sizeof(line), f) && g_map_count < MAX_MAP_ENTRIES) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\0')
            continue;

        char id[128];
        char path[PATH_MAX];
        if (sscanf(line, "%127s %4095s", id, path) == 2) {
            MapEntry *e = &g_map[g_map_count];
            strncpy(e->app_id, id, sizeof(e->app_id) - 1);
            e->app_id[sizeof(e->app_id) - 1] = '\0';
            strncpy(e->synth_path, path, sizeof(e->synth_path) - 1);
            e->synth_path[sizeof(e->synth_path) - 1] = '\0';
            g_map_count++;
        }
    }
    fclose(f);
}

static const char *lookup_synth_path(const char *app_id)
{
    for (int i = 0; i < g_map_count; i++) {
        if (strcmp(g_map[i].app_id, app_id) == 0)
            return g_map[i].synth_path;
    }
    return NULL;
}

static int get_ppid_and_uid(pid_t pid, pid_t *ppid, uid_t *uid)
{
    char statusPath[64];
    snprintf(statusPath, sizeof(statusPath), "/proc/%d/status", pid);

    FILE *f = fopen(statusPath, "r");
    if (!f)
        return -1;

    *ppid = -1;
    *uid = (uid_t)-1;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "PPid:", 5) == 0) {
            sscanf(line + 5, "%d", ppid);
        } else if (strncmp(line, "Uid:", 4) == 0) {
            unsigned u;
            sscanf(line + 4, "%u", &u);
            *uid = (uid_t)u;
        }
    }
    fclose(f);
    return 0;
}

static int get_real_exe(pid_t pid, char *buf, size_t buflen)
{
    char exePath[64];
    snprintf(exePath, sizeof(exePath), "/proc/%d/exe", pid);
    ssize_t n = real_readlink(exePath, buf, buflen - 1);
    if (n < 0)
        return -1;
    buf[n] = '\0';
    return 0;
}

static int get_flatpak_app_id(pid_t pid, char *app_id, size_t app_id_len)
{
    char infoPath[64];
    snprintf(infoPath, sizeof(infoPath), "/proc/%d/root/.flatpak-info", pid);

    FILE *f = fopen(infoPath, "r");
    if (!f)
        return -1;

    app_id[0] = '\0';
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl)
            *nl = '\0';
        if (strncmp(line, "name=", 5) == 0) {
            strncpy(app_id, line + 5, app_id_len - 1);
            app_id[app_id_len - 1] = '\0';
            break;
        }
    }
    fclose(f);
    return app_id[0] ? 0 : -1;
}

static int ancestor_chain_has_bwrap(pid_t pid, uid_t expected_uid)
{
    pid_t cur = pid;
    for (int hop = 0; hop < MAX_ANCESTOR_HOPS && cur > 1; hop++) {
        pid_t ppid;
        uid_t uid;
        if (get_ppid_and_uid(cur, &ppid, &uid) != 0)
            return 0;
        if (uid != expected_uid)
            return 0;

        char exe[PATH_MAX];
        if (get_real_exe(cur, exe, sizeof(exe)) == 0 &&
            strcmp(exe, "/usr/bin/bwrap") == 0)
            return 1;

        cur = ppid;
    }
    return 0;
}

static const char *is_trusted_flatpak(pid_t pid, uid_t pid_uid)
{
    char app_id[128];
    if (get_flatpak_app_id(pid, app_id, sizeof(app_id)) != 0)
        return NULL;

    const char *synth = lookup_synth_path(app_id);
    if (!synth)
        return NULL;

    if (!ancestor_chain_has_bwrap(pid, pid_uid))
        return NULL;

    return synth;
}

static int match_proc_suffix(const char *pathname, const char *suffix, pid_t *pid_out)
{
    if (strncmp(pathname, "/proc/", 6) != 0)
        return 0;

    const char *p = pathname + 6;
    const char *digitsStart = p;
    while (*p >= '0' && *p <= '9')
        p++;
    if (p == digitsStart)
        return 0;

    if (strcmp(p, suffix) != 0)
        return 0;

    *pid_out = (pid_t)strtol(digitsStart, NULL, 10);
    return 1;
}

ssize_t readlink(const char *pathname, char *buf, size_t bufsiz)
{
    ensure_real_readlink();

    pid_t pid;
    int is_exe = match_proc_suffix(pathname, "/exe", &pid);
    int is_ns = !is_exe && match_proc_suffix(pathname, "/ns/mnt", &pid);

    if (is_exe || is_ns) {
        pid_t ppid;
        uid_t uid;
        if (get_ppid_and_uid(pid, &ppid, &uid) == 0) {
            const char *synth = is_trusted_flatpak(pid, uid);
            if (synth) {
                const char *answer = is_exe ? synth : g_daemon_mnt_ns;
                size_t answer_len = is_exe ? strlen(synth) : g_daemon_mnt_ns_len;
                size_t len = answer_len < bufsiz ? answer_len : bufsiz;
                memcpy(buf, answer, len);
                return (ssize_t)len;
            }
        }
    }

    return real_readlink(pathname, buf, bufsiz);
}
SHIM_EOF
    gcc -shared -fPIC -O2 -Wall -o "$TMPDIR/evpn_flatpak_shim.so" "$TMPDIR/evpn_flatpak_shim.c" -ldl
    install -m 0644 "$TMPDIR/evpn_flatpak_shim.so" "$SHIM_SO"
    rm -rf "$TMPDIR"
    echo "Installed $SHIM_SO"
fi

# --- systemd drop-in so the daemon loads the shim ---
if [ ! -f "$SYSTEMD_DROPIN" ]; then
    mkdir -p "$SYSTEMD_DROPIN_DIR"
    cat > "$SYSTEMD_DROPIN" <<EOF
[Service]
Environment=LD_PRELOAD=$SHIM_SO
EOF
    systemctl daemon-reload
    NEED_RESTART=1
    echo "Installed systemd drop-in for LD_PRELOAD"
fi

# --- placeholder file the GUI/CLI can point at ---
mkdir -p "$SYNTH_DIR"
touch "$SYNTH_PATH"
chmod +x "$SYNTH_PATH"

# --- config entry ---
touch "$SHIM_CONF"
if ! grep -q "^$APP_ID " "$SHIM_CONF" 2>/dev/null; then
    echo "$APP_ID $SYNTH_PATH" >> "$SHIM_CONF"
    NEED_RESTART=1
    echo "Added $APP_ID to $SHIM_CONF"
fi

# --- register the split-tunnel rule with ExpressVPN itself ---
expressvpnctl set split-app "$EVPN_MODE_PREFIX:$SYNTH_PATH"
echo "Registered split-tunnel rule: $EVPN_MODE_PREFIX:$SYNTH_PATH"

if [ "$NEED_RESTART" -eq 1 ]; then
    echo "Restarting expressvpn.service to apply shim..."
    systemctl restart expressvpn.service
fi

echo
echo "Done. Launch $APP_NAME, then check:"
echo "  journalctl -u expressvpn.service -f"
echo "for: Adding <pid> to VPN exclusions/VPN Only for app: $SYNTH_PATH"
