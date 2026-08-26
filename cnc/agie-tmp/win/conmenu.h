// conmenu.h — повноекранний список у консолі: стрілки, Enter, Esc,
//             софт-кнопка внизу, скрол і статусна область під списком.
//
// Малюємо через консольне API напряму (позиція курсора + атрибути кольору).
// Якщо вивід або ввід перенаправлені (запуск із файлу, тести), усе працює
// далі: екран не чиститься, кожен кадр просто дописується, а клавіші
// читаються словами з рядка — DOWN, UP, ENTER, ESC. Це дає змогуганяти
// програму скриптом, не втрачаючи живого інтерфейсу для оператора.
//
// Кольори рядка списку:
//   звичайний        0x07  світло-сірий на чорному
//   вже доданий      0x08  темно-сірий  (те саме, що «підсвітити сірим»)
//   під курсором     0x70  чорний на світло-сірому
//   під курсором + доданий 0x87  світло-сірий на темно-сірому

#ifndef CONMENU_H
#define CONMENU_H

#include "imgtool.h"

enum { K_NONE = 0, K_UP, K_DOWN, K_PGUP, K_PGDN, K_HOME, K_END,
       K_ENTER, K_ESC, K_CHAR };

enum { MENU_NONE = 0, MENU_PICK, MENU_QUIT };

#define A_PLAIN   0x07
#define A_DONE    0x08
#define A_SEL     0x70
#define A_SELDONE 0x87
#define A_TITLE   0x0F
#define A_HINT    0x07
#define A_ERR     0x0C
#define A_OK      0x0A

static HANDLE c_out = NULL, c_in = NULL;
static int    c_out_con = 0, c_in_con = 0;
static WORD   c_attr0 = A_PLAIN;
static DWORD  c_inmode0 = 0;
static int    c_curvis = 1;
static int    win_w = 80, win_h = 25;

/* ---- запуск і відновлення ---------------------------------------------- */

static void con_restore(void)
{
    if (c_out_con) {
        CONSOLE_CURSOR_INFO ci;
        SetConsoleTextAttribute(c_out, c_attr0);
        if (GetConsoleCursorInfo(c_out, &ci)) {
            ci.bVisible = (BOOL)c_curvis;
            SetConsoleCursorInfo(c_out, &ci);
        }
    }
    if (c_in_con)
        SetConsoleMode(c_in, c_inmode0);
}

/* Ctrl+C: спершу повернути консолі нормальний вигляд, потім хай гасить */
static BOOL WINAPI con_ctrl(DWORD type)
{
    (void)type;
    con_restore();
    return FALSE;
}

static void con_size(void)
{
    CONSOLE_SCREEN_BUFFER_INFO c;
    if (c_out_con && GetConsoleScreenBufferInfo(c_out, &c)) {
        win_w = c.srWindow.Right - c.srWindow.Left + 1;
        win_h = c.srWindow.Bottom - c.srWindow.Top + 1;
    }
    if (win_w < 40)  win_w = 40;
    if (win_w > 200) win_w = 200;
    if (win_h < 12)  win_h = 12;
}

/* Коментар до образу — це ще 50 колонок праворуч, у 80 вони не влазять.
   Розсуваємо вікно: спершу буфер, потім саме вікно, інакше йому нікуди рости.
   Якщо екран вужчий, GetLargestConsoleWindowSize урве до можливого, а putline
   дообріже рядок — інтерфейс просто стане тіснішим, але не поламається. */
static void con_widen(int cols)
{
    CONSOLE_SCREEN_BUFFER_INFO c;
    COORD big, sz;
    SMALL_RECT r;

    if (!c_out_con || !GetConsoleScreenBufferInfo(c_out, &c))
        return;
    big = GetLargestConsoleWindowSize(c_out);
    if (big.X > 0 && cols > big.X)
        cols = big.X;
    if (c.dwSize.X >= cols)
        return;
    sz.X = (SHORT)cols;
    sz.Y = c.dwSize.Y;
    if (!SetConsoleScreenBufferSize(c_out, sz))
        return;
    r.Left   = 0;
    r.Top    = c.srWindow.Top;
    r.Right  = (SHORT)(cols - 1);
    r.Bottom = c.srWindow.Bottom;
    SetConsoleWindowInfo(c_out, TRUE, &r);
}

