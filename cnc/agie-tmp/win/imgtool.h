// imgtool.h — спільний шар для UNPACK.EXE і PACK.EXE.
//
// Дві частини:
//   1) консольний ввід-вивід (вивід переживає перенаправлення у файл);
//   2) том FAT12 у файлі-образі: розбір BPB, ланцюги кластерів, корінь.
//
// Геометрія НЕ зашита: усе читається з BPB образу. Тому ті самі два exe
// беруть і наш 720 КБ (737280 Б), і звичайні 1,44 МБ, якщо колись знадобиться.
//
// Увесь текст, що бачить оператор, — англійською; базові запити й правила
// імен продубльовані болгарською кирилицею. Коментарі — українською.
//
// Кодування — cp866 (DOS-кирилиця), однобайтове, і всюди одне й те саме:
// у ньому консоль, у ньому коментар усередині образу, у ньому ж рядки в
// самому exe (див. -fexec-charset у build.sh). Ніяких широких знаків: усі
// виклики WinAPI — явно однобайтові, з суфіксом A.
//
// Чому 866, а не 1251: на цехових машинах (XP і Win7) консоль піднімається
// саме в 866, і растровий шрифт консолі має гліфи саме під неї. 1251 там
// або не встає, або встає без гліфів — кирилиця перетворюється на сміття.

#ifndef IMGTOOL_H
#define IMGTOOL_H

#include <windows.h>
#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* ===================== консоль ========================================== */

/* Рядок на екран як є, байт у байт. Якщо вивід перенаправлено у файл або
   канал, WriteConsoleA не спрацює — тоді ті самі байти йдуть WriteFile. */
static void say(const char *s)
{
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD n = 0, len = (DWORD)strlen(s);
    if (h == INVALID_HANDLE_VALUE || len == 0)
        return;
    if (WriteConsoleA(h, s, len, &n, NULL))
        return;
    WriteFile(h, s, len, &n, NULL);
}

static void sayf(const char *fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, 2047, fmt, ap);
    buf[2047] = 0;
    va_end(ap);
    say(buf);
}

/* Консоль примусово в cp866 — і на вивід, і на ввід. Байти, які ми пишемо,
   консоль так і покаже, а байти, які оператор набере, так і прочитаємо: між
   екраном, клавіатурою і ABOUT_ME.TXT ніякого перекодування немає взагалі. */
static void con_cp866(void)
{
    SetConsoleOutputCP(866);
    SetConsoleCP(866);
}

/* пауза потрібна тільки там, звідки програма більше нічого не питатиме:
   інакше вікно згорнеться раніше, ніж оператор прочитає причину */
static void pause_exit(int code)
{
    say("\r\nPress any key to close this window.\r\n");
    _getch();
    ExitProcess((UINT)code);
}

/* ===================== тека, де лежить сам exe ========================== */

static int exe_dir(char *out, size_t cap)
{
    char p[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, p, MAX_PATH);
    char *slash;
    if (len == 0 || len >= MAX_PATH)
        return 0;
    slash = strrchr(p, '\\');
    if (!slash)
        return 0;
    *slash = 0;
    if (strlen(p) + 1 > cap)
        return 0;
    strcpy(out, p);
    return 1;
}

/* ===================== дрібниці ========================================= */

/* копія рядка з обрізанням по буферу і завжди із завершальним нулем */
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

static unsigned get16(const unsigned char *p) { return p[0] | (p[1] << 8); }
static unsigned get32(const unsigned char *p)
{
    return (unsigned)p[0] | ((unsigned)p[1] << 8)
         | ((unsigned)p[2] << 16) | ((unsigned)p[3] << 24);
}
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

/* ===================== том ============================================== */

typedef struct {
    unsigned char *img;        /* увесь файл у памʼяті */
    unsigned  imgsize;         /* байтів у файлі */
    unsigned  bps, spc, res, nfat, rootent, totsec, spf;
    unsigned char media;
    unsigned  rootsec, rootsecs;   /* корінь: перший сектор і скільки їх */
    unsigned  datasec;             /* перший сектор даних */
    unsigned  clusters;            /* скільки кластерів даних (номери 2..clusters+1) */
    unsigned  clustersz;
    char   path[MAX_PATH];
} Vol;

