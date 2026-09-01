// NEWIMG.EXE — створює поряд із собою образ дискети 720 КБ FAT12
//              з одним README.TXT усередині.
//
// Для стійки AGIE 100 (керування CNC 123) з ґотеком на FlashFloppy.
// Розкладка тому один в один та, яку стійка вже читає (образи F0/F1 від 20.08):
//   512 Б/сектор, 2 сектори/кластер, 1 reserved, 2 копії FAT, 112 записів кореня,
//   1440 секторів, media 0xF9, 3 сектори/FAT, 9 секторів/доріжку, 2 головки,
//   hidden 0, мітка тому AGIE нульовим записом кореня.
// Разом 737280 Б — рівно те, що FlashFloppy впізнає як 720k у img_type[].
//
// Корінь після створення: запис 0 — мітка AGIE, запис 1 — README.TXT,
// запис 2 — ABOUT_ME.TXT, якщо оператор дав коментар.
// Файли користувача лягатимуть далі.
//
// Імʼя добирається саме: номер 000..999 плюс те, що оператор допише сам,
// тобто 005.IMG або 005_proekt.IMG. Номер — це слот ґотека, і саме за ним
// стійка показує образ на семисегментнику, тому він завжди спереду.
// Вільний номер шукається ТІЛЬКИ по перших трьох байтах чужих імен:
// 005_proekt.IMG зайняв слот 005 так само, як 005.IMG.
//
// Питає рівно два: як назвати образ (можна нічого) і короткий коментар
// до нього (теж можна нічого). Коментар лягає першим рядком ABOUT_ME.TXT
// і закінчується CRLF; PACK.EXE та UNPACK.EXE показують його в своїх
// списках. Тільки друкований ASCII — і в імені, і в коментарі: кодову
// сторінку консолі кожна цехова машина піднімає свою (866, 437, 1251),
// і єдине, що в них усіх однакове, — латинка. Кодових сторінок програма
// не чіпає. Усе однобайтове, виклики WinAPI — з явним суфіксом A.
//
// Збірка:  i686-w64-mingw32-g++ -O2 -s -static -o NEWIMG.EXE newimg.cpp

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

// ---- геометрія тому -------------------------------------------------------
static const unsigned BPS      = 512;
static const unsigned SPC      = 2;
static const unsigned RES      = 1;
static const unsigned NFAT     = 2;
static const unsigned ROOTENT  = 112;
static const unsigned TOTSEC   = 1440;
static const unsigned char MEDIA = 0xF9;
static const unsigned SPF      = 3;
static const unsigned SPT      = 9;
static const unsigned HEADS    = 2;
static const unsigned HIDDEN   = 0;
static const char LABEL[12]    = "AGIE       ";   // рівно 11 значущих байтів

static const unsigned IMGSIZE    = TOTSEC * BPS;               // 737280
static const unsigned ROOTSEC    = ROOTENT * 32 / BPS;         // 7
static const unsigned DATASTART  = RES + NFAT * SPF + ROOTSEC; // 14
static const unsigned CLUSTERSZ  = SPC * BPS;                  // 1024

