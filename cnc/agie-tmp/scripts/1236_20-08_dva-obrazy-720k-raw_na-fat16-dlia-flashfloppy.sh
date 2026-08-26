#!/bin/bash
# ДВА образи 720 КБ FAT12 БЕЗ MBR + флешка FAT16 під FlashFloppy.
#
# Ґотек більше не на банковій прошивці — 20.08 залито FlashFloppy 3.44.
# Вона працює не як SFR1M44: флешка це звичайна ФС, а образи лежать на ній
# ЗВИЧАЙНИМИ ФАЙЛАМИ, і прошивка сама роздає їх по слотах.
#
# Тому обгортки LBA 63 тут НЕМАЄ, на відміну від 1524_19-08. FlashFloppy
# визначає геометрію IMG за розміром файлу: у таблиці img_type[] в
# src/image/img.c запис 720k це 9 секторів / 2 сторони / 80 циліндрів,
# рівно 737280 Б. Наші 769536 туди не лягають, та й у справжньої дискети
# MBR не буває — boot-сектор тому лежить у нульовому секторі.
#
# Решта збірки тому — один в один з 1524_19-08:
#   ЧИСТИЙ 8.3, ЖОДНОГО ЗАПИСУ LFN У ТОМІ, запис 0 — мітка тому,
#   імена не 8.3 пропускаються зі списком у звіті.
#
# Два образи: з ~/agie-tmp/f0 та з ~/agie-tmp/f1. На флешці стануть
# F0.IMG і F1.IMG — у режимі native на семисегментнику слоти роздаються
# за порядком сортування, тож F0 це слот 000, F1 це слот 001.
#
# Флешка: FAT16 на весь пристрій, без таблиці розділів, кластер 32 КБ,
# мітка FLPPY0 — тобто та сама розмітка, що там і була.
#
# ЗАПУСК ДАБЛКЛІКОМ. Жодних аргументів. Пристрій обирається діалогом.
# Кожна root-операція має власний sudo, сам скрипт без sudo.

set -uo pipefail

IMGDIR="$HOME/agie-tmp/img"
STAMP="$(date +%Y%m%d-%H%M%S)"

SRC=(  "$HOME/agie-tmp/f0"                  "$HOME/agie-tmp/f1" )
IMG=(  "$IMGDIR/720k-raw-f0_$STAMP.img"     "$IMGDIR/720k-raw-f1_$STAMP.img" )
ONDISK=( "F0.IMG"                           "F1.IMG" )

KNOWN_ID=(
  "usb-LG_USB_DRIVE_1829439393DA0067-0:0"
  "usb-General_UDisk_1406131114310738698410-0:0"
)
KNOWN_NAME=( "LG 2 ГБ" "General UDisk 1,9 ГБ" )

pause() { echo; read -n 1 -s -r -p "— натисни будь-яку клавішу —"; echo; }
die()   { echo; echo "!! $*"; pause; exit 1; }

mkdir -p "$IMGDIR" || die "не створюється $IMGDIR"
for s in "${SRC[@]}"; do [ -d "$s" ] || die "немає $s"; done
command -v mkfs.fat >/dev/null 2>&1 || [ -x /sbin/mkfs.fat ] || die "немає mkfs.fat (пакет dosfstools)"

# ---------------------------------------------------------------- образи ----
for k in 0 1; do
echo
echo "================ образ $((k+1)) з 2: ${SRC[$k]} ================"
python3 - "${IMG[$k]}" "${SRC[$k]}" <<'PYEOF'
import os, struct, sys, time

img_path, src = sys.argv[1], sys.argv[2]

BPS, SPC, RES, NFAT, ROOTENT = 512, 2, 1, 2, 112
TOTSEC, MEDIA, SPF, SPT, HEADS, HIDDEN = 1440, 0xF9, 3, 9, 2, 0
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

# --- усе, що лежить у теці --------------------------------------------------
short_files, skipped = [], []
for n in sorted(os.listdir(src)):
    p = os.path.join(src, n)
    if not os.path.isfile(p):
        continue
    st = os.stat(p)
    ro = not (st.st_mode & 0o200)            # read-only у теці -> атрибут R
    rec = (n, p, st.st_size, st.st_mtime, ro)
    (short_files if is83(n) else skipped).append(rec)

if not short_files:
    raise SystemExit("у %s немає жодного файлу з іменем 8.3" % src)

