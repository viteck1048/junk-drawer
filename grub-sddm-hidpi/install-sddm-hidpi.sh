#!/bin/bash
# SDDM на 4K-панелі: масштаб грітера + та сама шпалера, що й на екрані блокування.
# Самодостатній: картинка лежить поруч в assets/.
# Запуск:  sudo bash install-sddm-hidpi.sh
# Змінні:  SCALE=2.2         множник Qt для грітера (як масштаб сесії Plasma)
#          THEME=breeze      тема sddm, якщо не хочеш ту, що вже налаштована
set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
WALL_SRC="$HERE/assets/nebula-noir.jpeg"
WALL_DST="/usr/local/share/wallpapers/nebula-noir.jpeg"
SCALE="${SCALE:-2.2}"

[ "$(id -u)" -eq 0 ] || { echo "потрібен root"; exit 1; }
[ -f "$WALL_SRC" ] || { echo "ПОМИЛКА: нема $WALL_SRC"; exit 1; }

# --- 1. шпалера в системний шлях ----------------------------------------------
# юзер sddm не читає /home/viktor (drwx--x---), тому ні ~/Картинки,
# ні ~/.local/share/wallpapers йому не годяться — тільки щось під /usr.
echo "== шпалера -> $WALL_DST"
install -d /usr/local/share/wallpapers
install -m 644 "$WALL_SRC" "$WALL_DST"

# --- 2. яка тема зараз активна ------------------------------------------------
# Current= може бути в /etc/sddm.conf або в будь-якому /etc/sddm.conf.d/*.conf;
# виграє той файл, що сортується останнім.
detect_theme() {
    local f t=""
    for f in /etc/sddm.conf $(ls -1 /etc/sddm.conf.d/*.conf 2>/dev/null | sort); do
        [ -f "$f" ] || continue
        local v
        v=$(sed -nE 's/^[[:space:]]*Current[[:space:]]*=[[:space:]]*(.+[^[:space:]])[[:space:]]*$/\1/p' "$f" | tail -1)
        [ -n "$v" ] && t="$v"
    done
    echo "$t"
}

SET_CURRENT=0
THEME="${THEME:-$(detect_theme)}"
if [ -z "$THEME" ]; then
    # свіжа система, тему ще ніхто не вибрав
    for c in debian-breeze breeze; do
        [ -d "/usr/share/sddm/themes/$c" ] && { THEME="$c"; SET_CURRENT=1; break; }
    done
fi
THEME_DIR="/usr/share/sddm/themes/$THEME"
[ -n "$THEME" ] && [ -d "$THEME_DIR" ] || {
    echo "ПОМИЛКА: не знайшов теми sddm. Є такі:"; ls -1 /usr/share/sddm/themes/ 2>&1; exit 1; }
echo "== тема: $THEME ($THEME_DIR)$( [ "$SET_CURRENT" = 1 ] && echo ', виберу її явно' )"

# --- 3. фон теми --------------------------------------------------------------
# theme.conf.user — штатне перекриття sddm (перевірено на 0.21); пакетний
# theme.conf не чіпаємо, щоб оновлення пакета його спокійно перезаписало.
echo "== $THEME_DIR/theme.conf.user"
cat > "$THEME_DIR/theme.conf.user" <<EOF
[General]
type=image
background=$WALL_DST
EOF

# --- 4. масштаб грітера -------------------------------------------------------
# Xorg-івський -dpi 240 Qt6 ігнорує (заміряно в Xephyr). Діють лише
# QT_SCREEN_SCALE_FACTORS / QT_SCALE_FACTOR / QT_FONT_DPI. Дробові множники не округлюються.
# Окремий файл zz-*, щоб KCM «Екран входу» не затер і щоб сортувалось після kde_settings.conf.
echo "== /etc/sddm.conf.d/zz-hidpi.conf, масштаб $SCALE"
install -d /etc/sddm.conf.d
{
    echo "[General]"
    echo "GreeterEnvironment=QT_SCREEN_SCALE_FACTORS=$SCALE"
    if [ "$SET_CURRENT" = 1 ]; then
        echo
        echo "[Theme]"
        echo "Current=$THEME"
    fi
} > /etc/sddm.conf.d/zz-hidpi.conf

# --- 5. перевірка -------------------------------------------------------------
echo
echo "=========== ПЕРЕВІРКА ==========="
cat /etc/sddm.conf.d/zz-hidpi.conf
echo "---"
cat "$THEME_DIR/theme.conf.user"
echo "---"
ls -la "$WALL_DST"
echo
echo "застосувати: systemctl restart sddm   (це закриє поточну сесію)"
echo "або подивитись без ребуту:"
echo "  Xephyr :7 -screen 3840x2160 -dpi 240 -ac &"
echo "  DISPLAY=:7 QT_QPA_PLATFORM=xcb QT_SCREEN_SCALE_FACTORS=$SCALE \\"
echo "    sddm-greeter-qt6 --test-mode --theme $THEME_DIR"
