# GRUB + SDDM під 4K (Dell, 3840x2160 на 13")

Два екрани до входу в сесію, зроблені під ~333 PPI: меню GRUB і грітер SDDM.
Обидва скрипти самодостатні — усе, що вони ставлять, лежить поруч. Ставити на
свіжу систему в такому порядку, з правами root:

    sudo bash install-grub-theme.sh
    sudo bash install-sddm-hidpi.sh

Зроблено 2026-08-26 на Debian 13.6, Plasma 6.3.6 (Wayland, масштаб сесії 2.2),
sddm 0.21.0. Скрипти писані так, щоб пережити зміну дистрибутива — за умови,
що це KDE + SDDM. Що саме вони під це підлаштовують — нижче.

## Що в теці

    install-grub-theme.sh   тема GRUB -> /boot/grub{,2}/themes/hidpi + /etc/default/grub
    install-sddm-hidpi.sh   масштаб грітера + фон -> /etc/sddm.conf.d + тема sddm
    theme/                  сама тема GRUB: theme.txt, три .pf2, рамки, фон 4K
    assets/nebula-noir.jpeg шпалера для грітера (та сама, що й у Plasma)
    pf2info.py              читає метрики .pf2 — покриття гліфів, ascent/descent

## install-grub-theme.sh

Копіює `theme/` у `themes/hidpi`, дописує в `/etc/default/grub`:

    GRUB_THEME="<grubdir>/themes/hidpi/theme.txt"
    GRUB_FONT="<grubdir>/themes/hidpi/dejavu48.pf2"

і перебудовує конфіг. Бекап `/etc/default/grub` кладе поруч із міткою часу.
`GRUB_GFXMODE` не чіпає: закоментований = нативні 3840x2160, саме те, що треба.

Що робить заради переносності між дистрибутивами:

* каталог — `/boot/grub2`, якщо він є (Fedora, RHEL, openSUSE), інакше `/boot/grub`;
* перебудова — `update-grub`, інакше `grub2-mkconfig -o`, інакше `grub-mkconfig -o`;
* якщо в `/etc/default/grub` стоїть `GRUB_TERMINAL*=console` (типово у Fedora) —
  коментує рядок, бо з текстовим виводом графічна тема просто не показується;
* заголовок над меню бере з `NAME` у `/etc/os-release` і вписує в theme.txt
  на місці. Хочеш інший — `sudo TITLE="Що завгодно" bash install-grub-theme.sh`.

Наприкінці друкує перевірку: чи дописались змінні, скільки `loadfont` у grub.cfg,
чи є `theme=`, розмір фону, поточний заголовок, місце на /boot.

### Тема

`theme.txt`: заголовок DejaVu Sans Mono Bold 72, меню 18%..82% по ширині,
`item_height 84`, кегль пунктів 48, псевдографічні рамки з `menu_*.png` /
`select_*.png` (тайли 16x16 / 8x8 / 6x4 px), прогрес-бар таймауту на 80% висоти,
підказка клавіш на 90%. Кегль 48 підібраний оком: читається і псевдографіка
стикується без розривів.

Шрифти зроблені штатним `grub-mkfont` з пакета DejaVu — команди відтворюють
наявні файли байт у байт (перевірено md5):

    grub-mkfont -s 40 -o theme/dejavu40.pf2  /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
    grub-mkfont -s 48 -o theme/dejavu48.pf2  /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf
    grub-mkfont -s 72 -o theme/dejavu72b.pf2 /usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf

У всіх трьох повне покриття Box Drawing (128/128) і Block Elements (32/32) —
через це рамки і стикуються. Метрики дивитись `python3 pf2info.py theme/dejavu48.pf2`.

Фон `theme/background.jpg` — 3840x2160, та сама туманність, що й шпалера Plasma,
але окремий 4K-файл (сама шпалера лише 1920x1080).

## install-sddm-hidpi.sh

Три речі:

1. кладе шпалеру в `/usr/local/share/wallpapers/nebula-noir.jpeg`. Не в домівку:
   юзер sddm не має прав читати `/home/viktor` (`drwx--x---`), тож ні `~/Картинки`,
   ні `~/.local/share/wallpapers` йому не видно;