// ---- вміст README.TXT -----------------------------------------------------
// Англійською і з CRLF: його читатимуть на вінді та на стійці.
static const char README[] =
"AGIE floppy disk image\r\n"
"======================\r\n"
"\r\n"
"WHAT THIS FILE IS\r\n"
"This file is a raw image of a 3.5-inch 720 KB double-density floppy\r\n"
"disk. Geometry: 80 cylinders, 2 sides, 9 sectors per track, 512 bytes\r\n"
"per sector, 737280 bytes in total. The filesystem inside is FAT12 with\r\n"
"media descriptor 0xF9. This is the exact layout the AGIE control reads.\r\n"
"\r\n"
"HOW IT WAS CREATED\r\n"
"By NEWIMG.EXE, which sits next to this image. That program creates an\r\n"
"empty, correctly formatted image, asks for a name and a short comment,\r\n"
"and does nothing else. Every image starts with a three-digit number:\r\n"
"000, 001, 002 and so on, the first free one in the folder it is started\r\n"
"from. That number is the slot the machine shows on its display. After\r\n"
"the number you may add anything you like: 005.IMG and 005_MOULD.IMG\r\n"
"are the same slot 005, and NEWIMG.EXE looks only at the three digits\r\n"
"when it picks the next free number.\r\n"
"\r\n"
"WHAT IT IS FOR\r\n"
"It takes the place of a physical floppy disk. The USB stick is read by\r\n"
"a Gotek floppy emulator running FlashFloppy firmware, which presents\r\n"
"each image found on the stick to the machine as if it were a disk\r\n"
"inserted in the drive.\r\n"
"\r\n"
"WHAT TO DO WITH IT\r\n"
"1. Put your part programs into it. There are two ways:\r\n"
"   - PACK.EXE, next to this image. It lists the images and the files\r\n"
"     lying around them; you pick with the arrow keys and Enter.\r\n"
"     UNPACK.EXE does the opposite: it empties an image into a folder.\r\n"
"     Neither of them has to be installed.\r\n"
"   - Total Commander: put the cursor on the image and press Enter. It\r\n"
"     opens a disk image as if it were a folder and writes your changes\r\n"
"     straight back into the image.\r\n"
"2. Copy the finished image onto the USB stick used by the machine.\r\n"
"   You may rename it first; keep the .IMG extension and the three\r\n"
"   digits at the front - they are the slot number.\r\n"
"\r\n"
"THE COMMENT: ABOUT_ME.TXT\r\n"
"If you gave NEWIMG.EXE a comment, this image also holds ABOUT_ME.TXT.\r\n"
"Its first line is that comment, of any length up to 500 characters,\r\n"
"ending at the line break. PACK.EXE and UNPACK.EXE read it and show it\r\n"
"beside the image name in their lists, wrapped into lines of 50, so you\r\n"
"can tell the images apart without opening them. In those lists a long\r\n"
"comment shows its first line only, ending in three dots; the right\r\n"
"arrow key opens it in full, the left arrow folds it back.\r\n"
"To change the comment, edit the first line; nothing has to be padded.\r\n"
"Latin letters, digits and simple punctuation only - no Cyrillic. Every\r\n"
"machine in the shop brings its console up in a different code page, so\r\n"
"Latin is the only text that comes out right on all of them. Deleting\r\n"
"the file only leaves that column empty; nothing else breaks.\r\n"
"\r\n"
"FILE NAMING RULES INSIDE THE IMAGE\r\n"
"Short DOS names only: up to 8 characters, a dot, up to 3 more.\r\n"
"Capital letters A-Z, digits 0-9, and - _ . No spaces, no Cyrillic, no\r\n"
"long names. The control parses the directory itself and understands\r\n"
"nothing else.\r\n"
"\r\n"
"Do not put more than 109 files in one image: the root directory of\r\n"
"this format holds 112 entries, and up to three are already taken -\r\n"
"the volume label, this file, and ABOUT_ME.TXT if the image has a\r\n"
"comment.\r\n"
"\r\n"
"You may delete this README.TXT once you have added your own files.\r\n"
"\r\n"
"\r\n"
"AGIE disketen obraz (sashtoto na balgarski, s latinski bukvi)\r\n"
"=============================================================\r\n"
"\r\n"
"KAKVO E TOZI FAIL\r\n"
"Tova e surov obraz na 3.5-inchova disketa 720 KB, dvoina platnost.\r\n"
"Geometriya: 80 tsilindara, 2 strani, 9 sektora na pateka, 512 baita\r\n"
"na sektor, obshto 737280 baita. Failovata sistema vatre e FAT12 s\r\n"
"media deskriptor 0xF9. Tochno tazi naredba chete stoikata AGIE.\r\n"
"\r\n"
"KAK E NAPRAVEN\r\n"
"S NEWIMG.EXE, koyto e do tozi obraz. Tazi programa pravi prazen,\r\n"
"pravilno formatiran obraz, pita za ime i za kratak komentar i nishto\r\n"
"poveche. Vseki obraz zapochva s trytsifren nomer: 000, 001, 002 i taka\r\n"
"natatak - parviya svoboden v papkata, ot koyato e startirana. Tozi\r\n"
"nomer e slotat, koito mashinata pokazva na displeya si. Sled nomera\r\n"
"mozhesh da dopishesh kakvoto iskash: 005.IMG i 005_MOULD.IMG sa edin i\r\n"
"sasht slot 005, a NEWIMG.EXE gleda samo trite tsifri, kogato tarsi\r\n"
"sledvashtiya svoboden nomer.\r\n"
"\r\n"
"ZA KAKVO SLUZHI\r\n"
"Zamestva istinskata disketa. USB flashkata se chete ot Gotek emulator\r\n"
"s farmuer FlashFloppy, koyto podava vseki obraz ot flashkata na\r\n"
"mashinata taka, kakto ako beshe disketa v ustroistvoto.\r\n"
"\r\n"
"KAKVO DA PRAVISH S NEGO\r\n"
"1. Slozhi programite si vatre. Ima dva nachina:\r\n"
"   - PACK.EXE, koyto e do tozi obraz. Toi pokazva spisak s obrazite i\r\n"
"     s failovete okolo tyah; izbirash sas strelkite i Enter.\r\n"
"     UNPACK.EXE pravi obratnoto: izvazhda vsichko ot obraza v papka.\r\n"
"     Nito edna ot dvete ne se instalira.\r\n"
"   - Total Commander: slozhi kursora varhu obraza i natisni Enter.\r\n"
"     Toi otvarya obraza kato papka i zapisva promenite obratno v nego.\r\n"
"2. Kopirai gotoviya obraz na USB flashkata na mashinata.\r\n"
"   Mozhesh da go preimenuvash predi tova; zapazi razshirenieto .IMG i\r\n"
"   trite tsifri otpred - te sa nomerat na slota.\r\n"
"\r\n"
"KOMENTARAT: ABOUT_ME.TXT\r\n"
"Ako si dal komentar na NEWIMG.EXE, v tozi obraz ima i ABOUT_ME.TXT.\r\n"
"Parviyat mu red e tozi komentar - s dalzhina do 500 znaka, do kraya na\r\n"
"reda. PACK.EXE i UNPACK.EXE go chetat i go pokazvat do imeto na obraza\r\n"
"v spisatsite si, razdelen na redove po 50 znaka, za da razlichavash\r\n"
"obrazite bez da gi otvaryash. Dalag komentar se pokazva samo s parviya\r\n"
"si red, koito zavarshva s tri tochki; strelka nadyasno go otvarya\r\n"
"tsyal, strelka nalyavo go zatvarya obratno.\r\n"
"Za da smenish komentara, redaktirai parviya red - nishto ne se dopalva\r\n"
"s intervali. Samo latinitsa, tsifri i prosta punktuatsiya - bez\r\n"
"kirilitsa. Vsyaka mashina v tseha vdiga konzolata si v razlichna\r\n"
"kodova stranitsa, zatova samo latinskiyat tekst izliza pravilno\r\n"
"navsyakade. Ako iztriesh faila, kolonata prosto ostava prazna -\r\n"
"nishto drugo ne se chupi.\r\n"
"\r\n"
"PRAVILA ZA IMENATA NA FAILOVETE VATRE V OBRAZA\r\n"
"Samo kratki DOS imena: do 8 znaka, tochka, oshte do 3 znaka.\r\n"
"Glavni bukvi A-Z, tsifri 0-9 i - _ . Bez interval, bez kirilitsa, bez\r\n"
"dalgi imena. Stoikata sama chete kataloga i ne razbira nishto drugo.\r\n"
"\r\n"
"Ne slagai poveche ot 109 faila v edin obraz: korenat na tozi format\r\n"
"pobira 112 zapisa, a do tri sa veche zaeti - etiketat na toma, tozi\r\n"
"fail i ABOUT_ME.TXT, ako obrazat ima komentar.\r\n"
"\r\n"
"Mozhesh da iztriesh tozi README.TXT, sled kato slozhish svoite failove.\r\n";

