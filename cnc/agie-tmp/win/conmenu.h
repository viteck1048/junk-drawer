// conmenu.h — повноекранний список у консолі: стрілки, Enter, Esc,
//             софт-кнопка внизу, скрол і статусна область під списком.
//
// Усе однобайтове: рядки — char, виклики WinAPI — з явним суфіксом A,
// знаки — тільки ASCII (чому саме так, написано в шапці imgtool.h).
// Скільки байтів, стільки й знакомісць.
//
// Малюємо через консольне API напряму (позиція курсора + атрибути кольору).
// Якщо вивід або ввід перенаправлені (запуск із файлу, тести), усе працює
// далі: екран не чиститься, кожен кадр просто дописується, а клавіші
// читаються словами з рядка — DOWN, UP, ENTER, ESC. Це дає змогуганяти
// програму скриптом, не втрачаючи живого інтерфейсу для оператора.
// Стрілки праворуч/ліворуч у тому ж перенаправленому вводі — RIGHT і LEFT.
//
// Пункт списку заввишки один рядок, поки його коментар згорнутий. Стрілка
// праворуч розгортає коментар на всі його рядки (по 50 знаків, слова цілі),
// стрілка ліворуч згортає назад. У згорнутому вигляді останні три значущі
// знаки першого рядка — крапки, якщо там є що показувати далі.
//
// Кольори рядка списку:
//   звичайний        0x07  світло-сірий на чорному
//   вже доданий      0x08  темно-сірий  (те саме, що «підсвітити сірим»)
//   під курсором     0x70  чорний на світло-сірому
//   під курсором + доданий 0x87  світло-сірий на темно-сірому

#ifndef CONMENU_H
#define CONMENU_H

#include "imgtool.h"

enum { K_NONE = 0, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_PGUP, K_PGDN, K_HOME,
       K_END, K_ENTER, K_ESC, K_CHAR };

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
        say("\r\n");
        return;
    }
    if (!GetConsoleScreenBufferInfo(c_out, &c))
        return;
    cells = (DWORD)c.dwSize.X * (DWORD)c.dwSize.Y;
    FillConsoleOutputCharacterA(c_out, ' ', cells, z, &n);
    FillConsoleOutputAttribute(c_out, c_attr0, cells, z, &n);
    SetConsoleCursorPosition(c_out, z);
    con_size();
}

/* один рядок на позиції y, доповнений пробілами до ширини вікна */
static void putline(int y, WORD a, const char *s)
{
    char buf[256];
    /* у консолі рядок обрізаємо по ширині вікна, у файл/канал — не обрізаємо */
    int w = c_out_con ? (win_w - 1) : 254;
    int i, len;

    if (w > 254) w = 254;
    len = (int)strlen(s);
    for (i = 0; i < w; i++)
        buf[i] = (i < len) ? s[i] : ' ';
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
        while (len > 0 && buf[len - 1] == ' ')
            buf[len - 1] = 0, len--;
        say(buf);
        say("\r\n");
    }
}

static void putlinef(int y, WORD a, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, 255, fmt, ap);
    buf[255] = 0;
    va_end(ap);
    putline(y, a, buf);
}

/* ---- клавіші ------------------------------------------------------------ */

/* запасний шлях: ввід перенаправлено, читаємо назви клавіш словами */
static int key_from_line(char *ch)
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
    if (!_stricmp(a, "LEFT"))  return K_LEFT;
    if (!_stricmp(a, "RIGHT")) return K_RIGHT;
    if (!_stricmp(a, "PGUP"))  return K_PGUP;
    if (!_stricmp(a, "PGDN"))  return K_PGDN;
    if (!_stricmp(a, "HOME"))  return K_HOME;
    if (!_stricmp(a, "END"))   return K_END;
    if (!_stricmp(a, "ENTER")) return K_ENTER;
    if (!_stricmp(a, "ESC") || !a[0]) return K_ESC;
    if (ch) *ch = (char)a[0];
    return K_CHAR;
}

