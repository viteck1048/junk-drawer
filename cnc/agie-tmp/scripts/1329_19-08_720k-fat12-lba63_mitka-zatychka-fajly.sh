#!/bin/bash
# 720 КБ FAT12 у розділі з сектора 63 (LBA 63), таблиця MBR, тип 0x01.
#
# Кладе те, що ЛЕЖИТЬ У ~/agie-tmp/f0/ — усе, як є, нічого не вигадуючи.
# Порядок кореня: запис 0 — мітка тому, далі імена, що НЕ влазять у 8.3
# (довгі, з пробілами, з малими літерами) разом зі своїми LFN, далі робочі
# файли 8.3, далі нулі.
#
# Причина перевороту: за специфікацією FAT байт 0x00 на початку запису
# означає "вільний і всі наступні теж", тому вінда законно спинялась на
# першому нулі й затички в хвості не бачила — і створювала свою папку.
#
# ЗАПУСК ДАБЛКЛІКОМ. Жодних аргументів. Пристрій обирається діалогом.
# Кожна root-операція має власний sudo, сам скрипт без sudo.

set -uo pipefail

SRC="$HOME/agie-tmp/f0"
IMGDIR="$HOME/agie-tmp/img"
IMG="$IMGDIR/720k-lba63-mitka0_$(date +%Y%m%d-%H%M%S).img"

KNOWN_ID=(
  "usb-LG_USB_DRIVE_1829439393DA0067-0:0"
  "usb-General_UDisk_1406131114310738698410-0:0"
)
KNOWN_NAME=( "LG 2 ГБ" "General UDisk 1,9 ГБ" )

pause() { echo; read -n 1 -s -r -p "— натисни будь-яку клавішу —"; echo; }
die()   { echo; echo "!! $*"; pause; exit 1; }

mkdir -p "$IMGDIR" || die "не створюється $IMGDIR"
[ -d "$SRC" ] || die "немає $SRC"

# ---------------------------------------------------------------- образ ----
python3 - "$IMG" "$SRC" <<'PYEOF'
import os, struct, sys, time

img_path, src = sys.argv[1], sys.argv[2]

BPS, SPC, RES, NFAT, ROOTENT = 512, 2, 1, 2, 112
TOTSEC, MEDIA, SPF, SPT, HEADS, HIDDEN = 1440, 0xF9, 3, 9, 2, 63
PART_LBA, PART_TYPE = 63, 0x01
MBR_H, MBR_S = 255, 63
LABEL = b'AGIE       '
OK83  = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")

root_sectors = ROOTENT * 32 // BPS
data_start   = RES + NFAT * SPF + root_sectors
data_sectors = TOTSEC - data_start
nclusters    = data_sectors // SPC
csz          = SPC * BPS

def is83(name):
    base, dot, ext = name.partition('.')
    if not base or len(base) > 8 or len(ext) > 3 or '.' in ext:
        return False
    return set(base + ext) <= OK83

def pack83(name):
    base, _, ext = name.partition('.')
    return (base.ljust(8) + ext.ljust(3)).encode('ascii')

def alias(name, taken):
    """коротке імʼя-псевдонім для довгого, як його робить DOS: SYSTEM~1"""
    base, _, ext = name.rpartition('.') if '.' in name else (name, '', '')
    clean = ''.join(c for c in base.upper() if c in OK83)[:6] or 'FILE'
    ext3  = ''.join(c for c in ext.upper() if c in OK83)[:3]
    n = 1
    while True:
        cand = ('%s~%d' % (clean[:8 - 1 - len(str(n))], n)).ljust(8) + ext3.ljust(3)
        if cand.encode() not in taken:
            return cand.encode('ascii')
        n += 1