// ---- пояснення в ABOUT_ME.TXT ---------------------------------------------
// Йде одразу за 50 байтами коментаря і CRLF.
static const char ABOUT_BODY[] =
"Do not delete this file.\r\n"
"The first line above is the comment for this image: everything up to\r\n"
"the line break, up to 500 characters, Latin letters only - no\r\n"
"Cyrillic, because the machines here do not agree on a code page.\r\n"
"PACK.EXE and UNPACK.EXE read it and show it next to the image in their\r\n"
"lists, wrapped into lines of 50 characters. Nothing is padded: just\r\n"
"edit the first line in any text editor and keep the rest of the file\r\n"
"as it is. Without this file the comment column simply stays empty;\r\n"
"nothing else breaks.\r\n"
"\r\n"
"Ne iztrivai tozi fail.\r\n"
"Parviyat red gore e komentarat na tozi obraz: vsichko do kraya na\r\n"
"reda, do 500 znaka, samo na latinitsa - bez kirilitsa, zashtoto\r\n"
"mashinite tuk ne sa na edna kodova stranitsa. PACK.EXE i UNPACK.EXE go\r\n"
"pokazvat v spisaka do imeto na obraza, razdelen na redove po 50 znaka.\r\n"
"Nishto ne se dopalva s intervali - prosto redaktirai parviya red s\r\n"
"koyto i da e tekstov redaktor. Bez tozi fail kolonata s komentara\r\n"
"ostava prazna - nishto drugo.\r\n";