static int get_key(char *ch)
{
    if (ch) *ch = 0;
    if (!c_in_con)
        return key_from_line(ch);
    for (;;) {
        INPUT_RECORD r;
        DWORD n = 0;
        if (!ReadConsoleInputA(c_in, &r, 1, &n) || n == 0)
            return K_ESC;
        if (r.EventType != KEY_EVENT || !r.Event.KeyEvent.bKeyDown)
            continue;
        switch (r.Event.KeyEvent.wVirtualKeyCode) {
        case VK_UP:     return K_UP;
        case VK_DOWN:   return K_DOWN;
        case VK_LEFT:   return K_LEFT;
        case VK_RIGHT:  return K_RIGHT;
        case VK_PRIOR:  return K_PGUP;
        case VK_NEXT:   return K_PGDN;
        case VK_HOME:   return K_HOME;
        case VK_END:    return K_END;
        case VK_RETURN: return K_ENTER;
        case VK_ESCAPE: return K_ESC;
        default: break;
        }
        if ((unsigned char)r.Event.KeyEvent.uChar.AsciiChar >= 32) {
            if (ch) *ch = r.Event.KeyEvent.uChar.AsciiChar;
            return K_CHAR;
        }
    }
}

/* «натисни будь-яку клавішу», але без рядкового вводу */
static void wait_key(void)
{
    say("\r\nPress any key.  Natisni koi da e klavish.\r\n");
    get_key(NULL);
}

/* ---- список ------------------------------------------------------------- */

#define NAMEW 16               /* стільки знакомісць під ім'я образу/файлу */

typedef struct {
    char name[96];      /* справжнє ім'я файлу на диску */
    char disp[96];      /* як показувати (у pack — великими літерами) */
    char info[48];
    char nline[NOTE_MAXL][NOTE_W + 1];  /* коментар, уже розкладений по рядках */
    int     nlines;        /* скільки їх; 0 = коментаря немає */
    int     open;          /* коментар розгорнутий на всі рядки */
    int     done;          /* уже записаний в образ -> сірим */
} MItem;

typedef struct {
    MItem *it;
    int    n;
    int    sel, topline, rows;   /* topline — рядок, а не пункт: пункти різні */
    int    y0;                   /* перший рядок списку */
    int    ybtn, yhint, ystat;
    int    infow;                /* ширина колонки з датою (і розміром) */
    int    showdone;             /* чи є взагалі колонка «added» */
    const char *t1, *t2;
    const char *h1, *h2;
    const char *btn;
} Menu;

#define STATLINES 4

/* скільки рядків займає пункт: розгорнутий коментар — усі свої */
static int item_h(const Menu *m, int k)
{
    const MItem *e = &m->it[k];
    return (e->open && e->nlines > 1) ? e->nlines : 1;
}

/* абсолютний номер рядка, з якого починається пункт k */
static int item_line(const Menu *m, int k)
{
    int i, ln = 0;
    for (i = 0; i < k && i < m->n; i++)
        ln += item_h(m, i);
    return ln;
}

static int total_lines(const Menu *m) { return item_line(m, m->n); }

/* пункт, якому належить рядок ln */
static int item_at(const Menu *m, int ln)
{
    int i, y = 0;
    for (i = 0; i < m->n; i++) {
        y += item_h(m, i);
        if (ln < y)
            return i;
    }
    return m->n ? m->n - 1 : 0;
}

/* ім'я в NAMEW знакомісць; довше — тильда на останньому */
static void name_cut(const char *s, char *out)
{
    size_t len = strlen(s);
    if (len <= NAMEW) {
        strcpy(out, s);
        return;
    }
    strncpy(out, s, NAMEW - 1);
    out[NAMEW - 1] = '~';
    out[NAMEW] = 0;
}

/* Перший рядок коментаря для згорнутого пункту. Якщо далі є ще рядки,
   останні три значущі знаки заміняємо крапками — той самий знак обірваності,
   що й тильда в задовгому імені. */