need = sum(max(0, -(-sz // csz)) for _, _, sz, _, _ in short_files)
if need > nclusters:
    raise SystemExit("не влазить: треба %d кластерів, є %d" % (need, nclusters))
if 1 + len(short_files) > ROOTENT:
    raise SystemExit("не влазить у корінь: %d записів" % (1 + len(short_files)))

# --- дані + FAT -------------------------------------------------------------
fat = [0] * (nclusters + 2)
fat[0], fat[1] = 0xF00 | MEDIA, 0xFFF
image = bytearray(TOTSEC * BPS)
nextc = 2

def put(sz, path):
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
log_short = []

# 0) нульовий запис — мітка тому
root[0:32] = dirent(LABEL, 0x08, 0, 0, now)
i = 1

# 1) тільки файли 8.3 — жодного запису LFN у томі
for n, p, sz, mt, ro in short_files:
    n11 = pack83(n)
    cl = put(sz, p)
    root[i*32:(i+1)*32] = dirent(n11, 0x01 if ro else 0x20, cl, sz, mt)
    log_short.append((i, n, n11.decode(), cl, sz))
    i += 1

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

open(img_path, 'wb').write(image)

print("образ:      %s" % img_path)
print("розмір:     %d Б = том 720 КБ, %d секторів, БЕЗ MBR" % (len(image), TOTSEC))
print("media:      0x%02X   секторів/доріжку %d, головок %d, hidden %d" % (MEDIA, SPT, HEADS, HIDDEN))
print("кластерів:  %d (2..%d), по %d Б" % (nclusters, nclusters + 1, csz))
print()
print("узято з %s:" % src)
print("корінь, у порядку запису:")
print("            запис 0    мітка AGIE")
for i, n, n11, cl, sz in log_short:
    print("            запис %-3d  %-11s кластер %-4d %6d Б   <- %s" % (i, n11, cl, sz, n))
if skipped:
    print()
    print("ПРОПУЩЕНО, імʼя не 8.3 — на дискету НЕ потрапило:")
    for n, _, _, _, _ in skipped:
        print("            %s" % n)
PYEOF
[ $? -eq 0 ] || die "образ ${SRC[$k]} не зібрався"
[ -s "${IMG[$k]}" ] || die "образ ${IMG[$k]} порожній"
sz=$(stat -c%s "${IMG[$k]}")
[ "$sz" -eq 737280 ] || die "розмір ${IMG[$k]} = $sz Б, а FlashFloppy чекає рівно 737280"
done

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
echo "  0)   нічого не чіпати, лишити тільки образи"
echo "  r)   ввести шлях вручну"
echo
read -r -p "вибір: " ch

case "$ch" in
    0|"") echo; echo "образи готові, флешку не чіпав."; pause; exit 0;;
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
[ "$SZ" -le $((2*1024*1024*1024)) ] || die "$DEV більший за 2 ГіБ — FAT16 не покриє, відмовляюсь"

echo
lsblk -o NAME,SIZE,RM,MODEL,SERIAL,LABEL,MOUNTPOINT "$DEV"
echo
read -r -p "СТЕРТИ ВСЕ на $DEV, зробити FAT16 і покласти ${ONDISK[0]} + ${ONDISK[1]}? (yes) " ans
[ "$ans" = "yes" ] || die "скасовано"

for p in "$DEV"?*; do [ -b "$p" ] && sudo umount "$p" 2>/dev/null; done
sudo umount "$DEV" 2>/dev/null

echo "-> видаляю розділи"
sudo sfdisk --delete "$DEV" 2>/dev/null
sudo wipefs -a "$DEV"           || die "wipefs не спрацював"
sudo dd if=/dev/zero of="$DEV" bs=1M count=8 conv=fsync status=none || die "занулення не вдалось"

echo "-> FAT16 на весь пристрій, кластер 32 КБ, мітка FLPPY0"
sudo mkfs.fat -F 16 -s 64 -n FLPPY0 "$DEV" || die "mkfs.fat не спрацював"
sudo blockdev --flushbufs "$DEV"

MNT="$(mktemp -d)" || die "mktemp"
sudo mount -t vfat "$DEV" "$MNT" || die "не монтується $DEV"

echo "-> кладу образи"
for k in 0 1; do
    sudo cp "${IMG[$k]}" "$MNT/${ONDISK[$k]}" || { sudo umount "$MNT"; rmdir "$MNT"; die "не скопіювався ${ONDISK[$k]}"; }
done
sync
sudo blockdev --flushbufs "$DEV"

echo "-> звірка"
ok=1
for k in 0 1; do
    if sudo cmp -s "$MNT/${ONDISK[$k]}" "${IMG[$k]}"; then
        echo "   ${ONDISK[$k]}  збігається побайтово"
    else
        echo "   ${ONDISK[$k]}  НЕ ЗБІГАЄТЬСЯ"; ok=0
    fi
done
echo
sudo ls -l "$MNT"
echo
sudo umount "$MNT" && rmdir "$MNT"
[ "$ok" -eq 1 ] || die "ЗВІРКА НЕ ЗІЙШЛАСЬ"

echo
sudo dd if="$DEV" bs=512 count=1 status=none | od -A x -t x1z | head -3
echo
echo "готово: $DEV   ->  ${ONDISK[0]} слот 000,  ${ONDISK[1]} слот 001"
pause
