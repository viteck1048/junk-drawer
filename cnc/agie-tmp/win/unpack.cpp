// UNPACK.EXE — витягає всі файли з образу дискети FAT12 у теку поряд.
//
// Резервний інструмент до Total Commander: TC зручніший, але стоїть не всюди.
// Ця програма нічого не встановлює, працює з теки, де лежить.
//
// Хід роботи:
//   на старті друкує список усіх .IMG, що лежать поряд -> оператор ходить
//   стрілками й тисне Enter -> усе з кореня образу лягає в теку з тим самим
//   ім'ям без розширення. На цьому робота завершується.
//   Питання виникає тільки одне і тільки як треба: коли така тека вже є.
//
// Дати й час файлів переносяться з каталогу образу, щоб було видно, коли
// програму справді писали.
//
// Увесь текст на екрані — англійською; базові запити й пояснення
// продубльовані болгарською латинкою. Тільки ASCII — див. шапку imgtool.h.
//
// Збірка:  i686-w64-mingw32-g++ -O2 -s -static -o UNPACK.EXE unpack.cpp

#include "conmenu.h"

#define MAXIMG 400
static MItem images[MAXIMG];

/* дата й час запису каталогу -> час файлу на диску */
static void stamp_file(HANDLE f, unsigned date, unsigned time)
{
    FILETIME lft, ft;
    if (date == 0)
        return;
    if (!DosDateTimeToFileTime((WORD)date, (WORD)time, &lft))
        return;
    if (!LocalFileTimeToFileTime(&lft, &ft))
        return;
    SetFileTime(f, &ft, NULL, &ft);
}

/* один файл із образу на диск. 0 = не вийшло, причина вже надрукована */
static int extract(Vol *v, const Dent *d, const char *folder)
{
    char path[MAX_PATH];
    HANDLE  f;
    unsigned left = d->size, c = d->clus, guard = 0;
    int ok = 1;

    _snprintf(path, MAX_PATH - 1, "%s\\%s", folder, d->name);
    path[MAX_PATH - 1] = 0;

    if (d->size > 0 && !clus_valid(v, c)) {
        sayf("   %-12s  SKIPPED: the directory entry points outside the data area\r\n",
             d->name);
        return 0;
    }

    f = CreateFileA(path, GENERIC_WRITE, 0, NULL,
                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        sayf("   %-12s  SKIPPED: cannot create the file (code %lu)\r\n",
             d->name, GetLastError());
        return 0;
    }

    while (left > 0) {
        DWORD chunk = (left < v->clustersz) ? left : v->clustersz;
        DWORD wrote = 0;
        unsigned next;
        if (!clus_valid(v, c)) {
            sayf("   %-12s  TRUNCATED: the cluster chain breaks after %u of %u bytes\r\n",
                 d->name, d->size - left, d->size);
            ok = 0;
            break;
        }
        if (++guard > v->clusters + 2) {
            sayf("   %-12s  TRUNCATED: the cluster chain loops back on itself\r\n",
                 d->name);
            ok = 0;
            break;
        }
        if (!WriteFile(f, clus_ptr(v, c), chunk, &wrote, NULL) || wrote != chunk) {
            sayf("   %-12s  FAILED: write error (code %lu)\r\n", d->name, GetLastError());
            ok = 0;
            break;
        }
        left -= chunk;
        next = fat_get(v, c);
        if (left == 0)
            break;
        c = next;
    }

    stamp_file(f, d->date, d->time);
    CloseHandle(f);
    return ok;
}

/* один вибраний образ. 1 = розпаковано, роботу закінчено;
   0 = не вийшло або оператор скасував, вертаємось до списку */