static char volerr[512];

static void vol_close(Vol *v)
{
    if (v->img) free(v->img);
    v->img = NULL;
}

/* NULL = все гаразд, інакше — текст помилки англійською */
static const char *vol_open(const char *path, Vol *v)
{
    HANDLE f;
    DWORD  size, got = 0;
    unsigned char *bs;
    unsigned datasecs, fatneed;

    memset(v, 0, sizeof(*v));
    copy_str(v->path, MAX_PATH, path);

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        _snprintf(volerr, 511, "cannot open the image (code %lu)", GetLastError());
        return volerr;
    }
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size < 512) {
        CloseHandle(f);
        return "the file is too small to be a disk image";
    }
    if (size > 8u * 1024u * 1024u) {
        CloseHandle(f);
        return "the file is far too large to be a floppy image";
    }
    v->img = (unsigned char *)malloc(size);
    if (!v->img) {
        CloseHandle(f);
        return "out of memory";
    }
    if (!ReadFile(f, v->img, size, &got, NULL) || got != size) {
        CloseHandle(f);
        vol_close(v);
        return "the image could not be read to the end";
    }
    CloseHandle(f);
    v->imgsize = size;

    bs = v->img;
    v->bps     = get16(bs + 11);
    v->spc     = bs[13];
    v->res     = get16(bs + 14);
    v->nfat    = bs[16];
    v->rootent = get16(bs + 17);
    v->totsec  = get16(bs + 19);
    v->media   = bs[21];
    v->spf     = get16(bs + 22);
    if (v->totsec == 0)
        v->totsec = get32(bs + 32);

    if (v->bps != 512)
        return "not a floppy image: bytes per sector is not 512";
    if (v->spc == 0 || (v->spc & (v->spc - 1)) != 0)
        return "broken BPB: sectors per cluster is not a power of two";
    if (v->res == 0 || v->nfat == 0 || v->nfat > 2 || v->spf == 0)
        return "broken BPB: reserved sectors or FAT count out of range";
    if (v->rootent == 0 || (v->rootent * 32) % v->bps != 0)
        return "broken BPB: root directory entry count out of range";
    if (v->totsec == 0)
        return "broken BPB: total sector count is zero";

    v->clustersz = v->spc * v->bps;
    v->rootsecs  = v->rootent * 32 / v->bps;
    v->rootsec   = v->res + v->nfat * v->spf;
    v->datasec   = v->rootsec + v->rootsecs;
    if (v->datasec >= v->totsec)
        return "broken BPB: no data area left in the volume";
    datasecs   = v->totsec - v->datasec;
    v->clusters = datasecs / v->spc;

    if (v->clusters >= 4085)
        return "this image is not FAT12 (too many clusters); these tools handle FAT12 floppies only";
    fatneed = ((v->clusters + 2) * 3 + 1) / 2;
    if (fatneed > v->spf * v->bps)
        return "broken BPB: the FAT is too small for the cluster count";
    if ((DWORD)v->totsec * v->bps > v->imgsize)
        return "the file is shorter than the BPB says the volume is";

    if (bs[510] != 0x55 || bs[511] != 0xAA)
        say("   note: no 0x55AA signature in the boot sector; continuing anyway.\r\n");
    if ((DWORD)v->totsec * v->bps < v->imgsize)
        say("   note: the file has extra bytes after the end of the volume; ignoring them.\r\n");
    return NULL;
}