static void con_init(void)
{
    CONSOLE_SCREEN_BUFFER_INFO c;
    CONSOLE_CURSOR_INFO ci;
    DWORD m;

    c_out = GetStdHandle(STD_OUTPUT_HANDLE);
    c_in  = GetStdHandle(STD_INPUT_HANDLE);
    c_out_con = (c_out != INVALID_HANDLE_VALUE)
                && GetConsoleScreenBufferInfo(c_out, &c);
    c_in_con  = (c_in != INVALID_HANDLE_VALUE) && GetConsoleMode(c_in, &m);

    if (c_out_con) {
        c_attr0 = c.wAttributes;
        if (GetConsoleCursorInfo(c_out, &ci))
            c_curvis = ci.bVisible ? 1 : 0;
    }
    if (c_in_con) {
        c_inmode0 = m;
        /* без рядкового вводу й ехо, але з обробкою Ctrl+C */
        SetConsoleMode(c_in, ENABLE_PROCESSED_INPUT);
    }
    SetConsoleCtrlHandler(con_ctrl, TRUE);
    con_widen(110);
    con_size();
}

static void con_done(void)
{
    con_restore();
}

static void cursor_off(void)
{
    CONSOLE_CURSOR_INFO ci;
    if (c_out_con && GetConsoleCursorInfo(c_out, &ci)) {
        ci.bVisible = FALSE;
        SetConsoleCursorInfo(c_out, &ci);
    }
}

static void cursor_on(void)
{
    CONSOLE_CURSOR_INFO ci;
    if (c_out_con && GetConsoleCursorInfo(c_out, &ci)) {
        ci.bVisible = TRUE;
        SetConsoleCursorInfo(c_out, &ci);
    }
}

/* ---- малювання ---------------------------------------------------------- */

static void cls(void)
{
    CONSOLE_SCREEN_BUFFER_INFO c;
    COORD z = {0, 0};
    DWORD n, cells;
    if (!c_out_con) {
        say(L"\r\n");
        return;
    }
    if (!GetConsoleScreenBufferInfo(c_out, &c))
        return;
    cells = (DWORD)c.dwSize.X * (DWORD)c.dwSize.Y;
    FillConsoleOutputCharacterW(c_out, L' ', cells, z, &n);
    FillConsoleOutputAttribute(c_out, c_attr0, cells, z, &n);
    SetConsoleCursorPosition(c_out, z);
    con_size();
}

/* один рядок на позиції y, доповнений пробілами до ширини вікна */
static void putline(int y, WORD a, const wchar_t *s)
{
    wchar_t buf[256];
    /* у консолі рядок обрізаємо по ширині вікна, у файл/канал — не обрізаємо */
    int w = c_out_con ? (win_w - 1) : 254;
    int i, len;

    if (w > 254) w = 254;
    len = (int)wcslen(s);
    for (i = 0; i < w; i++)
        buf[i] = (i < len) ? s[i] : L' ';
    buf[w] = 0;

    if (c_out_con) {
        COORD p;
        p.X = 0;
        p.Y = (SHORT)y;
        SetConsoleCursorPosition(c_out, p);
        SetConsoleTextAttribute(c_out, a);
        say(buf);
        SetConsoleTextAttribute(c_out, c_attr0);
    } else {
        /* без консолі кольору немає — обрізаємо хвостові пробіли */
        len = w;
        while (len > 0 && buf[len - 1] == L' ')
            buf[len - 1] = 0, len--;
        say(buf);
        say(L"\r\n");
    }
}

static void putlinef(int y, WORD a, const wchar_t *fmt, ...)
{
    wchar_t buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf(buf, 255, fmt, ap);
    buf[255] = 0;
    va_end(ap);
    putline(y, a, buf);
}

/* ---- клавіші ------------------------------------------------------------ */

/* запасний шлях: ввід перенаправлено, читаємо назви клавіш словами */
static int key_from_line(wchar_t *ch)
{
    char a[64];
    DWORD got = 0;
    size_t i = 0;
    while (i + 1 < sizeof(a)) {
        char c;
        if (!ReadFile(c_in, &c, 1, &got, NULL) || got == 0)
            break;
        if (c == '\n')
            break;
        if (c != '\r')
            a[i++] = c;
    }
    if (i == 0 && got == 0)
        return K_ESC;
    a[i] = 0;
    if (!_stricmp(a, "UP"))    return K_UP;
    if (!_stricmp(a, "DOWN"))  return K_DOWN;
    if (!_stricmp(a, "PGUP"))  return K_PGUP;
    if (!_stricmp(a, "PGDN"))  return K_PGDN;
    if (!_stricmp(a, "HOME"))  return K_HOME;
    if (!_stricmp(a, "END"))   return K_END;
    if (!_stricmp(a, "ENTER")) return K_ENTER;
    if (!_stricmp(a, "ESC") || !a[0]) return K_ESC;
    if (ch) *ch = (wchar_t)a[0];
    return K_CHAR;
}

