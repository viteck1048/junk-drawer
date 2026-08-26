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
// Імʼя добирається саме: 000.IMG, як зайняте — 001.IMG, і так до 999.IMG.
//
// Питає рівно одне: короткий коментар до образу, до 50 знаків латинкою.
// Він лягає в перші 50 байтів ABOUT_ME.TXT, доповнений пробілами, а PACK.EXE
// та UNPACK.EXE показують його в своїх списках. Порожній ввід — файла немає.
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
"empty, correctly formatted image, asks for a short comment, and does\r\n"
"nothing else. Each new image is named 000.IMG, 001.IMG, 002.IMG and so\r\n"
"on, taking the first free number in the folder it is started from.\r\n"
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
"   You may rename it first; keep the .IMG extension.\r\n"
"\r\n"
"THE COMMENT: ABOUT_ME.TXT\r\n"
"If you gave NEWIMG.EXE a comment, this image also holds ABOUT_ME.TXT.\r\n"
"Its first line is that comment: 50 characters, padded with spaces.\r\n"
"PACK.EXE and UNPACK.EXE read it and show it beside the image name in\r\n"
"their lists, so you can tell the images apart without opening them.\r\n"
"To change the comment, edit the first line and keep it exactly 50\r\n"
"characters long. Deleting the file only leaves that column empty;\r\n"
"nothing else breaks.\r\n"
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
"pravilno formatiran obraz, pita za kratak komentar i nishto poveche.\r\n"
"Vseki nov obraz se kazva 000.IMG, 001.IMG, 002.IMG i taka natatak -\r\n"
"vzema parviya svoboden nomer v papkata, ot koyato e startirana.\r\n"
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
"   Mozhesh da go preimenuvash predi tova; zapazi razshirenieto .IMG.\r\n"
"\r\n"
"KOMENTARAT: ABOUT_ME.TXT\r\n"
"Ako si dal komentar na NEWIMG.EXE, v tozi obraz ima i ABOUT_ME.TXT.\r\n"
"Parviyat mu red e tozi komentar: 50 znaka, dopalneni s intervali.\r\n"
"PACK.EXE i UNPACK.EXE go chetat i go pokazvat do imeto na obraza v\r\n"
"spisatsite si, za da razlichavash obrazite bez da gi otvaryash.\r\n"
"Za da smenish komentara, redaktirai parviya red i go zapazi tochno\r\n"
"50 znaka dalag. Ako iztriesh faila, kolonata prosto ostava prazna -\r\n"
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
"The 50 characters in the first line above are the comment for this\r\n"
"image. PACK.EXE and UNPACK.EXE read them and show them next to the\r\n"
"image in their lists. Keep the first line 50 characters long, padded\r\n"
"with spaces, or the comment will come out wrong. Without this file\r\n"
"the comment column simply stays empty; nothing else breaks.\r\n"
"\r\n"
"Ne iztrivai tozi fail.\r\n"
"Parvite 50 znaka na parviya red gore sa komentarat na tozi obraz.\r\n"
"PACK.EXE i UNPACK.EXE go pokazvat v spisaka do imeto na obraza.\r\n"
"Parviyat red tryabva da e tochno 50 znaka, dopalnen s intervali.\r\n"
"Bez tozi fail kolonata s komentara ostava prazna - nishto drugo.\r\n";

#define ABOUT_MAX 50

// ---- вивід ----------------------------------------------------------------
static void say(const wchar_t *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0;
    if (h == INVALID_HANDLE_VALUE)
        return;
    if (WriteConsoleW(h, s, (DWORD)wcslen(s), &n, NULL))
        return;
    // вивід перенаправлено у файл — консольного API там немає, йдемо в UTF-8
    {
        int need = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
        char *a;
        if (need <= 1)
            return;
        a = (char *)malloc((size_t)need);
        if (!a)
            return;
        WideCharToMultiByte(CP_UTF8, 0, s, -1, a, need, NULL, NULL);
        WriteFile(h, a, (DWORD)(need - 1), &n, NULL);
        free(a);
    }
}

static void sayf(const wchar_t *fmt, ...)
{
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 1023, fmt, ap);
    buf[1023] = 0;
    va_end(ap);
    say(buf);
}

// один рядок з клавіатури; 0 = кінець вводу
static int read_line(wchar_t *buf, size_t cap)
{
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode, n = 0;
    size_t i;

    buf[0] = 0;
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    if (GetConsoleMode(h, &mode)) {
        if (!ReadConsoleW(h, buf, (DWORD)(cap - 1), &n, NULL) || n == 0)
            return 0;
        buf[n] = 0;
    } else {
        char a[256];
        DWORD got = 0;
        i = 0;
        while (i + 1 < sizeof(a)) {
            char c;
            if (!ReadFile(h, &c, 1, &got, NULL) || got == 0)
                break;
            if (c == '\n')
                break;
            a[i++] = c;
        }
        if (i == 0 && got == 0)
            return 0;
        a[i] = 0;
        MultiByteToWideChar(CP_ACP, 0, a, -1, buf, (int)cap);
    }
    i = wcslen(buf);
    while (i > 0 && (buf[i - 1] == L'\r' || buf[i - 1] == L'\n' || buf[i - 1] == L' '))
        buf[--i] = 0;
    return 1;
}