static int do_unpack(const char *dir, const char *imgname)
{
    Vol   v;
    Dent  list[512];
    char path[MAX_PATH], base[MAX_PATH], folder[MAX_PATH];
    const char *err;
    char *dot;
    int i, count = 0, done = 0, failed = 0;

    cursor_on();
    cls();

    _snprintf(path, MAX_PATH - 1, "%s\\%s", dir, imgname);
    path[MAX_PATH - 1] = 0;
    strncpy(base, imgname, MAX_PATH - 1);
    base[MAX_PATH - 1] = 0;
    dot = strrchr(base, '.');
    if (dot)
        *dot = 0;

    sayf("Opening %s ...\r\n", imgname);
    err = vol_open(path, &v);
    if (err) {
        sayf("!! %s\r\n", err);
        vol_close(&v);
        wait_key();
        return 0;
    }
    sayf("   FAT12, %u bytes, %u sectors of %u, cluster %u bytes, "
         "root holds %u entries\r\n",
         v.totsec * v.bps, v.totsec, v.bps, v.clustersz, v.rootent);

    /* увесь корінь, а не до першого нуля: це резервний інструмент,
       хай дістає й те, що лежить за термінатором каталогу */
    for (i = 0; i < (int)v.rootent && count < 512; i++)
        if (dent_load(&v, i, &list[count]))
            count++;

    if (count == 0) {
        say("   The image contains no files.\r\n"
            "   Obrazat e prazen.\r\n");
        vol_close(&v);
        wait_key();
        return 0;
    }
    sayf("   %d file(s) inside.\r\n", count);

    /* ---- куди класти ---------------------------------------------------- */
    _snprintf(folder, MAX_PATH - 1, "%s\\%s", dir, base);
    folder[MAX_PATH - 1] = 0;

    if (GetFileAttributesA(folder) != INVALID_FILE_ATTRIBUTES) {
        char c = 0;
        sayf("\r\n!! A folder or file named %s already exists here.\r\n", base);
        say("   O = extract into it, overwriting files with the same names\r\n"
            "   N = extract into a new folder next to it\r\n"
            "   C = cancel, back to the list\r\n"
            "   O = razopakovai vatre, presapisvai saeshtite imena\r\n"
            "   N = nova papka do neya\r\n"
            "   C = otkazvane, obratno kam spisaka\r\n");
        for (;;) {
            char ch = 0;
            int k = get_key(&ch);
            if (k == K_ESC) { c = 'C'; break; }
            if (k != K_CHAR) continue;
            if (ch >= 'a' && ch <= 'z')
                ch = (char)(ch - 'a' + 'A');
            if (ch == 'O' || ch == 'N' || ch == 'C') { c = ch; break; }
        }
        if (c == 'C') {
            vol_close(&v);
            return 0;
        }
        if (c == 'N') {
            int k;
            for (k = 2; k < 1000; k++) {
                _snprintf(folder, MAX_PATH - 1, "%s\\%s-%d", dir, base, k);
                folder[MAX_PATH - 1] = 0;
                if (GetFileAttributesA(folder) == INVALID_FILE_ATTRIBUTES)
                    break;
            }
            if (k >= 1000) {
                say("!! no free folder name left; clean the folder up first\r\n");
                vol_close(&v);
                wait_key();
                return 0;
            }
        } else if (!(GetFileAttributesA(folder) & FILE_ATTRIBUTE_DIRECTORY)) {
            sayf("!! %s is a file, not a folder; cannot extract into it\r\n", base);
            vol_close(&v);
            wait_key();
            return 0;
        }
    }

    if (GetFileAttributesA(folder) == INVALID_FILE_ATTRIBUTES
        && !CreateDirectoryA(folder, NULL)) {
        sayf("!! cannot create the folder (code %lu)\r\n", GetLastError());
        vol_close(&v);
        wait_key();
        return 0;
    }

    /* ---- витягуємо ------------------------------------------------------ */
    sayf("\r\nInto: %s\r\n\r\n", folder);
    for (i = 0; i < count; i++) {
        Dent *d = &list[i];
        if (extract(&v, d, folder)) {
            sayf("   %-12s  %7u bytes  %04u-%02u-%02u %02u:%02u\r\n",
                 d->name, d->size,
                 1980 + (d->date >> 9), (d->date >> 5) & 0x0F, d->date & 0x1F,
                 (d->time >> 11) & 0x1F, (d->time >> 5) & 0x3F);
            done++;
        } else {
            failed++;
        }
    }

    sayf("\r\n   %d file(s) extracted", done);
    if (failed)
        sayf(", %d had trouble (see above)", failed);
    say(".\r\n   The image itself was not changed.\r\n");
    vol_close(&v);
    wait_key();
    return 1;
}

int main(void)
{
    char dir[MAX_PATH];
    Menu m;
    int n;

    con_init();

    if (!exe_dir(dir, MAX_PATH)) {
        say("!! cannot work out my own folder\r\n");
        wait_key();
        con_done();
        return 1;
    }

    n = collect_images(dir, images, MAXIMG);
    if (n == 0) {
        cls();
        say("UNPACK - extract the files out of an AGIE floppy image\r\n\r\n"
            "There is no .IMG file next to this program.\r\n"
            "Copy an image here, or make one with NEWIMG.EXE, and start again.\r\n\r\n"
            "Nyama nito edin .IMG do tazi programa.\r\n"
            "Kopirai obraz tuk ili napravi nov s NEWIMG.EXE.\r\n");
        sayf("\r\nFolder: %s\r\n", dir);
        wait_key();
        con_done();
        return 1;
    }

    memset(&m, 0, sizeof(m));
    m.it  = images;
    m.n   = n;
    m.t1  = "UNPACK - choose an image and press Enter to extract it";
    m.t2  = "Izberi obraz sas strelkite i natisni Enter.";
    m.h1  = "  Arrows = move   Right/Left = open/close the comment   "
            "Enter = unpack   Esc = quit";
    m.h2  = "  Strelki = dvizhenie   Dyasno/Lyavo = tseliya komentar   "
            "Enter = razopakovai   Esc = izhod";
    m.btn = "[  Quit  /  Izhod  ]";
    m.infow    = 16;      /* тільки дата: розмір в усіх образів однаковий */
    m.showdone = 0;

    cursor_off();
    for (;;) {
        int r;
        menu_draw(&m);
        do {
            r = menu_key(&m);
        } while (r == MENU_NONE);
        if (r == MENU_QUIT)
            break;
        if (do_unpack(dir, images[m.sel].name))
            break;                      /* розпаковано — робота скінчена */
        cursor_off();                   /* скасовано або збій — назад до списку */
    }

    cursor_on();
    con_done();
    return 0;
}