static int vol_save(Vol *v)
{
    HANDLE f;
    DWORD wrote = 0;
    BOOL ok;
    f = CreateFileA(v->path, GENERIC_WRITE, 0, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    ok = WriteFile(f, v->img, v->imgsize, &wrote, NULL);
    FlushFileBuffers(f);
    CloseHandle(f);
    return ok && wrote == v->imgsize;
}

/* ---- таблиця розміщення ------------------------------------------------ */

static unsigned fat_get(const Vol *v, unsigned k)
{
    const unsigned char *fat = v->img + v->res * v->bps;
    unsigned o = k * 3 / 2;
    unsigned x;
    if (o + 1 >= v->spf * v->bps)
        return 0xFF7;                       /* поза таблицею — вважаємо збійним */
    x = fat[o] | (fat[o + 1] << 8);
    return (k & 1) ? (x >> 4) : (x & 0xFFF);
}

/* пише в усі копії FAT одразу — інакше вони розʼїдуться */
static void fat_set(Vol *v, unsigned k, unsigned val)
{
    unsigned i;
    for (i = 0; i < v->nfat; i++) {
        unsigned char *fat = v->img + (v->res + i * v->spf) * v->bps;
        unsigned o = k * 3 / 2;
        if (o + 1 >= v->spf * v->bps)
            continue;
        if (k & 1) {
            fat[o]     = (unsigned char)((fat[o] & 0x0F) | ((val << 4) & 0xF0));
            fat[o + 1] = (unsigned char)((val >> 4) & 0xFF);
        } else {
            fat[o]     = (unsigned char)(val & 0xFF);
            fat[o + 1] = (unsigned char)((fat[o + 1] & 0xF0) | ((val >> 8) & 0x0F));
        }
    }
}

static unsigned char *clus_ptr(Vol *v, unsigned c)
{
    return v->img + ((DWORD)v->datasec + (DWORD)(c - 2) * v->spc) * v->bps;
}

static int clus_valid(const Vol *v, unsigned c)
{
    return c >= 2 && c < v->clusters + 2;
}

static unsigned free_clusters(const Vol *v)
{
    unsigned c, n = 0;
    for (c = 2; c < v->clusters + 2; c++)
        if (fat_get(v, c) == 0)
            n++;
    return n;
}

/* ---- корінь ------------------------------------------------------------ */

typedef struct {
    int      idx;
    char     raw[11];
    unsigned attr, clus, size, time, date;
    char  name[16];      /* NAME.EXT для показу */
} Dent;

static unsigned char *root_ent(Vol *v, int i)
{
    return v->img + (DWORD)v->rootsec * v->bps + (DWORD)i * 32;
}

/* 8.3 з 11 сирих байтів у NAME.EXT */
static void raw_to_name(const char *raw, char *out)
{
    char n[13];
    int i, k = 0, e;
    for (i = 7; i >= 0 && raw[i] == ' '; i--) {}
    for (e = 0; e <= i; e++)
        n[k++] = raw[e];
    for (i = 10; i >= 8 && raw[i] == ' '; i--) {}
    if (i >= 8) {
        n[k++] = '.';
        for (e = 8; e <= i; e++)
            n[k++] = raw[e];
    }
    n[k] = 0;
    strcpy(out, n);
}

/* 1 = це справжній файл; 0 = порожньо, стерто, мітка, тека або уламок LFN */
static int dent_load(Vol *v, int i, Dent *d)
{
    unsigned char *e = root_ent(v, i);
    if (e[0] == 0x00 || e[0] == 0xE5)
        return 0;
    if ((e[11] & 0x3F) == 0x0F)     /* запис довгого імені */
        return 0;
    if (e[11] & 0x08)               /* мітка тому */
        return 0;
    if (e[11] & 0x10)               /* тека; у наших образах їх немає */
        return 0;
    d->idx = i;
    memcpy(d->raw, e, 11);
    if ((unsigned char)d->raw[0] == 0x05)
        d->raw[0] = (char)0xE5;     /* так DOS кодує справжній 0xE5 у першому байті */
    d->attr = e[11];
    d->time = get16(e + 22);
    d->date = get16(e + 24);
    d->clus = get16(e + 26);
    d->size = get32(e + 28);
    raw_to_name(d->raw, d->name);
    return 1;
}


/* ===================== імена ============================================ */

/* Правила імен усередині образу. Друкуємо англійською і болгарською
   латинкою — оператор читає або те, або те.
   ДОЗВОЛЕНІ ЗНАКИ навмисно винесені в один рядок: якщо стійка вдавиться
   дефісом чи підкресленням, правити тут і більше ніде.
   Дефіс лишений тому, що він реально трапляється на робочих дискетах
   (1148-2.NC, 1267-1.NC). */
static const char *NAME_EXTRA = "-_";

static int name_char_ok(char c)
{
    if (c >= 'A' && c <= 'Z') return 1;
    if (c >= '0' && c <= '9') return 1;
    return strchr(NAME_EXTRA, c) != NULL && c != 0;
}

static void print_name_rules(void)
{
    say("\r\n   Name rules inside the image:\r\n"
        "     up to 8 characters, then a dot, then up to 3 characters;\r\n"
        "     capital letters A-Z, digits 0-9, and - _ only;\r\n"
        "     lower case is accepted but stored in capitals;\r\n"
        "     no spaces, no Cyrillic, no long names.\r\n"
        "     Examples: 1234.NC   PROG.JOB   1148-2.NC   TE45.GEO\r\n"
        "   Правила за имената вътре в образа:\r\n"
        "     до 8 знака, точка, до 3 знака;\r\n"
        "     само главни букви A-Z, цифри 0-9 и - _ ;\r\n"
        "     малките букви се приемат и стават главни;\r\n"
        "     без интервал, без кирилица, без дълги имена.\r\n");
}

/* Ім'я файлу-кандидата: суворо 8.3, великі літери. NULL = гаразд. */
static const char *name_83_check(const char *s)
{
    const char *dot;
    size_t n, e;
    if (!*s)
        return "the name is empty";
    if (strpbrk(s, "\\/:"))
        return "give the file name only, without a path; the file must sit next to this program";
    dot = strchr(s, '.');
    if (dot && strchr(dot + 1, '.'))
        return "a DOS name may contain only one dot";
    n = dot ? (size_t)(dot - s) : strlen(s);
    e = dot ? strlen(dot + 1) : 0;
    if (n < 1 || n > 8)
        return "the part before the dot must be 1 to 8 characters long";
    if (dot && (e < 1 || e > 3))
        return "the part after the dot must be 1 to 3 characters long";
    {
        size_t i;
        for (i = 0; i < strlen(s); i++) {
            if (s[i] == '.')
                continue;
            if (s[i] >= 'a' && s[i] <= 'z')
                return "lower case is not allowed; the name must be in capital letters";
            if (!name_char_ok(s[i]))
                return "the name contains a character that is not allowed";
        }
    }
    return NULL;
}

/* 8.3 -> 11 сирих байтів запису каталогу */
static void name_to_raw(const char *s, char *raw)
{
    const char *dot = strchr(s, '.');
    size_t n = dot ? (size_t)(dot - s) : strlen(s);
    size_t i;
    memset(raw, ' ', 11);
    for (i = 0; i < n && i < 8; i++)
        raw[i] = (char)s[i];
    if (dot)
        for (i = 0; i < 3 && dot[1 + i]; i++)
            raw[8 + i] = (char)dot[1 + i];
}


/* ===================== коментар образу ================================== */

/* ABOUT_ME.TXT: перший рядок — коментар до образу, за ним CRLF, далі
   пояснення, що файл видаляти не варто. Створює його NEWIMG.EXE.

   Коментар більше не доповнюється пробілами до 50 знаків і більше не
   обмежений ними: кінець коментаря — CRLF, довжина будь-яка до NOTE_MAX.
   Байти — cp866, тобто латинка і кирилиця однаково. Старі образи (рівно
   50 знаків, доповнені пробілами, і той самий CRLF) читаються тим самим
   кодом: хвостові пробіли обрізаються й від них нічого не лишається.

   Файлу може не бути — це не помилка, просто коментаря немає.

   Читаємо НЕ через vol_open: список образів на флешці буває довгим, і тягти
   по 737 КБ на кожен рядок меню — це видима пауза при старті. Тут вистачає
   boot-сектора, кореня і одного кластера, тобто десь 8 КБ на образ. */

#define NOTE_W    50     /* ширина колонки коментаря, по ній же й переносимо */
#define NOTE_MAXL 12     /* стільки рядків максимум лишається від коментаря */
#define NOTE_MAX  500    /* стільки знаків максимум у самому коментарі */

static const char ABOUT_RAW[12] = "ABOUT_METXT";

static int read_at(HANDLE f, DWORD off, void *buf, DWORD len)
{
    DWORD got = 0;
    if (SetFilePointer(f, (LONG)off, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER
        && GetLastError() != NO_ERROR)
        return 0;
    return ReadFile(f, buf, len, &got, NULL) && got == len;
}

/* Коментар образу в out[], байт у байт як він лежить в образі (cp866).
   0 = коментаря немає. cap має бути не менше за NOTE_MAX + 1. */
static int read_about(const char *path, char *out, size_t cap)
{
    static unsigned char root[512 * 32];
    unsigned char bs[512], data[NOTE_MAX + 2];
    HANDLE f;
    unsigned bps, spc, res, nfat, rootent, spf, rootsec, datasec, clustersz;
    unsigned clus = 0, size = 0, want;
    int i, n;

    out[0] = 0;
    if (cap < NOTE_MAX + 1)
        return 0;

    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return 0;
    if (!read_at(f, 0, bs, 512)) { CloseHandle(f); return 0; }

    bps     = get16(bs + 11);
    spc     = bs[13];
    res     = get16(bs + 14);
    nfat    = bs[16];
    rootent = get16(bs + 17);
    spf     = get16(bs + 22);
    if (bps != 512 || spc == 0 || res == 0 || nfat == 0 || nfat > 2
        || spf == 0 || rootent == 0 || rootent > 512 || (rootent * 32) % bps) {
        CloseHandle(f);
        return 0;
    }
    rootsec   = res + nfat * spf;
    datasec   = rootsec + rootent * 32 / bps;
    clustersz = spc * bps;

    if (!read_at(f, rootsec * bps, root, rootent * 32)) { CloseHandle(f); return 0; }

    for (i = 0; i < (int)rootent; i++) {
        unsigned char *e = root + i * 32;
        if (e[0] == 0x00 || e[0] == 0xE5)
            continue;
        if ((e[11] & 0x3F) == 0x0F || (e[11] & 0x18))
            continue;
        if (memcmp(e, ABOUT_RAW, 11) == 0) {
            clus = get16(e + 26);
            size = get32(e + 28);
            break;
        }
    }
    if (clus < 2 || size == 0) { CloseHandle(f); return 0; }

    /* перший рядок цілком лежить у першому кластері: NOTE_MAX + CRLF < 1 КБ */
    want = NOTE_MAX + 2;
    if (want > size)      want = size;
    if (want > clustersz) want = clustersz;
    if (!read_at(f, (datasec + (clus - 2) * spc) * bps, data, want)) {
        CloseHandle(f);
        return 0;
    }
    CloseHandle(f);

    /* до CRLF (та й до будь-якого керівного знака), далі — не наша справа */
    for (n = 0; n < (int)want && n < NOTE_MAX; n++) {
        if (data[n] < 32 || data[n] == 127)
            break;
        out[n] = (char)data[n];
    }
    while (n > 0 && out[n - 1] == ' ')
        n--;
    out[n] = 0;
    return n > 0;
}

/* Розкласти коментар на рядки не довші за NOTE_W, переносячи слова цілком.
   Слово, довше за цілий рядок, ріжеться силоміць — інакше його нікуди дінеш.
   Повертає скільки рядків вийшло, не більше за maxl. */
static int wrap_note(const char *s, char lines[][NOTE_W + 1], int maxl)
{
    int nl = 0;
    while (*s && nl < maxl) {
        size_t len, cut, i;
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        len = strlen(s);
        if (len <= NOTE_W) {
            cut = len;
        } else {
            cut = 0;
            for (i = NOTE_W; i > 0; i--)          /* назад до найближчого пробілу */
                if (s[i] == ' ') { cut = i; break; }
            if (cut == 0)
                cut = NOTE_W;                     /* суцільне слово — ріжемо */
        }
        memcpy(lines[nl], s, cut);
        lines[nl][cut] = 0;
        while (cut > 0 && lines[nl][cut - 1] == ' ')
            lines[nl][--cut] = 0;
        if (cut > 0)
            nl++;
        s += (cut == 0) ? 1 : cut;
    }
    return nl;
}

#endif /* IMGTOOL_H */
