#!/bin/bash
# ОБРАЗ-ДИСКЕТА В БАНК 0, як його чекає прошивка серії SFR1M44.
#
# Не розділ, не MBR, не файлова система на флешці — сирий образ дискети,
# записаний на флешку З НУЛЬОВОГО СЕКТОРА. Це банк 0. Наступні банки в цій
# серії лежать з кроком 0x180000, але крок підтверджений лише одним джерелом
# (огляд Gough Lui), а в коментарях там же — екземпляр з кроком 0x190000.
# Банк 0 надійний в обох варіантах, бо нуль однаковий, тому пишемо тільки його.
#
# Файли беруться з ~/agie-tmp/f1/. Тільки імена 8.3; що не влазить —
# пропускається зі списком у звіті. Жодних записів LFN, жодних затичок.
#
# Розмір образу: 1.44 МБ — рідний для серії SFR1M44 (сама назва це й каже).
# Щоб зібрати 720 КБ, змініть SIZE_KB нижче на 720.
#
# ЗАПУСК ДАБЛКЛІКОМ. Жодних аргументів. Пристрій обирається діалогом.
# Кожна root-операція має власний sudo, сам скрипт без sudo.

set -uo pipefail

SRC="$HOME/agie-tmp/f1"
IMGDIR="$HOME/agie-tmp/img"
SIZE_KB=1440                 # 1440 = 1.44 МБ | 720 = 720 КБ
WIPE_MB=16                   # скільки занулити перед записом (≈10 банків)
IMG="$IMGDIR/bank0-${SIZE_KB}k_$(date +%Y%m%d-%H%M%S).img"

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
python3 - "$IMG" "$SRC" "$SIZE_KB" <<'PYEOF'
import os, struct, sys, time

img_path, src, size_kb = sys.argv[1], sys.argv[2], int(sys.argv[3])

GEO = {
    1440: dict(SPC=1, ROOTENT=224, TOTSEC=2880, MEDIA=0xF0, SPF=9, SPT=18, HEADS=2),
     720: dict(SPC=2, ROOTENT=112, TOTSEC=1440, MEDIA=0xF9, SPF=3, SPT=9,  HEADS=2),
}
if size_kb not in GEO:
    raise SystemExit("SIZE_KB має бути 1440 або 720")
g = GEO[size_kb]
BPS, RES, NFAT = 512, 1, 2
SPC, ROOTENT, TOTSEC = g['SPC'], g['ROOTENT'], g['TOTSEC']
MEDIA, SPF, SPT, HEADS, HIDDEN = g['MEDIA'], g['SPF'], g['SPT'], g['HEADS'], 0
LABEL = b'AGIE       '
OK83  = set("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789$%'-_@~`!(){}^#&")

root_sectors = ROOTENT * 32 // BPS
data_start   = RES + NFAT * SPF + root_sectors
nclusters    = (TOTSEC - data_start) // SPC
csz          = SPC * BPS

def is83(name):
    base, _, ext = name.partition('.')
    if not base or len(base) > 8 or len(ext) > 3 or '.' in ext:
        return False
    return set(base + ext) <= OK83

def pack83(name):
    base, _, ext = name.partition('.')
    return (base.ljust(8) + ext.ljust(3)).encode('ascii')

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

good, skipped = [], []
for n in sorted(os.listdir(src)):
    p = os.path.join(src, n)
    if not os.path.isfile(p):
        continue
    st = os.stat(p)
    (good if is83(n) else skipped).append(
        (n, p, st.st_size, st.st_mtime, not (st.st_mode & 0o200)))
if not good:
    raise SystemExit("у %s немає жодного файлу з іменем 8.3" % src)