// пауза тільки тут: без неї вікно зникне раніше, ніж помилку прочитають
static void fail(void)
{
    say(L"\r\n-- press any key --");
    _getch();
    ExitProcess(1);
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

// коментар з клавіатури: до 50 знаків, тільки друковані ASCII.
// 0 = оператор пропустив; інакше out[] — рівно ABOUT_MAX байтів з пробілами.
static int ask_comment(char *out)
{
    wchar_t in[256];
    for (;;) {
        size_t len, i;
        int bad = 0;

        say(L"\r\nShort comment for this image, up to 50 characters, Latin only.\r\n"
            L"Press Enter on an empty line if you do not want one.\r\n"
            L"Kratak komentar za tozi obraz, do 50 znaka, samo latinitsa.\r\n"
            L"Prazen red = bez komentar.\r\n> ");
        if (!read_line(in, 256) || !in[0])
            return 0;

        len = wcslen(in);
        if (len > ABOUT_MAX) {
            sayf(L"!! too long: %u characters, the limit is %u\r\n"
                 L"!! tvarde dalgo: %u znaka, maksimum %u\r\n",
                 (unsigned)len, ABOUT_MAX, (unsigned)len, ABOUT_MAX);
            continue;
        }
        for (i = 0; i < len; i++)
            if (in[i] < 32 || in[i] > 126)
                bad = 1;
        if (bad) {
            say(L"!! Latin letters, digits and simple punctuation only.\r\n"
                L"!! Samo latinitsa, tsifri i prosta punktuatsiya.\r\n");
            continue;
        }
        memset(out, ' ', ABOUT_MAX);
        for (i = 0; i < len; i++)
            out[i] = (char)in[i];
        return 1;
    }
}

int main(void)
{
    SetConsoleOutputCP(CP_UTF8);

    // ---- тека, де лежить сам exe (а не поточна тека процесу) --------------
    wchar_t exe[MAX_PATH];
    DWORD len = GetModuleFileNameW(NULL, exe, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        say(L"!! cannot work out my own path\r\n");
        fail();
    }
    wchar_t *slash = wcsrchr(exe, L'\\');
    if (!slash) {
        say(L"!! my own path has no separator\r\n");
        fail();
    }
    *slash = 0;                       // тепер exe == тека
    const wchar_t *dir = exe;

    // ---- перше вільне імʼя 000.IMG .. 999.IMG ----------------------------
    wchar_t path[MAX_PATH];
    int n;
    for (n = 0; n < 1000; n++) {
        _snwprintf(path, MAX_PATH - 1, L"%s\\%03d.IMG", dir, n);
        path[MAX_PATH - 1] = 0;
        if (GetFileAttributesW(path) == INVALID_FILE_ATTRIBUTES)
            break;
    }
    if (n >= 1000) {
        say(L"!! all names from 000.IMG to 999.IMG are taken\r\n");
        fail();
    }

    // ---- коментар до образу ----------------------------------------------
    static char about[ABOUT_MAX + 2 + sizeof(ABOUT_BODY)];
    unsigned abosize = 0;
    if (ask_comment(about)) {
        about[ABOUT_MAX]     = '\r';
        about[ABOUT_MAX + 1] = '\n';
        memcpy(about + ABOUT_MAX + 2, ABOUT_BODY, sizeof(ABOUT_BODY) - 1);
        abosize = ABOUT_MAX + 2 + (unsigned)(sizeof(ABOUT_BODY) - 1);
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
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        sayf(L"!! cannot create %s (code %lu)\r\n", path, GetLastError());
        fail();
    }
    DWORD wrote = 0;
    BOOL ok = WriteFile(f, img, IMGSIZE, &wrote, NULL);
    FlushFileBuffers(f);
    CloseHandle(f);
    if (!ok || wrote != IMGSIZE) {
        sayf(L"!! only %lu bytes written instead of %u\r\n", wrote, IMGSIZE);
        DeleteFileW(path);
        fail();
    }

    sayf(L"created:  %03d.IMG\r\n", n);
    sayf(L"folder:   %s\r\n", dir);
    sayf(L"size:     %u bytes = %u sectors of %u, no MBR\r\n", IMGSIZE, TOTSEC, BPS);
    sayf(L"layout:   FAT12, media 0x%02X, %u sectors per track, %u heads, "
         L"cluster %u bytes\r\n", MEDIA, SPT, HEADS, CLUSTERSZ);
    sayf(L"inside:   README.TXT, %u bytes, %u cluster(s)\r\n", rdsize, rdclus);
    if (abosize) {
        char c50[ABOUT_MAX + 1];
        wchar_t w50[ABOUT_MAX + 1];
        int k = ABOUT_MAX;
        memcpy(c50, about, ABOUT_MAX);
        while (k > 0 && c50[k - 1] == ' ')
            k--;
        c50[k] = 0;
        MultiByteToWideChar(CP_ACP, 0, c50, -1, w50, ABOUT_MAX + 1);
        sayf(L"          ABOUT_ME.TXT, %u bytes\r\n", abosize);
        sayf(L"comment:  %s\r\n", w50);
    } else {
        say(L"          no ABOUT_ME.TXT - this image has no comment\r\n");
    }
    sayf(L"root:     %u entries, %u used\r\n", ROOTENT, abosize ? 3 : 2);

    // програма стала діалоговою, тому пауза потрібна й на успіху:
    // інакше вікно згорнеться раніше, ніж оператор прочитає, що вийшло
    say(L"\r\n-- press any key --");
    _getch();
    return 0;
}