// Коментар: без доповнення пробілами, кінець — CRLF, довжина будь-яка до
// COMMENT_MAX. Стеля потрібна не операторові, а самому файлу: перший рядок
// має влізти в перший кластер образу, звідки його читають PACK і UNPACK.
#define COMMENT_MAX 500

// Ім'я образу після номера. Стеля — та сама, що в довгих іменах Windows;
// у вужче місце вперся б хіба той, хто пише повість замість назви.
#define NAME_MAX 255

// ---- вивід ----------------------------------------------------------------
static void say(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE || !*s)
        return;
    if (WriteConsoleA(h, s, (DWORD)strlen(s), &n, NULL))
        return;
    // вивід перенаправлено у файл — консольного API там немає, ті самі байти
    WriteFile(h, s, (DWORD)strlen(s), &n, NULL);
}

static void sayf(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, 1023, fmt, ap);
    buf[1023] = 0;
    va_end(ap);
    say(buf);
}

// один рядок з клавіатури; 0 = кінець вводу
static int read_line(char *buf, size_t cap)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode, n = 0;
    size_t i;

    buf[0] = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (GetConsoleMode(h, &mode)) {
        if (!ReadConsoleA(h, buf, (DWORD)(cap - 1), &n, NULL) || n == 0)
            return 0;
        buf[n] = 0;
    } else {
        // ввід перенаправлено: ті самі байти, без перекодування
        DWORD got = 0;
        i = 0;
        while (i + 1 < cap) {
            char c;
            if (!ReadFile(h, &c, 1, &got, NULL) || got == 0)
                break;
            if (c == '\n')
                break;
            buf[i++] = c;
        }
        if (i == 0 && got == 0)
            return 0;
        buf[i] = 0;
    }
    i = strlen(buf);
    while (i > 0 && (buf[i - 1] == '\r' || buf[i - 1] == '\n' || buf[i - 1] == ' '))
        buf[--i] = 0;
    return 1;
}

// пауза тільки тут: без неї вікно зникне раніше, ніж помилку прочитають
static void fail(void)
{
    say("\r\n-- press any key --");
    _getch();
    ExitProcess(1);
}

// копія рядка з обрізанням по буферу і завжди із завершальним нулем
static void copy_str(char *dst, size_t cap, const char *src)
{
    size_t n = strlen(src);
    if (cap == 0)
        return;
    if (n > cap - 1)
        n = cap - 1;
    memcpy(dst, src, n);
    dst[n] = 0;
}

// ---- дрібниці FAT ---------------------------------------------------------
static void put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
}

static void put32(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)(v & 0xFF);
    p[1] = (unsigned char)((v >> 8) & 0xFF);
    p[2] = (unsigned char)((v >> 16) & 0xFF);
    p[3] = (unsigned char)((v >> 24) & 0xFF);
}

// запис номер k у таблиці FAT12 — 12 біт, пів-байта зсуву через один
static void fat_set(unsigned char *fat, unsigned k, unsigned v)
{
    unsigned o = k * 3 / 2;
    if (k & 1) {
        fat[o]     = (unsigned char)((fat[o] & 0x0F) | ((v << 4) & 0xF0));
        fat[o + 1] = (unsigned char)((v >> 4) & 0xFF);
    } else {
        fat[o]     = (unsigned char)(v & 0xFF);
        fat[o + 1] = (unsigned char)((fat[o + 1] & 0xF0) | ((v >> 8) & 0x0F));
    }
}

// час і дата у форматі запису каталогу FAT
static void fat_now(unsigned *ftime, unsigned *fdate)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    *ftime = (st.wHour << 11) | (st.wMinute << 5) | (st.wSecond / 2);
    *fdate = ((st.wYear - 1980) << 9) | (st.wMonth << 5) | st.wDay;
}

