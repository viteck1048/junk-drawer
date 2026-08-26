#!/bin/bash
# GRUB під 4K-панель (3840x2160 на 13", ~333 PPI): своя тема, кегль 48 px.
# Самодостатній: усе, що ставиться, лежить поруч у theme/.
# Запуск:  sudo bash install-grub-theme.sh
# Змінні:  TITLE="Debian GNU/Linux"   заголовок над меню (типово — NAME з /etc/os-release)
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SRC="$HERE/theme"
DEF="/etc/default/grub"

[ "$(id -u)" -eq 0 ] || { echo "потрібен root"; exit 1; }
[ -f "$SRC/theme.txt" ] || { echo "ПОМИЛКА: нема $SRC/theme.txt"; exit 1; }

# --- 1. де в цього дистрибутива живе GRUB -------------------------------------
# Debian/Ubuntu/Arch — /boot/grub + update-grub
# Fedora/RHEL/openSUSE — /boot/grub2 + grub2-mkconfig
if [ -d /boot/grub2 ]; then GRUBDIR=/boot/grub2; else GRUBDIR=/boot/grub; fi
DST="$GRUBDIR/themes/hidpi"

if   command -v update-grub    >/dev/null; then MKCFG=(update-grub)
elif command -v grub2-mkconfig >/dev/null; then MKCFG=(grub2-mkconfig -o "$GRUBDIR/grub.cfg")
elif command -v grub-mkconfig  >/dev/null; then MKCFG=(grub-mkconfig  -o "$GRUBDIR/grub.cfg")
else echo "ПОМИЛКА: не знайшов ні update-grub, ні grub*-mkconfig"; exit 1; fi

echo "== GRUB: $GRUBDIR, перебудова: ${MKCFG[*]}"

# --- 2. місце на /boot --------------------------------------------------------
FREE=$(df -m "$GRUBDIR" | awk 'NR==2{print $4}')
echo "== вільно на $GRUBDIR: ${FREE} МБ"
if [ "$FREE" -lt 20 ]; then
    echo "ПОМИЛКА: замало місця. Спершу почисти старі ядра."
    exit 1
fi

# --- 3. тема ------------------------------------------------------------------
echo "== копіюю тему в $DST"
mkdir -p "$DST"
cp -f "$SRC"/*.pf2 "$SRC"/*.png "$SRC"/*.jpg "$SRC"/theme.txt "$DST"/

# заголовок під поточний дистрибутив
TITLE="${TITLE:-$( . /etc/os-release 2>/dev/null; echo "${NAME:-Linux}" )}"
echo "== заголовок: $TITLE"
python3 - "$DST/theme.txt" "$TITLE" <<'PY'
import re, sys
p, title = sys.argv[1], sys.argv[2]
s = open(p, encoding='utf-8').read()
s = re.sub(r'(id\s*=\s*"__title__".*?text\s*=\s*")[^"]*(")',
           lambda m: m.group(1) + title + m.group(2), s, flags=re.S)
open(p, 'w', encoding='utf-8').write(s)
PY

# --- 4. /etc/default/grub, з бекапом ------------------------------------------
BAK="$DEF.bak-$(date +%F-%H%M)"
echo "== бекап $DEF -> $BAK"
cp -f "$DEF" "$BAK"

# графічна тема не показується, якщо вивід загнаний у текстову консоль
# (Fedora типово ставить GRUB_TERMINAL_OUTPUT=console)
if grep -qE '^[[:space:]]*GRUB_TERMINAL(_OUTPUT)?=.*console' "$DEF"; then
    echo "== GRUB_TERMINAL*=console вимикає тему — коментую"
    sed -i -E 's/^([[:space:]]*GRUB_TERMINAL(_OUTPUT)?=.*console.*)$/#\1  # вимкнено install-grub-theme.sh/' "$DEF"
fi

sed -i -E '/^[[:space:]]*#?[[:space:]]*GRUB_(THEME|FONT)=/d' "$DEF"
cat >> "$DEF" <<CONF

# --- HiDPI: панель 3840x2160 на 13" (333 PPI), кегль 48 px ---
GRUB_THEME="$DST/theme.txt"
GRUB_FONT="$DST/dejavu48.pf2"
CONF

# GRUB_GFXMODE лишається як є: закоментований = нативні 3840x2160, саме те, що треба.

# --- 5. перебудова ------------------------------------------------------------
echo "== ${MKCFG[*]}"
"${MKCFG[@]}"

# --- 6. перевірка (нічого тут не має валити скрипт) ---------------------------
echo
echo "=========== ПЕРЕВІРКА ==========="
grep -nE '^GRUB_(THEME|FONT)=' "$DEF" || echo "!! GRUB_THEME/GRUB_FONT не дописались"
echo -n "loadfont у grub.cfg: "; grep -cE '^[[:space:]]*loadfont' "$GRUBDIR/grub.cfg" || true
grep -E '^[[:space:]]*(set )?theme=' "$GRUBDIR/grub.cfg" || echo "!! theme= НЕ встановлено"
echo -n "фон: "
if [ -f "$DST/background.jpg" ] && command -v identify >/dev/null; then
    identify -format '%wx%h\n' "$DST/background.jpg"
else
    ls -la "$DST/background.jpg" 2>/dev/null || echo "нема"
fi
grep -nE '^[[:space:]]*text[[:space:]]*=' "$DST/theme.txt" | head -1
df -h "$GRUBDIR" | tail -1