def fatdt(ts):
    t = time.localtime(ts)
    return ((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2),
            ((t.tm_year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday)

def dirent(n11, attr, clus, size, ts):
    m, d = fatdt(ts)
    e = bytearray(32)
    e[0:11] = n11
    e[11] = attr
    struct.pack_into('<HH', e, 14, m, d)
    struct.pack_into('<H',  e, 18, d)
    struct.pack_into('<HH', e, 22, m, d)
    struct.pack_into('<H',  e, 26, clus)
    struct.pack_into('<I',  e, 28, size)
    return bytes(e)

def lfn_checksum(n11):
    s = 0
    for b in n11:
        s = (((s & 1) << 7) + (s >> 1) + b) & 0xFF
    return s

def lfn_entries(name, n11):
    """записи LFN у порядку, в якому вони лежать на диску (останній шматок першим)"""
    csum = lfn_checksum(n11)
    u = name.encode('utf-16-le')
    total = (len(name) + 12) // 13
    out = []
    for seq in range(total, 0, -1):
        part = u[(seq - 1) * 26: seq * 26]
        e = bytearray(b'\xFF' * 32)
        e[0]  = seq | (0x40 if seq == total else 0)
        e[11] = 0x0F
        e[12] = 0
        e[13] = csum
        struct.pack_into('<H', e, 26, 0)
        k = 0
        for a, b_ in ((1, 10), (14, 25), (28, 31)):
            for off in range(a, b_, 2):
                if k < len(part) // 2:
                    e[off:off+2] = part[k*2:k*2+2]
                elif k == len(part) // 2:
                    e[off:off+2] = b'\x00\x00'
                k += 1
        out.append(bytes(e))
    return out

# --- усе, що лежить у f0 ----------------------------------------------------
short_files, long_files = [], []
for n in sorted(os.listdir(src)):
    p = os.path.join(src, n)
    if not os.path.isfile(p):
        continue
    st = os.stat(p)
    ro = not (st.st_mode & 0o200)            # read-only у f0 -> атрибут R
    rec = (n, p, st.st_size, st.st_mtime, ro)
    (short_files if is83(n) else long_files).append(rec)

if not short_files and not long_files:
    raise SystemExit("у %s порожньо" % src)

need = sum(max(0, -(-sz // csz)) for _, _, sz, _, _ in short_files + long_files)
if need > nclusters:
    raise SystemExit("не влазить: треба %d кластерів, є %d" % (need, nclusters))

lfn_need = sum((len(n) + 12) // 13 + 1 for n, _, _, _, _ in long_files)
if 1 + len(short_files) + lfn_need > ROOTENT:
    raise SystemExit("не влазить у корінь: %d записів" % (1 + len(short_files) + lfn_need))

# --- дані + FAT -------------------------------------------------------------
fat = [0] * (nclusters + 2)
fat[0], fat[1] = 0xF00 | MEDIA, 0xFFF
image = bytearray(TOTSEC * BPS)
nextc = 2

def put(sz, path):
    """записати вміст, повернути номер першого кластера (0 для порожнього файлу)"""
    global nextc
    if sz == 0:
        return 0
    first = nextc
    data  = open(path, 'rb').read()
    nc    = -(-sz // csz)
    for i in range(nc):
        c   = first + i
        off = (data_start + (c - 2) * SPC) * BPS
        chunk = data[i*csz:(i+1)*csz]
        image[off:off+len(chunk)] = chunk
        fat[c] = 0xFFF if i == nc - 1 else c + 1
    nextc += nc
    return first

now  = time.time()
root = bytearray(ROOTENT * 32)

log_long, log_short = [], []
taken = set()

# 0) нульовий запис — мітка тому
label_at = 0
root[0:32] = dirent(LABEL, 0x08, 0, 0, now)
i = 1

# 1) далі довгі імена — затичка одразу за міткою
for n, p, sz, mt, ro in long_files:
    n11 = alias(n, taken)
    taken.add(n11)
    cl = put(sz, p)
    ents = lfn_entries(n, n11) + [dirent(n11, 0x01 if ro else 0x20, cl, sz, mt)]
    start = i
    for k, e in enumerate(ents):
        root[(i+k)*32:(i+k+1)*32] = e
    i += len(ents)
    log_long.append((start, i - 1, n, n11.decode(), cl, sz, len(ents) - 1))

# 2) далі робочі файли 8.3
for n, p, sz, mt, ro in short_files:
    n11 = pack83(n)
    taken.add(n11)
    cl = put(sz, p)
    root[i*32:(i+1)*32] = dirent(n11, 0x01 if ro else 0x20, cl, sz, mt)
    log_short.append((i, n, n11.decode(), cl, sz))
    i += 1

used_end = i

def pack_fat():
    b = bytearray(SPF * BPS)
    for k, v in enumerate(fat):
        o = k * 3 // 2
        if k % 2 == 0:
            b[o]   = v & 0xFF
            b[o+1] = (b[o+1] & 0xF0) | ((v >> 8) & 0x0F)
        else:
            b[o]   = (b[o] & 0x0F) | ((v << 4) & 0xF0)
            b[o+1] = (v >> 4) & 0xFF
    return bytes(b)

bs = bytearray(BPS)
bs[0:3]  = b'\xEB\x3C\x90'
bs[3:11] = b'MSDOS5.0'
struct.pack_into('<H', bs, 11, BPS)
bs[13] = SPC
struct.pack_into('<H', bs, 14, RES)
bs[16] = NFAT
struct.pack_into('<H', bs, 17, ROOTENT)
struct.pack_into('<H', bs, 19, TOTSEC)
bs[21] = MEDIA
struct.pack_into('<H', bs, 22, SPF)
struct.pack_into('<H', bs, 24, SPT)
struct.pack_into('<H', bs, 26, HEADS)
struct.pack_into('<I', bs, 28, HIDDEN)
struct.pack_into('<I', bs, 32, 0)
bs[36] = 0x00
bs[38] = 0x29
struct.pack_into('<I', bs, 39, int(now) & 0xFFFFFFFF)
bs[43:54] = LABEL
bs[54:62] = b'FAT12   '
bs[510:512] = b'\x55\xAA'

image[0:BPS] = bs
f = pack_fat()
image[RES*BPS:(RES+SPF)*BPS] = f
image[(RES+SPF)*BPS:(RES+2*SPF)*BPS] = f
ro_off = (RES + NFAT*SPF) * BPS
image[ro_off:ro_off+len(root)] = root

def chs(lba):
    c = lba // (MBR_H * MBR_S)
    h = (lba // MBR_S) % MBR_H
    sec = lba % MBR_S + 1
    if c > 1023:
        c, h, sec = 1023, MBR_H - 1, MBR_S
    return bytes([h, ((c >> 2) & 0xC0) | sec, c & 0xFF])

def chs_txt(lba):
    return "C%d/H%d/S%d" % (lba // (MBR_H * MBR_S), (lba // MBR_S) % MBR_H, lba % MBR_S + 1)

mbr = bytearray(BPS)
e = bytearray(16)
e[0] = 0x80
e[1:4] = chs(PART_LBA)
e[4] = PART_TYPE
e[5:8] = chs(PART_LBA + TOTSEC - 1)
struct.pack_into('<I', e, 8,  PART_LBA)
struct.pack_into('<I', e, 12, TOTSEC)
mbr[446:462] = e
mbr[510:512] = b'\x55\xAA'

disk = bytearray(PART_LBA * BPS) + image
disk[0:BPS] = mbr
open(img_path, 'wb').write(disk)

print("образ:      %s" % img_path)
print("розмір:     %d Б = MBR + 62 нулі + том 720 КБ" % len(disk))
print("розділ:     LBA %d, %d секторів, тип 0x%02X (FAT12), активний" % (PART_LBA, TOTSEC, PART_TYPE))
print("            CHS %s .. %s при геометрії %d головок / %d секторів"
      % (chs_txt(PART_LBA), chs_txt(PART_LBA + TOTSEC - 1), MBR_H, MBR_S))
print("media:      0x%02X   секторів/доріжку %d, головок %d, hidden %d" % (MEDIA, SPT, HEADS, HIDDEN))
print("кластерів:  %d (2..%d), по %d Б" % (nclusters, nclusters + 1, csz))
print()
print("узято з %s — усе, що там лежить:" % src)
print("корінь, у порядку запису:")
print("            запис %-3d  мітка AGIE" % label_at)
for a, b_, n, n11, cl, sz, nl in log_long:
    print("            записи %d..%d  %d LFN + %-11s кластер %-4d %6d Б   <- %s"
          % (a, b_, nl, n11, cl, sz, n))
    if cl == 0:
        print("                       розмір 0 -> кластер не виділявся, в даних нічого немає")
for i, n, n11, cl, sz in log_short:
    print("            запис %-3d  %-11s кластер %-4d %6d Б   <- %s" % (i, n11, cl, sz, n))
print("            записи %d..%d — нулі (0x00 = кінець каталогу)" % (used_end, ROOTENT - 1))
print()
print("мітка тому на нульовому записі, затичка одразу за нею — до нулів далеко,")
print("тому і вінда, і стійка доходять до неї при звичайному скануванні.")
PYEOF
[ $? -eq 0 ] || die "образ не зібрався"
[ -s "$IMG" ] || die "образ порожній"

# ------------------------------------------------------------- пристрої ----
echo
echo "================ пошук флешки ================"

DEVS=() DESCS=()
add() {
    local d="$1" i
    for i in "${DEVS[@]:-}"; do [ "$i" = "$d" ] && return; done
    local sz mdl ser szh
    sz=$(lsblk -dnbo SIZE "$d" 2>/dev/null) || return
    [ -n "$sz" ] && [ "$sz" -gt 0 ] 2>/dev/null || return
    mdl=$(lsblk -dno MODEL  "$d" 2>/dev/null | sed 's/ *$//')
    ser=$(lsblk -dno SERIAL "$d" 2>/dev/null | sed 's/ *$//')
    szh=$(LC_ALL=C awk -v s="$sz" 'BEGIN{printf "%.2f", s/1000000000}')
    DEVS+=("$d")
    DESCS+=("$(printf '%-9s %6s ГБ  %-20s %-24s %s' "$d" "$szh" "$mdl" "$ser" "$2")")
}

for i in "${!KNOWN_ID[@]}"; do
    lnk="/dev/disk/by-id/${KNOWN_ID[$i]}"
    [ -e "$lnk" ] && add "$(readlink -f "$lnk")" "<< ВІДОМА: ${KNOWN_NAME[$i]}"
done
while read -r n sz rm tran; do
    [ "$rm" = "1" ] || continue
    [ "$tran" = "usb" ] || [ -z "$tran" ] || continue
    [ "$sz" -gt 0 ] 2>/dev/null || continue
    [ "$sz" -le $((8*1000*1000*1000)) ] || continue
    add "/dev/$n" ""
done < <(lsblk -dnb -o NAME,SIZE,RM,TRAN 2>/dev/null)

if [ "${#DEVS[@]}" -eq 0 ]; then
    echo "знімних USB-накопичувачів не знайдено."
else
    for i in "${!DEVS[@]}"; do echo "  $((i+1)))  ${DESCS[$i]}"; done
fi
echo "  0)   нічого не чіпати, лишити тільки образ"
echo "  r)   ввести шлях вручну"
echo
read -r -p "вибір: " ch

case "$ch" in
    0|"") echo; echo "образ готовий, флешку не чіпав."; pause; exit 0;;
    r|R)  read -r -p "шлях (напр. /dev/sdc): " DEV;;
    *)    if [[ "$ch" =~ ^[0-9]+$ ]] && [ "$ch" -ge 1 ] && [ "$ch" -le "${#DEVS[@]}" ]; then
              DEV="${DEVS[$((ch-1))]}"
          else
              die "невідомий вибір: $ch"
          fi;;
esac

[ -b "$DEV" ] || die "не блоковий пристрій: $DEV"
case "$DEV" in
    /dev/sd[a-z]|/dev/mmcblk[0-9]) ;;
    *) die "треба цілий диск, а не розділ: $DEV";;
esac
[ "$(lsblk -dno RM "$DEV")" = "1" ] || die "$DEV не знімний — відмовляюсь"
SZ=$(lsblk -dnbo SIZE "$DEV")
[ "$SZ" -gt 0 ] 2>/dev/null || die "$DEV без носія"
[ "$SZ" -le $((8*1000*1000*1000)) ] || die "$DEV більший за 8 ГБ — відмовляюсь"

echo
lsblk -o NAME,SIZE,RM,MODEL,SERIAL,LABEL,MOUNTPOINT "$DEV"
echo
read -r -p "СТЕРТИ ВСЕ на $DEV і залити 720 КБ у розділ з LBA 63? (yes) " ans
[ "$ans" = "yes" ] || die "скасовано"

for p in "$DEV"?*; do [ -b "$p" ] && sudo umount "$p" 2>/dev/null; done
sudo umount "$DEV" 2>/dev/null

echo "-> видаляю розділи"
sudo sfdisk --delete "$DEV" 2>/dev/null
sudo wipefs -a "$DEV"           || die "wipefs не спрацював"
sudo dd if=/dev/zero of="$DEV" bs=1M count=8 conv=fsync status=none || die "занулення не вдалось"
echo "-> заливаю образ"
sudo dd if="$IMG" of="$DEV" bs=512 conv=fsync status=none || die "запис не вдався"
sudo blockdev --flushbufs "$DEV"
sudo partprobe "$DEV" 2>/dev/null

echo "-> звірка"
if sudo dd if="$DEV" bs=512 count=1503 status=none | cmp - "$IMG"; then
    echo "   MBR + том (1503 сектори) збігаються побайтово"
else
    die "ЗВІРКА НЕ ЗІЙШЛАСЬ"
fi
echo
sudo dd if="$DEV" bs=512 count=1 status=none | od -A x -t x1z | head -3
echo
echo "готово: $DEV"
pause
