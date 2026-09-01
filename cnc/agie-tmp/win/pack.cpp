// PACK.EXE — кладе файли з теки поряд усередину образу дискети FAT12.
//
// Резервний інструмент до Total Commander: TC зручніший, але стоїть не всюди.
// Ця програма нічого не встановлює, працює з теки, де лежить.
//
// Хід роботи:
//   на старті друкує список усіх .IMG поряд -> стрілки й Enter вибирають
//   «дискету» -> далі список файлів поряд, які годяться за іменем і не більші
//   за 360 КБ -> кожен Enter записує файл в образ і підсвічує його сірим,
//   щоб оператор бачив, що цей уже доданий.
//   Програма не спиняється, поки не натиснуть Esc, софт-кнопку внизу списку
//   або Ctrl+C.
//
// Малі латинські літери в іменах приймаються, але і в списку, і в образі
// ім'я стає великим: усередині FAT12 воно все одно зберігається великими.
//
// Образ переписується цілком і тільки після того, як усі перевірки пройшли:
// жодних тимчасових файлів поряд не створюється. Це навмисно — на робочій
// флешці зайвий файл зсунув би нумерацію слотів ґотека.
//
// Дата й час запису каталогу беруться з самого файлу, а не з годинника.
//
// Увесь текст на екрані — англійською; базові запити й пояснення
// продубльовані болгарською латинкою. Тільки ASCII — див. шапку imgtool.h.
//
// Збірка:  i686-w64-mingw32-g++ -O2 -s -static -o PACK.EXE pack.cpp

#include "conmenu.h"

#define MAXIMG   400
#define MAXFILE  400
#define MAXSRC   (360u * 1024u)      /* 368640 Б — і образи відсіюються самі */

static MItem images[MAXIMG];
static MItem files[MAXFILE];

/* час останнього запису файлу -> дата й час запису каталогу FAT */
static void file_stamp(const char *path, unsigned *date, unsigned *time)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    FILETIME lft;
    WORD d = 0, t = 0;
    *date = 0; *time = 0;
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa))
        return;
    if (!FileTimeToLocalFileTime(&fa.ftLastWriteTime, &lft))
        return;
    if (!FileTimeToDosDateTime(&lft, &d, &t))
        return;
    *date = d;
    *time = t;
}

/* один запис каталогу: ім'я 8.3 рівно 11 байтів, без записів LFN */
static void dirent_write(unsigned char *e, const char *raw11, unsigned char attr,
                         unsigned clus, unsigned size, unsigned time, unsigned date)
{
    memset(e, 0, 32);
    memcpy(e, raw11, 11);
    e[11] = attr;
    put16(e + 14, time);
    put16(e + 16, date);
    put16(e + 18, date);
    put16(e + 22, time);
    put16(e + 24, date);
    put16(e + 26, clus);
    put32(e + 28, size);
}

/* скільки кластерів у ланцюгу, що починається з c */
static unsigned chain_len(Vol *v, unsigned c)
{
    unsigned n = 0;
    while (clus_valid(v, c) && n <= v->clusters + 2) {
        n++;
        c = fat_get(v, c);
    }
    return n;
}

static void chain_free(Vol *v, unsigned c)
{
    unsigned guard = 0;
    while (clus_valid(v, c) && ++guard <= v->clusters + 2) {
        unsigned next = fat_get(v, c);
        fat_set(v, c, 0);
        c = next;
    }
}

static unsigned root_used(Vol *v)
{
    unsigned i, used = 0;
    for (i = 0; i < v->rootent; i++) {
        unsigned char b = root_ent(v, (int)i)[0];
        if (b != 0x00 && b != 0xE5)
            used++;
    }
    return used;
}

static void show_space(Menu *m, Vol *v, int line)
{
    menu_status(m, line, A_PLAIN,
                "   Image now holds %u of %u root entries, %u bytes free.",
                root_used(v), v->rootent, free_clusters(v) * v->clustersz);
}

/* питання в статусній області; повертає велику літеру з keys або 0 */
static char ask_status(const char *keys)
{
    for (;;) {
        char ch = 0;
        int k = get_key(&ch);
        if (k == K_ESC)
            return 0;
        if (k != K_CHAR)
            continue;
        if (ch >= 'a' && ch <= 'z')
            ch = (char)(ch - 'a' + 'A');
        if (strchr(keys, ch))
            return ch;
    }
}