need = sum(max(1, -(-sz // csz)) for _, _, sz, _, _ in good)
if need > nclusters:
    raise SystemExit("не влазить: треба %d кластерів, є %d" % (need, nclusters))
if 1 + len(good) > ROOTENT:
    raise SystemExit("не влазить у корінь")

fat = [0] * (nclusters + 2)
fat[0], fat[1] = 0xF00 | MEDIA, 0xFFF
image = bytearray(TOTSEC * BPS)
nextc = 2
placed = []
for n, p, sz, mt, ro in good:
    if sz == 0:
        placed.append((n, 0, sz, mt, ro, 0)); continue
    first = nextc
    data  = open(p, 'rb').read()
    nc    = -(-sz // csz)
    for i in range(nc):
        c   = first + i
        off = (data_start + (c - 2) * SPC) * BPS
        chunk = data[i*csz:(i+1)*csz]
        image[off:off+len(chunk)] = chunk
        fat[c] = 0xFFF if i == nc - 1 else c + 1
    nextc += nc
    placed.append((n, first, sz, mt, ro, nc))

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

now  = time.time()
root = bytearray(ROOTENT * 32)
root[0:32] = dirent(LABEL, 0x08, 0, 0, now)
i = 1
for n, first, sz, mt, ro, nc in placed:
    root[i*32:(i+1)*32] = dirent(pack83(n), 0x01 if ro else 0x20, first, sz, mt)
    i += 1
used_end = i

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
open(img_path, 'wb').write(image)

print("образ:      %s" % img_path)
print("розмір:     %d Б — сирий образ дискети %s, БЕЗ MBR і розділу"
      % (len(image), "1.44 МБ" if size_kb == 1440 else "720 КБ"))
print("піде на флешку з нульового сектора = БАНК 0")
print("media:      0x%02X, %d Б/сектор, %d сект/кластер, корінь %d записів,"
      % (MEDIA, BPS, SPC, ROOTENT))
print("            %d FAT по %d сект, %d сект/доріжку, %d головки, hidden %d"
      % (NFAT, SPF, SPT, HEADS, HIDDEN))
print("кластерів:  %d (2..%d), по %d Б" % (nclusters, nclusters + 1, csz))
print()
print("узято з %s:" % src)
print("            запис 0    мітка AGIE")
for k, (n, first, sz, mt, ro, nc) in enumerate(placed, start=1):
    print("            запис %-3d  %-8s %-3s кластер %-5d %6d Б"
          % (k, pack83(n)[:8].decode().strip(), pack83(n)[8:].decode().strip(), first, sz))
print("            записи %d..%d — нулі" % (used_end, ROOTENT - 1))
if skipped:
    print()
    print("ПРОПУЩЕНО (імена не 8.3):")
    for n, _, sz, _, _ in skipped:
        print("            %-40s %d Б" % (n, sz))
print()
print("записів LFN у томі: 0")
PYEOF
[ $? -eq 0 ] || die "образ не зібрався"
[ -s "$IMG" ] || die "образ порожній"
IMGSZ=$(stat -c %s "$IMG")

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
read -r -p "СТЕРТИ ВСЕ на $DEV і залити образ у банк 0? (yes) " ans
[ "$ans" = "yes" ] || die "скасовано"

for p in "$DEV"?*; do [ -b "$p" ] && sudo umount "$p" 2>/dev/null; done
sudo umount "$DEV" 2>/dev/null

echo "-> видаляю розділи й таблицю"
sudo sfdisk --delete "$DEV" 2>/dev/null
sudo wipefs -a "$DEV" || die "wipefs не спрацював"
echo "-> занулюю перші $WIPE_MB МБ (щоб не лишилось старих банків)"
sudo dd if=/dev/zero of="$DEV" bs=1M count="$WIPE_MB" conv=fsync status=none || die "занулення не вдалось"
echo "-> пишу банк 0 з нульового сектора"
sudo dd if="$IMG" of="$DEV" bs=512 conv=fsync status=none || die "запис не вдався"
sudo blockdev --flushbufs "$DEV"

echo "-> звірка"
if sudo dd if="$DEV" bs=512 count=$((IMGSZ/512)) status=none | cmp - "$IMG"; then
    echo "   образ на флешці збігається побайтово ($IMGSZ Б)"
else
    die "ЗВІРКА НЕ ЗІЙШЛАСЬ"
fi
echo
sudo dd if="$DEV" bs=512 count=1 status=none | od -A x -t x1z | head -3
echo
echo "готово: $DEV — банк 0 записаний."
echo "На стійці: спершу d0 (ініціалізація буфера), далі читання."
pause