static void note_head(const MItem *e, char *out)
{
    int len;
    out[0] = 0;
    if (e->nlines == 0)
        return;
    strcpy(out, e->nline[0]);
    if (e->nlines == 1)
        return;
    len = (int)strlen(out);
    if (len <= 3) {
        strcpy(out, "...");
        return;
    }
    out[len - 3] = '.';
    out[len - 2] = '.';
    out[len - 1] = '.';
}

static void menu_layout(Menu *m)
{
    int tot, avail;
    con_size();
    m->y0 = 3;
    avail = win_h - (m->y0 + 1 + 1 + 1 + 2 + 1 + STATLINES);
    if (avail < 1)
        avail = 1;
    tot = total_lines(m);
    m->rows = (tot < avail) ? tot : avail;
    if (m->rows < 1)
        m->rows = 1;
    m->ybtn  = m->y0 + m->rows + 1;
    m->yhint = m->ybtn + 2;
    m->ystat = m->yhint + 3;
    if (m->sel < 0)     m->sel = 0;
    if (m->sel > m->n)  m->sel = m->n;
    if (m->topline > tot - m->rows) m->topline = tot - m->rows;
    if (m->topline < 0) m->topline = 0;
}

/* Підкрутити видиме поле під вибраний пункт. Крутимо східцями по три рядки:
   курсор стрибає по образах, як і раніше, а список під ним іде рівними
   кроками, а не смикається щоразу, коли попереду чийсь розгорнутий
   коментар. У розгорнутого пункту намагаємось показати всі його рядки. */
#define SCROLL_STEP 3

static void menu_show_sel(Menu *m)
{
    int ln, h, want, need;

    if (m->sel >= m->n)          /* софт-кнопка стоїть на своєму місці завжди */
        return;
    ln = item_line(m, m->sel);
    h  = item_h(m, m->sel);
    if (h > m->rows)
        h = m->rows;
    if (ln < m->topline) {
        need = m->topline - ln;
        m->topline -= ((need + SCROLL_STEP - 1) / SCROLL_STEP) * SCROLL_STEP;
        if (m->topline < 0)
            m->topline = 0;
    }
    want = ln + h - 1;
    if (want > m->topline + m->rows - 1) {
        need = want - (m->topline + m->rows - 1);
        m->topline += ((need + SCROLL_STEP - 1) / SCROLL_STEP) * SCROLL_STEP;
    }
}

static void menu_draw_items(Menu *m)
{
    char nm[NAMEW + 2], nt[NOTE_W + 4];
    const char *gap = m->showdone ? "       " : "";
    int k, j, ln = 0, y = 0;

    for (k = 0; k < m->n && y < m->rows; k++) {
        MItem *e = &m->it[k];
        int h = item_h(m, k);
        WORD a = (k == m->sel) ? (e->done ? A_SELDONE : A_SEL)
                               : (e->done ? A_DONE    : A_PLAIN);
        for (j = 0; j < h; j++, ln++) {
            if (ln < m->topline)
                continue;
            if (y >= m->rows)
                break;
            if (j == 0) {
                name_cut(e->disp, nm);
                if (h == 1)
                    note_head(e, nt);
                else
                    strcpy(nt, e->nline[0]);
                putlinef(m->y0 + y, a, " %s %-*s %-*s %s%s",
                         (k == m->sel) ? ">" : " ",
                         NAMEW, nm, m->infow, e->info,
                         (m->showdone && e->done) ? "added  " : gap, nt);
            } else {
                putlinef(m->y0 + y, a, "   %-*s %-*s %s%s",
                         NAMEW, "", m->infow, "", gap, e->nline[j]);
            }
            y++;
        }
    }
    while (y < m->rows) {
        putline(m->y0 + y, A_PLAIN, "");
        y++;
    }
    putlinef(m->ybtn, (m->sel == m->n) ? A_SEL : A_PLAIN,
             " %s %s", (m->sel == m->n) ? ">" : " ", m->btn);
}