2. пише `theme.conf.user` в каталог активної теми з `type=image` + шлях до фону.
   Пакетний `theme.conf` не чіпає — хай оновлення пакета його спокійно переписує.
   Механізм `.user` перевірений на sddm 0.21 експериментально, грітер підхопив;
3. пише `/etc/sddm.conf.d/zz-hidpi.conf` з
   `GreeterEnvironment=QT_SCREEN_SCALE_FACTORS=2.2`. Окремий файл `zz-*`, щоб
   KCM «Екран входу» не затер і щоб сортувалось після `kde_settings.conf`.

Активну тему шукає сам: `Current=` по `/etc/sddm.conf` і `/etc/sddm.conf.d/*.conf`,
виграє останній за сортуванням файл. Якщо тему ще ніхто не вибрав (свіжа система),
бере `debian-breeze`, інакше `breeze`, і дописує вибір у свій же `zz-hidpi.conf`.
Перевизначити: `sudo THEME=breeze SCALE=2.5 bash install-sddm-hidpi.sh`.

### Чому саме QT_SCREEN_SCALE_FACTORS

`kde_settings.conf` містить `[X11] ServerArguments=-dpi 240`, і Xorg справді
стартує з цим аргументом, але **Qt6 його ігнорує**. Заміряно у вкладеному X
(Xephyr 1920x1080 -dpi 240, грітер у `--test-mode`, маркер — рядок логу
`Adding view for ... QRect(...)`):

| змінна                          | логічний вид | масштаб |
|---------------------------------|--------------|---------|
| нічого (тільки -dpi 240)        | 1920x1080    | 1.0     |
| QT_ENABLE_HIGHDPI_SCALING=1     | 1920x1080    | 1.0     |
| QT_USE_PHYSICAL_DPI=1           | 1920x1080    | 1.0     |
| QT_SCALE_FACTOR=2.5             | 768x432      | 2.5     |
| QT_SCREEN_SCALE_FACTORS=2.5     | 768x432      | 2.5     |
| QT_FONT_DPI=240                 | 768x432      | 2.5     |

На повних 3840x2160 з `QT_SCREEN_SCALE_FACTORS=2.2` вид став 1745x982 — точно
як у сесії. Дробовий множник Qt6 не округлює.

### Прев'ю без ребуту

    Xephyr :7 -screen 3840x2160 -dpi 240 -ac &
    DISPLAY=:7 QT_QPA_PLATFORM=xcb QT_SCREEN_SCALE_FACTORS=2.2 \
      sddm-greeter-qt6 --test-mode --theme /usr/share/sddm/themes/debian-breeze
    import -window root -display :7 out.png

Без `QT_QPA_PLATFORM=xcb` грітер чіпляється до Wayland-сесії і DISPLAY ігнорує.

## Свідомо не зроблено

* **Блюр і затемнення фону грітера.** Екран блокування ганяє шпалеру через
  `WallpaperFader.qml` (FastBlur radius 50 + шейдер: contrast 0.65,
  saturation 1.6, intensity 0.6 для темної схеми), а `themes/breeze/Background.qml`
  цього не робить — тому на вході картинка різкіша і яскравіша. Рішення 2026-08-26:
  лишити як є.
* **Аватар.** Грітер шукає `/var/lib/AccountsService/icons/<user>`,
  `/usr/share/sddm/faces/<user>.face.icon`, `$HOME/.face.icon`; останній недоступний
  через права на домівку. Буде дефолтна спіраль дистрибутива.
* **Прибрати логотип унизу** — знімається `showlogo=hidden` у `theme.conf.user`,
  якщо колись захочеться.

Обидва екрани (блокування і вхід) зібрані з тих самих QML-компонентів
`org.kde.breeze.components` з plasma-workspace — Clock, SessionManagementScreen,
UserDelegate, ActionButton, Battery. Тому вирівнювання це не нова тема, а кілька
параметрів.