static int get_key(wchar_t *ch)
{
    if (ch) *ch = 0;
    if (!c_in_con)
        return key_from_line(ch);
    for (;;) {
        INPUT_RECORD r;
        DWORD n = 0;
        if (!ReadConsoleInputW(c_in, &r, 1, &n) || n == 0)
            return K_ESC;
        if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown)
            continue;
        switch (r.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:     return K_UP;
        case VK_DOWN:   return K_DOWN;
        case VK_PRIOR:  return K_PGUP;
        case VK_NEXT:   return K_PGDN;
        case VK_HOME:   return K_HOME;
        case VK_END:    return K_END;
        case VK_RETURN: return K_ENTER;
        case VK_ESCAPE: return K_ESC;
        default: break;
        }
        if (r.Event.KeyEvent.uChar.UnicodeChar >= 32) {
            if (ch) *ch = r.Event.KeyEvent.uChar.UnicodeChar;
            return K_CHAR;
        }
    }
}

/* «натисни будь-яку клавішу», але без рядкового вводу */
static void wait_key(void)
{
    say(L"\r\nPress any key.  Natisni koi da e klavish.\r\n");
    get_key(NULL);
}

/* ---- список ------------------------------------------------------------- */

typedef struct {
    wchar_t name[96];      /* справжнє ім'я файлу на диску */
    wchar_t disp[96];      /* як показувати (у pack — великими літерами) */
    wchar_t info[48];
    wchar_t note[64];      /* коментар з ABOUT_ME.TXT; у списку файлів порожній */
    int     done;          /* уже записаний в образ -> сірим */
} MItem;

typedef struct {
    MItem *it;
    int    n;
    int    sel, top, rows;
    int    y0;             /* перший рядок списку */
    int    ybtn, yhint, ystat;
    const wchar_t *t1, *t2;
    const wchar_t *h1, *h2;
    const wchar_t *btn;
} Menu;

#define STATLINES 4

static void menu_layout(Menu *m)
{
    con_size();
    m->y0   = 3;
    m->rows = win_h - (m->y0 + 1 + 1 + 1 + 2 + 1 + STATLINES);
    if (m->rows > m->n) m->rows = m->n;
    if (m->rows < 1)    m->rows = 1;
    m->ybtn  = m->y0 + m->rows + 1;
    m->yhint = m->ybtn + 2;
    m->ystat = m->yhint + 3;
    if (m->sel < 0) m->sel = 0;
    if (m->sel > m->n) m->sel = m->n;
    if (m->sel >= m->n) {
        m->top = m->n - m->rows;      /* на софт-кнопці показуємо хвіст списку */
    } else {
        if (m->top > m->sel)
            m->top = m->sel;
        if (m->sel >= m->top + m->rows)
            m->top = m->sel - m->rows + 1;
    }
    if (m->top > m->n - m->rows) m->top = m->n - m->rows;
    if (m->top < 0) m->top = 0;
}

static void menu_draw_items(Menu *m)
{
    int i;
    for (i = 0; i < m->rows; i++) {
        int k = m->top + i;
        if (k < m->n) {
            MItem *e = &m->it[k];
            WORD a = (k == m->sel) ? (e->done ? A_SELDONE : A_SEL)
                                   : (e->done ? A_DONE    : A_PLAIN);
            putlinef(m->y0 + i, a, L" %s %-13s %-31s %-6s %s",
                     (k == m->sel) ? L">" : L" ",
                     e->disp, e->info, e->done ? L"added" : L"", e->note);
        } else {
            putline(m->y0 + i, A_PLAIN, L"");
        }
    }
    putlinef(m->ybtn, (m->sel == m->n) ? A_SEL : A_PLAIN,
             L" %s %s", (m->sel == m->n) ? L">" : L" ", m->btn);
}

static void menu_draw(Menu *m)
{
    menu_layout(m);
    cls();
    putlinef(0, A_TITLE, L"%s   [ %d / %d ]",
             m->t1, (m->sel < m->n) ? m->sel + 1 : m->n, m->n);
    putline(1, A_TITLE, m->t2);
    putline(2, A_PLAIN, L"");
    menu_draw_items(m);
    putline(m->ybtn + 1, A_PLAIN, L"");
    putline(m->yhint,     A_HINT, m->h1);
    putline(m->yhint + 1, A_HINT, m->h2);
}

/* заголовок містить лічильник, тому при русі перемальовуємо і його */
static void menu_refresh(Menu *m)
{
    menu_layout(m);
    putlinef(0, A_TITLE, L"%s   [ %d / %d ]",
             m->t1, (m->sel < m->n) ? m->sel + 1 : m->n, m->n);
    menu_draw_items(m);
}

static void menu_status_clear(Menu *m)
{
    int i;
    for (i = 0; i < STATLINES; i++)
        putline(m->ystat + i, A_PLAIN, L"");
}

