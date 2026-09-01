#!/bin/sh
# Крос-збірка трьох консольних утиліт під Windows з Debian.
# Пакет: g++-mingw-w64-i686. 32 біти навмисне — такий exe іде і на x86, і на x64.
# -static обовʼязковий: без нього exe тягне libstdc++/libgcc, а на цеховому
# компі їх немає і програма не запуститься.
#
# -D_WIN32_WINNT=0x0501 — стеля Windows XP. Без нього заголовки mingw-w64
# відкривають усе аж до Windows 10 (за замовчуванням 0x0A00) і мовчки дають
# узяти API, якого на XP немає. Тепер компілятор лається одразу.
# Сам лінкер і так ставить у PE subsystem 4.0, тобто нижче за XP-шні 5.1,
# тому XP такий exe вантажить; питання лише в тому, які функції звати.
#
# Кодових сторінок тут немає навмисно: усі рядки в програмах — чистий ASCII,
# однаковий і в 437, і в 866, і в 1251. Джерела в UTF-8 (українські
# коментарі), але всередині лапок не має бути жодного не-ASCII знака.
#
# Іконки: windres збирає *.rc (там один рядок — ICON з номером 1, саме його
# Explorer показує на файлі) в об'єктник, а він просто йде лінкерові разом
# з кодом. Самі .ico лежать у icon/ і малюються icon/mkicon.py.
set -e
CXX=i686-w64-mingw32-g++
RC=i686-w64-mingw32-windres
FLAGS="-O2 -s -static -D_WIN32_WINNT=0x0501 -Wall -Wextra -Wno-unused-function"
for p in newimg unpack pack; do
    $RC "$p.rc" "$p.res.o"
done
$CXX $FLAGS -o NEWIMG.EXE newimg.cpp newimg.res.o
$CXX $FLAGS -o UNPACK.EXE unpack.cpp unpack.res.o
$CXX $FLAGS -o PACK.EXE   pack.cpp   pack.res.o
rm -f newimg.res.o unpack.res.o pack.res.o
ls -l NEWIMG.EXE UNPACK.EXE PACK.EXE