/* усе, крім статусної області */
static void menu_paint(Menu *m)
{
    menu_layout(m);
    putlinef(0, A_TITLE, "%s   [ %d / %d ]",
             m->t1, (m->sel < m->n) ? m->sel + 1 : m->n, m->n);
    putline(1, A_TITLE, m->t2);
    putline(2, A_PLAIN, "");
    menu_draw_items(m);
    putline(m->ybtn + 1, A_PLAIN, "");
    putline(m->yhint,     A_HINT, m->h1);
    putline(m->yhint + 1, A_HINT, m->h2);
}

static void menu_draw(Menu *m)
{
    menu_layout(m);
    cls();
    menu_paint(m);
}

/* заголовок містить лічильник, тому при русі перемальовуємо і його */
static void menu_refresh(Menu *m)
{
    menu_layout(m);
    putlinef(0, A_TITLE, "%s   [ %d / %d ]",
             m->t1, (m->sel < m->n) ? m->sel + 1 : m->n, m->n);
    menu_draw_items(m);
}

/* Розгортання/згортання коментаря змінює висоту списку, а з нею й місце
   софт-кнопки, підказок і статусної області. Коли блок виріс, нове малювання
   само накриває старе; коли вкоротився — під ним лишився б хвіст, тому там
   чистимо екран. */
static void menu_relayout(Menu *m, int oldbtn)
{
    menu_layout(m);
    menu_show_sel(m);
    if (m->ybtn < oldbtn)
        menu_draw(m);
    else
        menu_paint(m);
}

static void menu_status_clear(Menu *m)
{
    int i;
    for (i = 0; i < STATLINES; i++)
        putline(m->ystat + i, A_PLAIN, "");
}

static void menu_status(Menu *m, int line, WORD a, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    if (line < 0 || line >= STATLINES)
        return;
    va_start(ap, fmt);
    _vsnprintf(buf, 255, fmt, ap);
    buf[255] = 0;
    va_end(ap);
    putline(m->ystat + line, a, buf);
}

static int menu_key(Menu *m)
{
    int k = get_key(NULL);
    int last = m->n;
    int oldbtn, tot;

    menu_layout(m);
    oldbtn = m->ybtn;
    tot = total_lines(m);

    switch (k) {
    case K_UP:
        if (m->sel > 0) {
            m->sel--;
            menu_show_sel(m);
        }
        break;
    case K_DOWN:
        if (m->sel < last) {
            m->sel++;
            menu_show_sel(m);
        }
        break;
    case K_RIGHT:
        if (m->sel < m->n && m->it[m->sel].nlines > 1 && !m->it[m->sel].open) {
            m->it[m->sel].open = 1;
            menu_relayout(m, oldbtn);
            return MENU_NONE;
        }
        break;
    case K_LEFT:
        if (m->sel < m->n && m->it[m->sel].open) {
            m->it[m->sel].open = 0;
            menu_relayout(m, oldbtn);
            return MENU_NONE;
        }
        break;
    case K_PGUP:
        if (tot <= m->rows) {
            m->sel = 0;
        } else {
            m->topline -= m->rows;
            if (m->topline < 0)
                m->topline = 0;
            m->sel = item_at(m, m->topline);
        }
        break;
    case K_PGDN:
        if (tot <= m->rows) {
            m->sel = last;
        } else {
            m->topline += m->rows;
            if (m->topline > tot - m->rows)
                m->topline = tot - m->rows;
            m->sel = item_at(m, m->topline);
        }
        break;
    case K_HOME:
        m->sel = 0;
        m->topline = 0;
        break;
    case K_END:
        m->sel = last;
        m->topline = (tot > m->rows) ? tot - m->rows : 0;
        break;
    case K_ESC:
        return MENU_QUIT;
    case K_ENTER:
        return (m->sel == last) ? MENU_QUIT : MENU_PICK;
    default:
        return MENU_NONE;
    }
    menu_refresh(m);
    return MENU_NONE;
}


/* ===================== збір списків ===================================== */

static void stamp_info(const FILETIME *ft, DWORD size, char *out, size_t cap)
{
    SYSTEMTIME st;
    FILETIME lft;
    if (FileTimeToLocalFileTime(ft, &lft) && FileTimeToSystemTime(&lft, &st))
        _snprintf(out, cap - 1, "%7lu bytes  %04u-%02u-%02u %02u:%02u",
                   (unsigned long)size, st.wYear, st.wMonth, st.wDay,
                   st.wHour, st.wMinute);
    else
        _snprintf(out, cap - 1, "%7lu bytes", (unsigned long)size);
    out[cap - 1] = 0;
}