static void menu_status(Menu *m, int line, WORD a, const wchar_t *fmt, ...)
{
    wchar_t buf[256];
    va_list ap;
    if (line < 0 || line >= STATLINES)
        return;
    va_start(ap, fmt);
    _vsnwprintf(buf, 255, fmt, ap);
    buf[255] = 0;
    va_end(ap);
    putline(m->ystat + line, a, buf);
}

static int menu_key(Menu *m)
{
    int k = get_key(NULL);
    int last = m->n;                  /* n == софт-кнопка */
    switch (k) {
    case K_UP:    if (m->sel > 0)    m->sel--;               break;
    case K_DOWN:  if (m->sel < last) m->sel++;               break;
    case K_PGUP:  m->sel -= m->rows; if (m->sel < 0) m->sel = 0;        break;
    case K_PGDN:  m->sel += m->rows; if (m->sel > last) m->sel = last;  break;
    case K_HOME:  m->sel = 0;                                break;
    case K_END:   m->sel = last;                             break;
    case K_ESC:   return MENU_QUIT;
    case K_ENTER: return (m->sel == last) ? MENU_QUIT : MENU_PICK;
    default:      return MENU_NONE;
    }
    menu_refresh(m);
    return MENU_NONE;
}


/* ===================== збір списків ===================================== */

static void stamp_info(const FILETIME *ft, DWORD size, wchar_t *out, size_t cap)
{
    SYSTEMTIME st;
    FILETIME lft;
    if (FileTimeToLocalFileTime(ft, &lft) && FileTimeToSystemTime(&lft, &st))
        _snwprintf(out, cap - 1, L"%7lu bytes  %04u-%02u-%02u %02u:%02u",
                   (unsigned long)size, st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute);
    else
        _snwprintf(out, cap - 1, L"%7lu bytes", (unsigned long)size);
    out[cap - 1] = 0;
}

/* впорядкувати за іменем: FindFirstFile порядку не гарантує, а оператор
   очікує 000, 001, 002 підряд */
static void sort_items(MItem *it, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        MItem t = it[i];
        for (j = i - 1; j >= 0 && _wcsicmp(it[j].disp, t.disp) > 0; j--)
            it[j + 1] = it[j];
        it[j + 1] = t;
    }
}

/* усі *.IMG поряд */
static int collect_images(const wchar_t *dir, MItem *it, int max)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;
    _snwprintf(pat, MAX_PATH - 1, L"%s\\*.img", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (fd.nFileSizeHigh || wcslen(fd.cFileName) > 90)
            continue;
        if (n >= max)
            break;
        wcscpy(it[n].name, fd.cFileName);
        wcscpy(it[n].disp, fd.cFileName);
        stamp_info(&fd.ftLastWriteTime, fd.nFileSizeLow, it[n].info, 48);
        {   /* коментар з ABOUT_ME.TXT; немає файлу — колонка лишається порожня */
            wchar_t full[MAX_PATH];
            _snwprintf(full, MAX_PATH - 1, L"%s\\%s", dir, fd.cFileName);
            full[MAX_PATH - 1] = 0;
            if (!read_about(full, it[n].note, 64))
                it[n].note[0] = 0;
        }
        it[n].done = 0;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    sort_items(it, n);
    return n;
}

/* Кандидати на пакування: ім'я лягає в 8.3, розмір не більший за maxsize.
   Малі латинські літери приймаємо і показуємо великими — у цьому й уся
   поблажка; в образ ім'я теж піде великими.
   .EXE відкидаємо: це наші ж утиліти, на дискеті стійки їм нічого робити.
   Образи відсіюються самі — 737280 Б більше за 360 КБ. */
static int collect_files(const wchar_t *dir, MItem *it, int max, DWORD maxsize)
{
    wchar_t pat[MAX_PATH];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    int n = 0;
    _snwprintf(pat, MAX_PATH - 1, L"%s\\*", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileW(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        wchar_t up[96];
        size_t i, len;
        const wchar_t *dot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (fd.nFileSizeHigh || fd.nFileSizeLow > maxsize)
            continue;
        len = wcslen(fd.cFileName);
        if (len == 0 || len > 12)
            continue;
        for (i = 0; i <= len; i++) {
            wchar_t c = fd.cFileName[i];
            up[i] = (c >= L'a' && c <= L'z') ? (wchar_t)(c - L'a' + L'A') : c;
        }
        if (name_83_check(up) != NULL)
            continue;
        dot = wcsrchr(up, L'.');
        if (dot && _wcsicmp(dot, L".EXE") == 0)
            continue;
        if (n >= max)
            break;
        wcscpy(it[n].name, fd.cFileName);
        wcscpy(it[n].disp, up);
        stamp_info(&fd.ftLastWriteTime, fd.nFileSizeLow, it[n].info, 48);
        it[n].note[0] = 0;
        it[n].done = 0;
        n++;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    sort_items(it, n);
    return n;
}

#endif /* CONMENU_H */