// один запис каталогу: імʼя 8.3 рівно 11 байтів, без записів LFN
static void dirent(unsigned char *e, const char *n11, unsigned char attr,
                   unsigned clus, unsigned size, unsigned ftime, unsigned fdate)
{
    memcpy(e, n11, 11);
    e[11] = attr;
    put16(e + 14, ftime);
    put16(e + 16, fdate);
    put16(e + 18, fdate);
    put16(e + 22, ftime);
    put16(e + 24, fdate);
    put16(e + 26, clus);
    put32(e + 28, size);
}

// покласти байти в наступні вільні кластери підряд; повертає перший кластер
static unsigned put_file(unsigned char *img, unsigned char *fat, unsigned *next,
                         const char *data, unsigned size)
{
    unsigned first = *next;
    unsigned n = (size + CLUSTERSZ - 1) / CLUSTERSZ;
    unsigned i;
    for (i = 0; i < n; i++) {
        unsigned c = first + i;
        unsigned chunk = (size - i * CLUSTERSZ < CLUSTERSZ)
                       ? (size - i * CLUSTERSZ) : CLUSTERSZ;
        memcpy(img + (DATASTART + (c - 2) * SPC) * BPS, data + i * CLUSTERSZ, chunk);
        fat_set(fat, c, (i == n - 1) ? 0xFFF : (c + 1));
    }
    *next = first + n;
    return first;
}

// Ім'я образу після номера: тільки друкований ASCII і нічого з того, що
// Windows не пускає в імена файлів. Порожній ввід — лишається сам номер.
// 0 = імені не буде; інакше out[] — те, що допишеться після XXX_.
static int ask_name(char *out, size_t cap, size_t room)
{
    char in[1024];
    for (;;) {
        size_t len, i;
        int bad = 0;
        const char *dot;

        say("\r\nName for this image, after the number. ASCII only.\r\n"
            "Press Enter on an empty line to leave just the number.\r\n"
            "Ime na obraza, sled nomera. Samo latinski bukvi.\r\n"
            "Prazen red = samo nomerat.\r\n> ");
        if (!read_line(in, 1024) || !in[0])
            return 0;

        // ".IMG" програма допише сама; хай оператор не сперечається з нею
        len = strlen(in);
        dot = (len > 4) ? in + len - 4 : NULL;
        if (dot && (_stricmp(dot, ".IMG") == 0)) {
            in[len - 4] = 0;
            len -= 4;
        }
        while (len > 0 && (in[len - 1] == ' ' || in[len - 1] == '.'))
            in[--len] = 0;
        if (len == 0)
            return 0;

        for (i = 0; i < len; i++)
            if ((unsigned char)in[i] < 32 || (unsigned char)in[i] > 126
                || strchr("\\/:*?\"<>|", in[i]))
                bad = 1;
        if (bad) {
            say("!! Latin letters, digits and simple punctuation only;\r\n"
                "!! none of these:  \\ / : * ? \" < > |\r\n"
                "!! Samo latinitsa, tsifri i prosta punktuatsiya;\r\n"
                "!! bez tezi znatsi:  \\ / : * ? \" < > |\r\n");
            continue;
        }
        if (len > NAME_MAX || len > room) {
            size_t lim = (NAME_MAX < room) ? NAME_MAX : room;
            sayf("!! too long: %u characters, %u fit here\r\n"
                 "!! tvarde dalgo: %u znaka, tuk se pobirat %u\r\n",
                 (unsigned)len, (unsigned)lim, (unsigned)len, (unsigned)lim);
            continue;
        }
        copy_str(out, cap, in);
        return 1;
    }
}