/* Записати вибраний файл в образ. 1 = записано (підсвічуємо сірим). */
static int add_file(Vol *v, const char *dir, const char *imgfile,
                    MItem *e, Menu *m)
{
    char src[MAX_PATH];
    WIN32_FILE_ATTRIBUTE_DATA fa;
    char     raw[11];
    unsigned size, need, freec, fdate, ftime;
    unsigned oldclus = 0, oldlen = 0;
    int      i, slot = -1;
    unsigned char *buf = NULL;

    menu_status_clear(m);

    _snprintf(src, MAX_PATH - 1, "%s\\%s", dir, e->name);
    src[MAX_PATH - 1] = 0;
    if (!GetFileAttributesExA(src, GetFileExInfoStandard, &fa)
        || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        menu_status(m, 0, A_ERR, "!! %s is gone from the folder.", e->disp);
        return 0;
    }
    size = fa.nFileSizeLow;
    name_to_raw(e->disp, raw);          /* в образ ім'я йде великими літерами */
    file_stamp(src, &fdate, &ftime);

    /* ---- чи є вже такий файл усередині ---------------------------------- */
    for (i = 0; i < (int)v->rootent; i++) {
        Dent d;
        if (!dent_load(v, i, &d))
            continue;
        if (memcmp(d.raw, raw, 11) == 0) {
            char c;
            menu_status(m, 0, A_ERR,
                        "!! %s is already inside %s (%u bytes).",
                        d.name, imgfile, d.size);
            menu_status(m, 1, A_PLAIN,
                        "   R = replace it with the file on disk    C = cancel");
            menu_status(m, 2, A_PLAIN,
                        "   R = zameni go s faila ot diska          C = otkazvane");
            c = ask_status("RC");
            menu_status_clear(m);
            if (c != 'R') {
                menu_status(m, 0, A_PLAIN, "   Cancelled, the image was not touched.");
                return 0;
            }
            /* нічого не звільняємо просто зараз: якщо новий файл не влізе
               або не прочитається, старий має лишитись цілим */
            oldclus = d.clus;
            oldlen  = chain_len(v, d.clus);
            slot    = i;
            break;
        }
    }

    /* ---- місце ---------------------------------------------------------- */
    need  = (size + v->clustersz - 1) / v->clustersz;
    freec = free_clusters(v) + oldlen;      /* заміна віддає свої кластери назад */
    if (need > freec) {
        menu_status(m, 0, A_ERR,
                    "!! not enough room: %s needs %u bytes, only %u bytes are free.",
                    e->disp, size, freec * v->clustersz);
        menu_status(m, 1, A_PLAIN,
                    "   Use another image, or delete something from this one first.");
        menu_status(m, 2, A_PLAIN,
                    "   Izberi drug obraz ili iztrii neshto ot tozi.");
        return 0;
    }
    if (slot < 0) {
        for (i = 0; i < (int)v->rootent; i++) {
            unsigned char b = root_ent(v, i)[0];
            if (b == 0x00 || b == 0xE5) { slot = i; break; }
        }
    }
    if (slot < 0) {
        menu_status(m, 0, A_ERR,
                    "!! the root directory of %s is full: all %u entries are taken.",
                    imgfile, v->rootent);
        menu_status(m, 1, A_PLAIN,
                    "   This floppy format has no room for more files.");
        return 0;
    }

    /* ---- читаємо файл --------------------------------------------------- */
    if (size > 0) {
        HANDLE f = CreateFileA(src, GENERIC_READ, FILE_SHARE_READ, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        DWORD got = 0;
        if (f == INVALID_HANDLE_VALUE) {
            menu_status(m, 0, A_ERR, "!! cannot read %s (code %lu)",
                        e->disp, GetLastError());
            return 0;
        }
        buf = (unsigned char *)malloc(size);
        if (!buf) {
            CloseHandle(f);
            menu_status(m, 0, A_ERR, "!! out of memory");
            return 0;
        }
        if (!ReadFile(f, buf, size, &got, NULL) || got != size) {
            CloseHandle(f);
            free(buf);
            menu_status(m, 0, A_ERR, "!! %s could not be read to the end", e->disp);
            return 0;
        }
        CloseHandle(f);
    }

    /* ---- аж тепер, коли все перевірено, чіпаємо образ -------------------- */
    if (oldclus)
        chain_free(v, oldclus);

    {
        unsigned first = 0, prev = 0, c, taken = 0, off = 0;
        for (c = 2; c < v->clusters + 2 && taken < need; c++) {
            unsigned chunk;
            if (fat_get(v, c) != 0)
                continue;
            chunk = (size - off < v->clustersz) ? (size - off) : v->clustersz;
            memcpy(clus_ptr(v, c), buf + off, chunk);
            if (chunk < v->clustersz)             /* хвіст кластера — у нулі */
                memset(clus_ptr(v, c) + chunk, 0, v->clustersz - chunk);
            off += chunk;
            fat_set(v, c, 0xFFF);                 /* поки що кінець ланцюга */
            if (prev)
                fat_set(v, prev, c);
            else
                first = c;
            prev = c;
            taken++;
        }
        if (buf)
            free(buf);
        dirent_write(root_ent(v, slot), raw, 0x20, first, size, ftime, fdate);
    }

    if (!vol_save(v)) {
        menu_status(m, 0, A_ERR, "!! WRITING %s FAILED (code %lu).",
                    imgfile, GetLastError());
        menu_status(m, 1, A_ERR,
                    "   Check the image with Total Commander before using it.");
        return 0;
    }

    menu_status(m, 0, A_OK, "   added %s to %s - %u bytes, %u cluster(s)",
                e->disp, imgfile, size, need);
    show_space(m, v, 1);
    return 1;
}

int main(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    char t1[160], t2[160], h1[160], h2[160];
    Menu mi, mf;
    Vol  v;
    int  n, nf;
    const char *err;

    con_init();

    if (!exe_dir(dir, MAX_PATH)) {
        say("!! cannot work out my own folder\r\n");
        wait_key();
        con_done();
        return 1;
    }

    /* ---- який образ ------------------------------------------------------ */
    n = collect_images(dir, images, MAXIMG);
    if (n == 0) {
        cls();
        say("PACK - put files into an AGIE floppy image\r\n\r\n"
            "There is no .IMG file next to this program.\r\n"
            "PACK never creates an image - NEWIMG.EXE does that.\r\n\r\n"
            "Nyama nito edin .IMG do tazi programa.\r\n"
            "Napravi nov obraz s NEWIMG.EXE.\r\n");
        sayf("\r\nFolder: %s\r\n", dir);
        wait_key();
        con_done();
        return 1;
    }

    memset(&mi, 0, sizeof(mi));
    mi.it  = images;
    mi.n   = n;
    mi.t1  = "PACK - choose the image you want to put files into";
    mi.t2  = "Izberi obraza, v koito shte slagash failove.";
    mi.h1  = "  Arrows = move   Right/Left = open/close the comment   "
             "Enter = choose   Esc = quit";
    mi.h2  = "  Strelki = dvizhenie   Dyasno/Lyavo = tseliya komentar   "
             "Enter = izberi   Esc = izhod";
    mi.btn = "[  Quit  /  Izhod  ]";
    mi.infow    = 16;      /* тільки дата: розмір в усіх образів однаковий */
    mi.showdone = 0;

    cursor_off();
    for (;;) {
        int r;
        menu_draw(&mi);
        do {
            r = menu_key(&mi);
        } while (r == MENU_NONE);
        if (r == MENU_QUIT) {
            cursor_on();
            con_done();
            return 0;
        }

        _snprintf(path, MAX_PATH - 1, "%s\\%s", dir, images[mi.sel].name);
        path[MAX_PATH - 1] = 0;

        /* краще впертись у «тільки читання» зараз, ніж після всіх перевірок */
        {
            HANDLE t = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
            if (t == INVALID_HANDLE_VALUE) {
                menu_status_clear(&mi);
                menu_status(&mi, 0, A_ERR,
                            "!! %s cannot be opened for writing (code %lu).",
                            images[mi.sel].name, GetLastError());
                menu_status(&mi, 1, A_PLAIN,
                            "   Read-only, or open in another program?");
                continue;
            }
            CloseHandle(t);
        }
        err = vol_open(path, &v);
        if (err) {
            menu_status_clear(&mi);
            menu_status(&mi, 0, A_ERR, "!! %s", err);
            vol_close(&v);
            continue;
        }
        break;
    }

    /* ---- які файли ------------------------------------------------------- */
    nf = collect_files(dir, files, MAXFILE, MAXSRC);
    if (nf == 0) {
        cursor_on();
        cls();
        sayf("No file next to this program can go into %s.\r\n"
             "Nyama podhodyasht fail do tazi programa.\r\n\r\n"
             "A file is taken only if its name fits the DOS 8.3 rules and it is\r\n"
             "not bigger than 360 KB.\r\n", images[mi.sel].name);
        print_name_rules();
        sayf("\r\nFolder: %s\r\n", dir);
        wait_key();
        vol_close(&v);
        con_done();
        return 1;
    }

    _snprintf(t1, 159, "PACK - Enter puts the file into %s", images[mi.sel].name);
    _snprintf(t2, 159, "Enter zapisva faila v %s. Sivite sa veche dobaveni.",
               images[mi.sel].name);
    _snprintf(h1, 159, "  Arrows = move    Enter = put into the image    "
                        "Esc = finish");
    _snprintf(h2, 159, "  Strelki = dvizhenie    Enter = zapishi    Esc = krai");

    memset(&mf, 0, sizeof(mf));
    mf.it  = files;
    mf.n   = nf;
    mf.t1  = t1;
    mf.t2  = t2;
    mf.h1  = h1;
    mf.h2  = h2;
    mf.btn = "[  Finish  /  Gotovo  ]";
    mf.infow    = 31;      /* розмір і дата: у файлів вони різні й потрібні */
    mf.showdone = 1;

    menu_draw(&mf);
    show_space(&mf, &v, 0);
    for (;;) {
        int r = menu_key(&mf);
        if (r == MENU_QUIT)
            break;
        if (r != MENU_PICK)
            continue;
        if (add_file(&v, dir, images[mi.sel].name, &files[mf.sel], &mf)) {
            files[mf.sel].done = 1;
            menu_draw_items(&mf);       /* сірий рядок з'являється одразу */
        }
    }

    vol_close(&v);
    cursor_on();
    cls();
    say("Done.  Gotovo.\r\n");
    con_done();
    return 0;
}
