#!/bin/bash
# ЧИТАЄ флешку і каже, що там насправді. Нічого не пише і не стирає.
# sudo всередині (потрібен для сирого читання блокового пристрою).
# Запуск даблкліком, без аргументів.

set -uo pipefail

KNOWN_ID=(
  "usb-LG_USB_DRIVE_1829439393DA0067-0:0"
  "usb-General_UDisk_1406131114310738698410-0:0"
)
KNOWN_NAME=( "LG 2 ГБ" "General UDisk 1,9 ГБ" )
TMP="${TMPDIR:-/tmp}/agie-read-$$.bin"
trap 'rm -f "$TMP"' EXIT

pause() { echo; read -n 1 -s -r -p "— натисни будь-яку клавішу —"; echo; }
die()   { echo; echo "!! $*"; pause; exit 1; }

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

echo "================ що читаємо ================"
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

[ "${#DEVS[@]}" -gt 0 ] || die "знімних USB-накопичувачів не знайдено"
for i in "${!DEVS[@]}"; do echo "  $((i+1)))  ${DESCS[$i]}"; done
echo "  r)   ввести шлях вручну"
echo
if [ "${#DEVS[@]}" = 1 ]; then
    read -r -p "вибір [1]: " ch; ch="${ch:-1}"
else
    read -r -p "вибір: " ch
fi
case "$ch" in
    r|R) read -r -p "шлях: " DEV;;
    *)   if [[ "$ch" =~ ^[0-9]+$ ]] && [ "$ch" -ge 1 ] && [ "$ch" -le "${#DEVS[@]}" ]; then
             DEV="${DEVS[$((ch-1))]}"
         else die "невідомий вибір: $ch"; fi;;
esac
[ -b "$DEV" ] || die "не блоковий пристрій: $DEV"

echo
echo "-> читаю перші 4 МБ (тільки читання)"
sudo dd if="$DEV" of="$TMP" bs=1M count=4 status=none || die "не прочиталось"
sudo chown "$(id -u):$(id -g)" "$TMP" 2>/dev/null
echo

python3 - "$TMP" "$DEV" <<'PYEOF'
import struct, sys
d = open(sys.argv[1], 'rb').read()
dev = sys.argv[2]

print("=" * 64)
print("що лежить на %s" % dev)
print("=" * 64)

vols = []
if d[510:512] == b'\x55\xAA' and d[0] not in (0xEB, 0xE9):
    print("сектор 0: таблиця розділів MBR")
    for i in range(4):
        e = d[446 + i*16: 462 + i*16]
        if e[4] == 0:
            continue
        lba, n = struct.unpack_from('<II', e, 8)
        print("  розділ %d: тип 0x%02X, LBA %d, %d секторів%s"
              % (i+1, e[4], lba, n, ", активний" if e[0] == 0x80 else ""))
        vols.append(lba * 512)
elif d[0] in (0xEB, 0xE9) and d[510:512] == b'\x55\xAA':
    print("сектор 0: одразу завантажувальний сектор ФС — розділу немає (superfloppy)")
    vols.append(0)
else:
    print("сектор 0: ні MBR, ні BPB — не впізнаю")

for V in vols:
    b = d[V:V+512]
    if b[510:512] != b'\x55\xAA':
        print("\nтом на зсуві %d: немає підпису 55AA" % V); continue
    bps, spc = struct.unpack_from('<HB', b, 11)
    res, nf, rent, tot = struct.unpack_from('<HBHH', b, 14)
    media, spf, spt, heads = struct.unpack_from('<BHHH', b, 21)
    hid = struct.unpack_from('<I', b, 28)[0]
    print("\nтом на зсуві %d (сектор %d):" % (V, V // 512))
    print("  OEM %r, тип у BPB %r, мітка %r"
          % (b[3:11].decode('latin1'), b[54:62].decode('latin1'), b[43:54].decode('latin1')))
    print("  %d Б/сектор, %d сект/кластер, %d reserved, %d FAT по %d сект, корінь %d записів"
          % (bps, spc, res, nf, spf, rent))
    print("  media 0x%02X, %d сект/доріжку, %d головки, hidden %d, всього %d секторів"
          % (media, spt, heads, hid, tot))
    root = V + (res + nf*spf) * bps
    dstart = (res + nf*spf) + rent*32//bps
    ncl = (tot - dstart) // spc
    print("  кластерів %d, тип за числом кластерів: %s"
          % (ncl, "FAT12" if ncl < 4085 else ("FAT16" if ncl < 65525 else "FAT32")))
    print()
    print("  корінь (%d записів):" % rent)
    used = 0
    pend = []
    for i in range(rent):
        e = d[root + i*32: root + i*32 + 32]
        if e[0] == 0x00:
            continue
        used += 1
        a = e[11]
        if a == 0x0F:
            pend.append(i)
            continue
        nm = e[0:11].decode('latin1')
        cl = struct.unpack_from('<H', e, 26)[0]
        sz = struct.unpack_from('<I', e, 28)[0]
        if a & 0x08 and not a & 0x10:
            kind = "мітка тому"
        elif a & 0x10:
            kind = ">>> ПАПКА <<<"
        else:
            kind = "ФАЙЛ"
        lfn = (" (+%d записів LFN: %s)" % (len(pend), ",".join(map(str, pend)))) if pend else ""
        print("    запис %-3d  %-11s  attr=0x%02X  кластер %-4d  %6d Б   %s%s"
              % (i, nm, a, cl, sz, kind, lfn))
        pend = []
    print()
    print("  зайнято записів кореня: %d з %d; останній запис — %d" % (used, rent, rent - 1))
    # чи є десь у томі каталог узагалі
    ndir = 0
    for i in range(rent):
        e = d[root + i*32: root + i*32 + 32]
        if e[0] not in (0x00, 0xE5) and e[11] != 0x0F and (e[11] & 0x10):
            ndir += 1
    print("  ПАПОК у корені: %d" % ndir)
PYEOF
pause