// Коментар з клавіатури. Довжина будь-яка до COMMENT_MAX, знаки — тільки
// друкований ASCII. 0 = оператор пропустив; інакше out[] — сам коментар,
// без CRLF.
static int ask_comment(char *out, size_t cap)
{
    char in[1024];
    for (;;) {
        size_t len, i;
        int bad = 0;

        say("\r\nShort comment for this image, Latin letters only.\r\n"
            "Press Enter on an empty line if you do not want one.\r\n"
            "Kratak komentar za tozi obraz, samo latinitsa.\r\n"
            "Prazen red = bez komentar.\r\n> ");
        if (!read_line(in, 1024) || !in[0])
            return 0;

        /* char знаковий, тому байт понад 127 без приведення виглядав би
           відʼємним і проскочив би повз перевірку */
        len = strlen(in);
        for (i = 0; i < len; i++)
            if ((unsigned char)in[i] < 32 || (unsigned char)in[i] > 126)
                bad = 1;
        if (bad) {
            say("!! Latin letters, digits and simple punctuation only.\r\n"
                "!! Samo latinitsa, tsifri i prosta punktuatsiya.\r\n");
            continue;
        }
        if (len > COMMENT_MAX) {
            sayf("!! too long: %u characters, the limit is %u\r\n"
                 "!! tvarde dalgo: %u znaka, maksimum %u\r\n",
                 (unsigned)len, (unsigned)COMMENT_MAX,
                 (unsigned)len, (unsigned)COMMENT_MAX);
            continue;
        }
        copy_str(out, cap, in);
        return 1;
    }
}

