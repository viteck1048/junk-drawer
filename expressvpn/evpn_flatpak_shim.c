/*
 * evpn_flatpak_shim.c
 *
 * LD_PRELOAD shim for expressvpn-daemon (Linux, kapps_net split-tunnel engine).
 *
 * PROBLEM
 *   kapps_net's ProcTracker matches a newly-exec'd process against the
 *   configured splitTunnelRules by comparing readlink("/proc/<pid>/exe")
 *   against a literal path string, then requires
 *   readlink("/proc/<pid>/ns/mnt") to equal the daemon's own mount
 *   namespace before it will add the pid to the bypass/vpn-only net_cls
 *   cgroup (kapps_net/src/linux/proc_tracker.cpp, upstream:
 *   pia-foss/desktop). Flatpak apps run inside a bubblewrap sandbox: their
 *   real exe path is meaningless from the host ("/app/lib/firefox/..."),
 *   and they always live in a private mount namespace, so they can never
 *   match today, by design.
 *
 * APPROACH
 *   Intercept the libc readlink() symbol inside expressvpn-daemon only
 *   (via LD_PRELOAD). For the two specific paths kapps_net queries, if the
 *   target pid is independently verified to be a genuine flatpak sandbox
 *   process for an app-id we've been told to handle, answer with:
 *     - /proc/<pid>/exe   -> a configured synthetic host path
 *     - /proc/<pid>/ns/mnt -> the daemon's own mount namespace id
 *   Every other readlink() call (any other path, or a flatpak pid we don't
 *   recognize) passes straight through to the real libc implementation.
 *   No vendor binary or library is modified; this is reversible by
 *   removing the LD_PRELOAD environment variable.
 *
 * VERIFICATION (see is_trusted_flatpak)
 *   - /proc/<pid>/root/.flatpak-info must exist and its
 *     [Application] name= must be in our configured app map.
 *   - Walking the pid's parent chain (via /proc/<pid>/status PPid) must
 *     reach a process whose *real* exe (read directly, bypassing our own
 *     hook) is exactly /usr/bin/bwrap, within a few hops.
 *   - Every pid visited in that walk must share the same real uid as the
 *     candidate process.
 *
 * HONEST LIMITATION
 *   This is a userspace heuristic, not a cryptographic guarantee. Any
 *   process already running as the same Linux user could in principle
 *   invoke bwrap itself and fabricate a matching .flatpak-info to spoof
 *   these checks. That is an acceptable residual risk for a single-user
 *   desktop: anything running with your own privileges already has much
 *   more direct ways to defeat a personal VPN kill switch (e.g. just
 *   disconnect it, or add its own iptables/ip rule). This shim only
 *   extends split tunneling to *your own* flatpak apps; it does not
 *   attempt to defend against a co-resident malicious process.
 *
 * CONFIG
 *   A whitespace-separated "<flatpak-app-id> <synthetic-host-path>" file,
 *   one per line, '#' comments allowed. Path defaults to
 *   /etc/evpn-flatpak-shim.conf, overridable via EVPN_FLATPAK_MAP.
 *
 *   Example line:
 *     org.mozilla.firefox /opt/flatpak-apps/org.mozilla.firefox
 *
 *   Add the synthetic path to ExpressVPN as an ordinary split-tunnel rule
 *   (GUI "Add application", or `expressvpnctl set split-app
 *   bypass:/opt/flatpak-apps/org.mozilla.firefox` /
 *   `vpn:/opt/flatpak-apps/org.mozilla.firefox`). If the GUI's file picker
 *   insists on an existing file, create an empty placeholder there and
 *   chmod +x it -- its contents are never read or executed, only the path
 *   string is ever compared.
 *
 * BUILD (on a real build host, not this machine)
 *   gcc -shared -fPIC -O2 -Wall -o evpn_flatpak_shim.so evpn_flatpak_shim.c -ldl
 *
 * DEPLOY
 *   Install the .so somewhere root-owned and readable (e.g.
 *   /usr/local/lib/evpn_flatpak_shim.so), write the config file, then
 *   preload it into the expressvpn daemon via a systemd drop-in:
 *
 *     systemctl edit expressvpn.service
 *     [Service]
 *     Environment=LD_PRELOAD=/usr/local/lib/evpn_flatpak_shim.so
 *
 *     systemctl daemon-reload
 *     systemctl restart expressvpn.service
 *
 *   All of the above (installing the .so, writing the config, editing the
 *   unit) requires root -- run it yourself, not via an assistant sudo call.
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

/* Cached once at startup: this process's (the daemon's) own mount
 * namespace id, exactly as a real readlink("/proc/self/ns/mnt", ...)
 * would report it. */
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

/* Parse /proc/<pid>/status for PPid and real uid. Uses the REAL readlink
 * where needed elsewhere; this function itself only touches fopen/fgets. */
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

/* Real (unspoofed) exe path for pid, via the genuine libc readlink. */
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

/* Parse /proc/<pid>/root/.flatpak-info for [Application] name=. Returns 0
 * and fills app_id if found. This file is placed by bubblewrap inside the
 * sandbox root; reading it through /proc/<pid>/root works from the host
 * because the daemon runs as root. */
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

/* Walk up to MAX_ANCESTOR_HOPS ancestors of pid. Returns 1 if a hop with
 * real exe == "/usr/bin/bwrap" is found before any uid mismatch, 0
 * otherwise. */
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

/* Full verification: is `pid` a genuine flatpak sandbox process for an
 * app-id we're configured to handle? Returns the synthetic path to report
 * for it, or NULL if not trusted / not configured. */
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

/* Match "/proc/<digits><suffix>" exactly, extracting the pid. */
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
