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
# -fexec-charset=CP866 — ОБОВʼЯЗКОВИЙ, не косметика. Джерела лежать у UTF-8
# (щоб їх читали редактор і git), а в exe рядки мають лягти однобайтовим
# cp866 — тим самим, у якому працює консоль на цехових машинах і в якому
# зберігається коментар усередині образу. Без цього прапорця болгарські
# рядки поїдуть у програму як UTF-8 і на екрані буде сміття.
# -finput-charset=UTF-8 просто каже компіляторові, з чого перекодовувати.
set -e
CXX=i686-w64-mingw32-g++
FLAGS="-O2 -s -static -D_WIN32_WINNT=0x0501 -Wall -Wextra -Wno-unused-function"
FLAGS="$FLAGS -finput-charset=UTF-8 -fexec-charset=CP866"
$CXX $FLAGS -o NEWIMG.EXE newimg.cpp
$CXX $FLAGS -o UNPACK.EXE unpack.cpp
$CXX $FLAGS -o PACK.EXE   pack.cpp
ls -l NEWIMG.EXE UNPACK.EXE PACK.EXE