// Перший вільний номер образу. Дивимось ТІЛЬКИ на перші три байти імені:
// 005.IMG і 005_proekt.IMG — той самий слот 005, бо для стійки значить саме
// номер, а не те, що оператор дописав після нього. -1 = вільних немає.
static int next_number(const char *dir)
{
    static char taken[1000];
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int i;

    memset(taken, 0, sizeof(taken));
    _snprintf(pat, MAX_PATH - 1, "%s\\*.img", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            const char *f = fd.cFileName;
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            if (f[0] < '0' || f[0] > '9') continue;
            if (f[1] < '0' || f[1] > '9') continue;
            if (f[2] < '0' || f[2] > '9') continue;
            taken[(f[0] - '0') * 100 + (f[1] - '0') * 10 + (f[2] - '0')] = 1;
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    for (i = 0; i < 1000; i++)
        if (!taken[i])
            return i;
    return -1;
}

int main(void)
{
    // ---- тека, де лежить сам exe (а не поточна тека процесу) --------------
    char exe[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        say("!! cannot work out my own path\r\n");
        fail();
    }
    char *slash = strrchr(exe, '\\');
    if (!slash) {
        say("!! my own path has no separator\r\n");
        fail();
    }
    *slash = 0;                       // тепер exe == тека
    const char *dir = exe;

    // ---- перший вільний номер 000..999 -----------------------------------
    char path[MAX_PATH], fname[MAX_PATH], nick[NAME_MAX + 1];
    int n = next_number(dir);
    int room;
    if (n < 0) {
        say("!! all slots from 000 to 999 are taken in this folder\r\n");
        fail();
    }

    // ---- як назвати образ -------------------------------------------------
    // скільки знаків лишилось на власне ім'я: тека + \ + "000_" + ім'я + ".IMG"
    room = MAX_PATH - 1 - (int)strlen(dir) - 1 - 4 - 4;
    nick[0] = 0;
    if (room < 1)
        say("\r\nThe path to this folder is too long for a name; "
            "the image will be just a number.\r\n");
    else if (!ask_name(nick, NAME_MAX + 1, (size_t)room))
        nick[0] = 0;

    if (nick[0])
        _snprintf(fname, MAX_PATH - 1, "%03d_%s.IMG", n, nick);
    else
        _snprintf(fname, MAX_PATH - 1, "%03d.IMG", n);
    fname[MAX_PATH - 1] = 0;
    _snprintf(path, MAX_PATH - 1, "%s\\%s", dir, fname);
    path[MAX_PATH - 1] = 0;
    if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
        sayf("!! %s already exists in this folder\r\n", fname);
        fail();
    }

    // ---- коментар до образу ----------------------------------------------
    // Байти з клавіатури лягають у файл як є: ASCII в будь-якій кодовій
    // сторінці той самий, перекодовувати нічого.
    static char about[COMMENT_MAX + 2 + sizeof(ABOUT_BODY)];
    char note[COMMENT_MAX + 1];
    unsigned abosize = 0;
    note[0] = 0;
    if (ask_comment(note, COMMENT_MAX + 1)) {
        unsigned k = (unsigned)strlen(note);
        memcpy(about, note, k);
        about[k++] = '\r';
        about[k++] = '\n';
        memcpy(about + k, ABOUT_BODY, sizeof(ABOUT_BODY) - 1);
        abosize = k + (unsigned)(sizeof(ABOUT_BODY) - 1);
    }

    // ---- том у памʼяті ----------------------------------------------------
    static unsigned char img[IMGSIZE];
    memset(img, 0, sizeof(img));

    unsigned ftime, fdate;
    fat_now(&ftime, &fdate);
    unsigned volid = (unsigned)time(NULL);

    // boot-сектор: BPB такий самий, як у робочих образів, boot-код порожній
    unsigned char *bs = img;
    bs[0] = 0xEB; bs[1] = 0x3C; bs[2] = 0x90;
    memcpy(bs + 3, "MSDOS5.0", 8);
    put16(bs + 11, BPS);
    bs[13] = (unsigned char)SPC;
    put16(bs + 14, RES);
    bs[16] = (unsigned char)NFAT;
    put16(bs + 17, ROOTENT);
    put16(bs + 19, TOTSEC);
    bs[21] = MEDIA;
    put16(bs + 22, SPF);
    put16(bs + 24, SPT);
    put16(bs + 26, HEADS);
    put32(bs + 28, HIDDEN);
    put32(bs + 32, 0);
    bs[36] = 0x00;                    // номер приводу
    bs[37] = 0x00;
    bs[38] = 0x29;                    // розширений boot signature
    put32(bs + 39, volid);
    memcpy(bs + 43, LABEL, 11);
    memcpy(bs + 54, "FAT12   ", 8);
    bs[510] = 0x55; bs[511] = 0xAA;

    // ---- дані: README.TXT, за ним ABOUT_ME.TXT ---------------------------
    const unsigned rdsize = (unsigned)(sizeof(README) - 1);   // без завершального нуля
    const unsigned rdclus = (rdsize + CLUSTERSZ - 1) / CLUSTERSZ;
    unsigned next = 2, rdclus0, aboclus0 = 0;

    unsigned char *fat = img + RES * BPS;
    fat_set(fat, 0, 0xF00 | MEDIA);   // 0xFF9, збігається з байтом media
    fat_set(fat, 1, 0xFFF);

    rdclus0 = put_file(img, fat, &next, README, rdsize);
    if (abosize)
        aboclus0 = put_file(img, fat, &next, about, abosize);

    // друга копія FAT — точна копія першої
    memcpy(img + (RES + SPF) * BPS, fat, SPF * BPS);

    // ---- корінь: мітка тому, README.TXT, ABOUT_ME.TXT --------------------
    unsigned char *root = img + (RES + NFAT * SPF) * BPS;
    dirent(root,      LABEL,         0x08, 0, 0,       ftime, fdate);
    dirent(root + 32, "README  TXT", 0x20, rdclus0, rdsize, ftime, fdate);
    if (abosize)
        dirent(root + 64, "ABOUT_METXT", 0x20, aboclus0, abosize, ftime, fdate);

    // ---- запис ------------------------------------------------------------
    HANDLE f = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        sayf("!! cannot create %s (code %lu)\r\n", path, GetLastError());
        fail();
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, img, IMGSIZE, &wrote, NULL);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || wrote != IMGSIZE) {
        sayf("!! only %lu bytes written instead of %u\r\n", wrote, IMGSIZE);
        DeleteFileA(path);
        fail();
    }

    sayf("created:  %s\r\n", fname);
    sayf("folder:   %s\r\n", dir);
    sayf("size:     %u bytes = %u sectors of %u, no MBR\r\n", IMGSIZE, TOTSEC, BPS);
    sayf("layout:   FAT12, media 0x%02X, %u sectors per track, %u heads, "
         "cluster %u bytes\r\n", MEDIA, SPT, HEADS, CLUSTERSZ);
    sayf("inside:   README.TXT, %u bytes, %u cluster(s)\r\n", rdsize, rdclus);
    if (abosize) {
        sayf("          ABOUT_ME.TXT, %u bytes\r\n", abosize);
        sayf("comment:  %s\r\n", note);
    } else {
        say("          no ABOUT_ME.TXT - this image has no comment\r\n");
    }
    sayf("root:     %u entries, %u used\r\n", ROOTENT, abosize ? 3 : 2);

    // програма стала діалоговою, тому пауза потрібна й на успіху:
    // інакше вікно згорнеться раніше, ніж оператор прочитає, що вийшло
    say("\r\n-- press any key --");
    _getch();
    return 0;
}