/* Для образів розміру в списку немає: він однаковий у всіх (720 КБ) і в
   колонці був би самим лише шумом. Лишається час останнього запису. */
static void stamp_date(const FILETIME *ft, char *out, size_t cap)
{
    SYSTEMTIME st;
    FILETIME lft;
    if (FileTimeToLocalFileTime(ft, &lft) && FileTimeToSystemTime(&lft, &st))
        _snprintf(out, cap - 1, "%04u-%02u-%02u %02u:%02u",
                   st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute);
    else
        out[0] = 0;
    out[cap - 1] = 0;
}

/* впорядкувати за іменем: FindFirstFile порядку не гарантує, а оператор
   очікує 000, 001, 002 підряд */
static void sort_items(MItem *it, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        MItem t = it[i];
        for (j = i - 1; j >= 0 && _stricmp(it[j].disp, t.disp) > 0; j--)
            it[j + 1] = it[j];
        it[j + 1] = t;
    }
}

/* усі *.IMG поряд */
static int collect_images(const char *dir, MItem *it, int max)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;
    _snprintf(pat, MAX_PATH - 1, "%s\\*.img", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (fd.nFileSizeHigh || strlen(fd.cFileName) > 90)
            continue;
        if (n >= max)
            break;
        strcpy(it[n].name, fd.cFileName);
        strcpy(it[n].disp, fd.cFileName);
        stamp_date(&fd.ftLastWriteTime, it[n].info, 48);
        {   /* коментар з ABOUT_ME.TXT; немає файлу — колонка лишається порожня */
            char full[MAX_PATH];
            static char note[NOTE_MAX + 1];
            _snprintf(full, MAX_PATH - 1, "%s\\%s", dir, fd.cFileName);
            full[MAX_PATH - 1] = 0;
            it[n].nlines = read_about(full, note, NOTE_MAX + 1)
                         ? wrap_note(note, it[n].nline, NOTE_MAXL) : 0;
        }
        it[n].open = 0;
        it[n].done = 0;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    sort_items(it, n);
    return n;
}

/* Кандидати на пакування: ім'я лягає в 8.3, розмір не більший за maxsize.
   Малі латинські літери приймаємо і показуємо великими — у цьому й уся
   поблажка; в образ ім'я теж піде великими.
   .EXE відкидаємо: це наші ж утиліти, на дискеті стійки їм нічого робити.
   Образи відсіюються самі — 737280 Б більше за 360 КБ. */
static int collect_files(const char *dir, MItem *it, int max, DWORD maxsize)
{
    char pat[MAX_PATH];
    WIN32_FIND_DATAA fd;
    HANDLE h;
    int n = 0;
    _snprintf(pat, MAX_PATH - 1, "%s\\*", dir);
    pat[MAX_PATH - 1] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE)
        return 0;
    do {
        char up[96];
        size_t i, len;
        const char *dot;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            continue;
        if (fd.nFileSizeHigh || fd.nFileSizeLow > maxsize)
            continue;
        len = strlen(fd.cFileName);
        if (len == 0 || len > 12)
            continue;
        for (i = 0; i <= len; i++) {
            char c = fd.cFileName[i];
            up[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
        }
        if (name_83_check(up) != NULL)
            continue;
        dot = strrchr(up, '.');
        if (dot && _stricmp(dot, ".EXE") == 0)
            continue;
        if (n >= max)
            break;
        strcpy(it[n].name, fd.cFileName);
        strcpy(it[n].disp, up);
        stamp_info(&fd.ftLastWriteTime, fd.nFileSizeLow, it[n].info, 48);
        it[n].nlines = 0;
        it[n].open = 0;
        it[n].done = 0;
        n++;
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    sort_items(it, n);
    return n;
}

#endif /* CONMENU_H */
