#!/usr/bin/env python3
"""
restore_with_instructions — повний цикл відновлення Sinumerik РВ-001:
покроковий майстер підготовки на пульті, далі заливка шести файлів.

Показує оператору екран стійки в псевдографіці, під ним — що натиснути,
і блимає NEXT. Оператор робить дію на стійці й тисне Enter на лаптопі;
екран перемальовується наступним кроком.

ДВА ЗАХОДИ, а не один (2026-08-06). `MD 5`, `MD 8`, `MD 12` і `MD 20`
не активуються ні ребутом, ні Power On — лише софткнопками `FORMAT USER M.`
+ `CLEAR PART PR.` (мануал, розд. 5.1, стор. 5–2). Стара процедура
форматувала памʼять ДО того, як приходив TEA1, тож наші машинні дані не
діяли жодного разу. Тепер: TEA1 → перезапуск → форматування вже з ним →
решта пʼяти файлів.

Тексти для оператора — болгарською. Написи на «екрані» відтворені
посимвольно зі знімків пульта, без перекладу.

  ./restore_with_instructions.py          весь цикл: майстер + заливка
  ./restore_with_instructions.py --demo   лише прокрутити екрани, без заливки
"""
import os, select, subprocess, sys, time

W = 60                      # ширина «екрана» всередині рамки
KW = 11                     # ширина комірки софткнопки
ESC_CLEAR = "\033[2J\033[H"
BOLD, DIM, INV, OFF = "\033[1m", "\033[2m", "\033[7m", "\033[0m"


def render(body, keys, point=None):
    """body — рядки екрана; keys — 5 пар (верх, низ); point — номер кнопки 1..5."""
    out = ["    ╔" + "═" * W + "╗"]
    for line in body:
        out.append("    ║" + line.ljust(W)[:W] + "║")
    out.append("    ╟" + "┬".join("─" * KW for _ in range(5))
               + "─" * (W - 5 * KW - 4) + "╢")
    for row in (0, 1):
        cells = [k[row].ljust(KW)[:KW] for k in keys]
        out.append("    ║" + "│".join(cells) + " " * (W - 5 * KW - 4) + "║")
    out.append("    ╚" + "═" * W + "╝")

    # Физическите бутони под екрана. Вляво от петте стои  ∧  — връща
    # от подменю в главното меню. Вдясно стои  > .
    row = [" "] * 76
    lab = [" "] * 76

    def put(dst, col, s):
        for j, ch in enumerate(s):
            dst[col + j] = ch

    put(row, 1, "[∧]")
    put(lab, 0, "назад")
    for i in range(5):
        col = 5 + i * (KW + 1) + 3
        put(row, col, "▄▄▄▄▄")
        put(lab, col + 2, str(i + 1))
    put(row, 5 + 5 * (KW + 1) - 1, "▄▄▄")
    out.append("  " + "".join(row).rstrip())
    out.append("  " + "".join(lab).rstrip())
    if point:
        col = 5 + (point - 1) * (KW + 1) + 3
        out.append("  " + " " * col + BOLD + "▲▲▲▲▲" + OFF)
        out.append("  " + " " * col + BOLD + f"бутон {point}" + OFF)
    return out


# ─────────────────────────────────────────────────────── екрани стійки
HEAD = " 1    ORD 1 Batteriealarm-Netzgeraet                    - K1"

MAIN_BODY = [
    HEAD,
    "INBETRIEBN. URLOESCHEN",
    "3GE 570.822.9102.23",
    "",
    "     DATEN EIN-AUSGABE",
    "",
    "     NC ANWENDERDATEN",
    "",
    "     PLC INITIAL./PLC-PROGRAMM LADEN",
    "",
    "     MASCHINENDATEN LOESCHEN / LADEN",
    "",
    "     INBETRIEBN. ENDE, KENNW. LOESCHEN",
]
MAIN_KEYS = [("DATEN", "EIN-AUS"), ("NC", "DATEN"), ("PLC", "INITIAL"),
             ("MASCH.-", "DATEN"), ("INBETR.  >", "ENDE KW")]

NC_BODY = [
    HEAD,
    "NC URLOESCHEN",
    "     ┌──────────────────────────────────────────┐",
    "     │ Fuer die folgenden Funktionen erst       │",
    "     │  die Maschinendaten eingeben !           │",
    "     └──────────────────────────────────────────┘",
    "",
    "  ✓ ANWENDERSPEICHER FORMATIEREN",
    "",
    "  ✓ TEILEPROGRAMMSPEICHER LOESCHEN",
    "",
    "  ✓ ALARM-TEXTSPEICHER FORMATIEREN",
    "",
]
NC_KEYS = [("AWS", "FORMAT."), ("TEILEP.", "LOESCH."), ("FORMAT.", "AL-TEXT"),
           ("", ""), ("", "")]

MD_BODY = [
    HEAD,
    "MASCHINENDATEN LOESCHEN / LADEN",
    "",
    "  ✓ NC MASCHINENDATEN LOESCHEN",
    "",
    "  ✓ NC STANDARD MASCHINENDATEN LADEN",
    "",
    "  ✓ PLC MASCHINENDATEN LOESCHEN",
    "",
    "  ✓ PLC STANDARD MASCHINENDATEN LADEN",
    "",
    "    MD AUS ASM LADEN",
    "",
]
MD_KEYS = [("NC-MD", "LOESCH."), ("NC-MD", "LADEN"), ("PLC-MD", "LOESCH."),
           ("PLC-MD", "LADEN"), ("MD-ASM", "LADEN")]

PLC_BODY = [
    HEAD,
    "PLC INITIAL./PLC-PROGRAMM LADEN",
    "",
    "  ✓ PLC URLOESCHEN",
    "",
    "  ✓ REMANENTE MERKER LOESCHEN",
    "",
    "    PLC-PROGRAMM LADEN",
    "",
    "",
    "",
    "",
    "",
]
PLC_KEYS = [("PLC", "URLOE."), ("MERKER", "LOESCH."), ("", ""),
            ("", ""), ("ASM-PR", "LADEN")]

SET_BODY = [
    HEAD,
    "SETTINGDATEN BITS",
    "",
    "     5000  00000000        5001  00000000",
    "     5002  00000000        5003  00000000",
    "     5004  00000000        5005  00000000",
    "     5006  00000000        5007  00000000",
    "     5008  00000000        5009  00000000",
    "     5010  00000000        5011  00000111",
    "     5012  00000001        5013  00000111",
    "     5014  00010001        5015  10010011",
    "     5016  00000000        5017  00000000",
    "     5018  00000000        5019  00000000",
]
SET_KEYS = [("DATEN-", "EINGABE"), ("DATEN-", "AUSGABE"), ("", ""),
            ("", ""), ("", "")]

EIN_BODY = [
    HEAD,
    "DATENEINGABE",
    "",
    "Freier Speicher:      28360  Zeichen",
    "",
    "Datenart:",
    "",
    "Eingabeschnittstelle:        1",
    "",
    "Schnittstellenzuordnung:     1=RTS-LINE",
    "                             2=RTS-LINE",
    "",
    "",
]
EIN_KEYS = [("", ""), ("", ""), ("", ""), ("START", ""), ("STOP", "")]


# ─────────────────────────────────────────────────────────── кроки
# Екран без стійки: момент, коли на неї ще не дивляться.
BOOT_BODY = [
    "",
    "        ┌────────────────────────────────────────┐",
    "        │                                        │",
    "        │            OVERALL RESET               │",
    "        │                                        │",
    "        └────────────────────────────────────────┘",
    "",
    "",
    "",
    "",
    "",
    "",
    "",
]
BOOT_KEYS = [("", ""), ("", ""), ("", ""), ("", ""), ("", "")]

CHECK_BODY = [
    HEAD,
    "DATENEINGABE",
    "",
    "Freier Speicher:      88972  Zeichen      <-- ТАЗИ ЦИФРА",
    "",
    "Datenart:",
    "",
    "Eingabeschnittstelle:        1",
    "",
    "Schnittstellenzuordnung:     1=RTS-LINE",
    "                             2=RTS-LINE",
    "",
    "",
]


# ─────────────────────────────────────────── заход 1: машинни данни
STEPS1 = [
    (BOOT_BODY, BOOT_KEYS, None, "ВЛИЗАНЕ В РЕЖИМА", [
        "Изключи стойката.",
        "",
        "Задръж бутона с ОКОТО и я включи, без да го пускаш.",
        "Дръж го, докато на екрана се появи  OVERALL RESET.",
        "",
        "Батерията НЕ се вади. Влизането тук не трие нищо —",
        "трият само изрично натиснатите софтбутони.",
    ]),
    (MAIN_BODY, MAIN_KEYS, 2, "ГЛАВНО МЕНЮ", [
        "Натисни бутон  2  —  [NC DATEN]",
    ]),
    (NC_BODY, NC_KEYS, None, "NC URLOESCHEN", [
        "Сложи трите отметки:  натисни бутони  1 ,  2  и  3 .",
        "",
        "Провери, че пред трите реда стои  ✓ . И трите, преди",
        "да излезеш от менюто.",
        "",
        "Надписът в рамката — «първо въведи машинните данни» —",
        "не е декорация. Точно заради него правим втори заход.",
    ]),
    (MAIN_BODY, MAIN_KEYS, 4, "ГЛАВНО МЕНЮ", [
        "Първо се върни в главното меню:  натисни  [∧] .",
        "",
        "После натисни бутон  4  —  [MASCH.-DATEN]",
    ]),
    (MD_BODY, MD_KEYS, None, "MASCHINENDATEN LOESCHEN / LADEN", [
        "Сложи четирите отметки:  бутони  1 ,  2 ,  3  и  4 .",
        "",
        "Бутон  5  [MD-ASM LADEN]  НЕ се пипа.",
        "",
        "Внимание: тези бутони трият и битовете на опциите.",
        "Затова във ВТОРИЯ заход това меню не се пипа изобщо.",
    ]),
    (MAIN_BODY, MAIN_KEYS, 3, "ГЛАВНО МЕНЮ", [
        "Върни се с  [∧] , после натисни бутон  3  —  [PLC INITIAL]",
    ]),
    (PLC_BODY, PLC_KEYS, None, "PLC INITIAL./PLC-PROGRAMM LADEN", [
        "Сложи двете отметки:  бутони  1  и  2 .",
        "",
        "Бутон  5  [ASM-PR LADEN]  НЕ се пипа.",
    ]),
    (MAIN_BODY, MAIN_KEYS, 1, "ГЛАВНО МЕНЮ", [
        "Върни се с  [∧] , после натисни бутон  1  —  [DATEN EIN-AUS]",
    ]),
    (SET_BODY, SET_KEYS, None, "SETTINGDATEN BITS", [
        "Работи се с двете панели ВДЯСНО от дисплея: стрелките",
        "за движение, цифровата клавиатура за въвеждане.",
        "",
        "   5011  <-  111           5012  <-  1",
        "   5013  <-  111           5014  <-  10001",
        "                           5015  <-  10010011",
        "",
        "Първите две са за зареждането. Последните три са за",
        "четене на програми ОБРАТНО към лаптопа.",
    ]),
    (SET_BODY, SET_KEYS, 1, "SETTINGDATEN BITS", [
        "Натисни бутон  1  —  [DATEN-EINGABE]",
    ]),
    (EIN_BODY, EIN_KEYS, None, "DATENEINGABE", [
        "В поле  «Eingabeschnittstelle»  въведи  1 .",
        "Това е кабелът към лаптопа.",
        "",
        "«Schnittstellenzuordnung: 1=RTS-LINE»  си стои така.",
    ]),
    (EIN_BODY, EIN_KEYS, 4, "DATENEINGABE", [
        "Подготовката на първия заход свърши.",
        "",
        "Сега тръгва ЕДИН файл — TEA1, машинните данни.",
        "Лаптопът ще мига  >>> PUSH START >>>  — тогава",
        "натискаш  [START] . Върви около минута.",
        "",
        "Натисни ENTER тук, за да почне.",
    ]),
]


# ─────────────────────────────── между заходите: батерия и рестарт
STEPS2 = [
    (BOOT_BODY, BOOT_KEYS, None, "БАТЕРИЯ И РЕСТАРТ", [
        "1.  СЛОЖИ БАТЕРИЯТА — сега, преди изключване.",
        "    Без нея машинните данни се губят при спиране на",
        "    тока и целият пръв заход отива на вятъра.",
        "",
        "2.  Изключи стойката.",
        "",
        "3.  Задръж бутона с ОКОТО и включи, без да го пускаш,",
        "    докато не се появи  OVERALL RESET.",
    ]),
    (MAIN_BODY, MAIN_KEYS, 1, "ГЛАВНО МЕНЮ", [
        "В този заход НИЩО в менютата не се пипа.",
        "",
        "Рестартът след TEA1 сам форматира паметта, и то вече",
        "по НАШИТЕ машинни данни. Другите нулирания са направени",
        "в първия заход и стигат веднъж. Затова:",
        "",
        "   бутон  2   [NC DATEN]       —  НЕ",
        "   бутон  3   [PLC INITIAL]    —  НЕ",
        "   бутон  4   [MASCH.-DATEN]   —  НЕ",
        "",
        "Последният е и опасен: той трие машинните данни, които",
        "току-що заредихме, а с тях и битовете на опциите.",
        "",
        "Натисни бутон  1  —  [DATEN EIN-AUS] , после  [DATEN-EINGABE]",
    ]),
    (CHECK_BODY, EIN_KEYS, None, "ПРОВЕРКА — ЕДНА ЦИФРА", [
        "Погледни  «Freier Speicher»:",
        "",
        "   ≈ 88 972    ✓  форматирането видя нашите MD",
        "   ≈ 118 910   ✓  същото, но при MD 20 = 0",
        "   ≈ 28 360    ✗  форматирано по чужди данни — СПРИ",
        "   0           ✗  паметта не е форматирана — СПРИ",
        "",
        "При грешна цифра не продължавай: почни от заход 1.",
    ]),
    (SET_BODY, SET_KEYS, None, "SETTINGDATEN BITS", [
        "Форматирането изтри сетинг данните. Въведи ги пак:",
        "",
        "   5011  <-  111           5012  <-  1",
        "   5013  <-  111           5014  <-  10001",
        "                           5015  <-  10010011",
    ]),
    (EIN_BODY, EIN_KEYS, 4, "DATENEINGABE", [
        "В поле  «Eingabeschnittstelle»  въведи  1 .",
        "",
        "Сега тръгват останалите ПЕТ файла:",
        "   TEA2 · PCP · PCA-en · LP · asm_dat",
        "",
        "За всеки от тях лаптопът мига  >>> PUSH START >>>  —",
        "тогава натискаш  [START] . Последният, asm_dat, върви",
        "няколко минути. Това НЕ е забиване.",
        "",
        "Натисни ENTER тук, за да почне.",
    ]),
]


FINAL = [
    "",
    "   ГОТОВО.  Всичките шест файла са предадени.",
    "",
    "   ▸  Главно меню → [INBETR. ENDE KW]  (бутон 5)",
    "      Стойката прави POWER ON RESET и влиза в нормален режим.",
    "",
    "   СЛЕД ТОВА — БЕЗ ТОВА МАШИНАТА НЕ РАБОТИ:",
    "",
    "   1. ОГРАНИЧЕНИЕ НА РАБОТНАТА ЗОНА",
    "      [SETTINGDATEN] → [AXIAL] , за всяка ос мин и макс.",
    "      Ако стоят нули — аларм 172*/176* и NC START е блокиран.",
    "      Реалните ходове (от TEA1):",
    "         ос 1:  +773000   -39500",
    "         ос 2:  +426000   -47000",
    "         ос 3:  +45000    -329500",
    "         ос 4:  +99999999 -99999999   въртяща, без граница",
    "",
    "   2. ИЗХОД В НУЛЕВА ТОЧКА за всяка ос (REFPOINT).",
    "      Софтуерните крайни изключватели работят само след него.",
    "",
    "   3. КОРЕКЦИИ НА ИНСТРУМЕНТИТЕ, НУЛЕВИ ОТМЕСТВАНИЯ (G54..G57),",
    "      R-параметри. Форматирането ги изтри всичките.",
    "",
    "   4. ПРОВЕРКА:  ./send test/test4.nc   после [START]",
    "                 ./recv --dc1           после [DATEN-AUSGABE]",
    "",
    "   Подробно:  instrukcia.txt , раздел СЛЕД ПРОЦЕДУРАТА",
    "",
]


def box(lines, w=64):
    out = ["  ╔" + "═" * w + "╗"]
    for l in lines:
        out.append("  ║" + l.ljust(w)[:w] + "║")
    out.append("  ╚" + "═" * w + "╝")
    return out


def draw(step, n, total, phase):
    body, keys, point, title, lines = step
    out = [ESC_CLEAR]
    out.append(f"  {BOLD}РВ-001 · {phase}{OFF}"
               f"{DIM}      стъпка {n} от {total}{OFF}")
    out.append("")
    out.append(f"  {DIM}на стойката:{OFF}  {BOLD}{title}{OFF}")
    out.append("")
    out += render(body, keys, point)
    out.append("")
    for l in lines:
        out.append("    " + l)
    sys.stdout.write("\n".join(out) + "\n")


def wait_next():
    """Блимає NEXT, поки оператор не натисне Enter."""
    frame = 0
    sys.stdout.write("\n")
    while True:
        on = (frame // 2) % 2
        tag = f"{INV}{BOLD}  >>>  NEXT  >>>  {OFF}" if on else "                  "
        sys.stdout.write(f"\r    {tag}   {DIM}готово? натисни ENTER"
                         f"   (q = изход){OFF}   ")
        sys.stdout.flush()
        r, _, _ = select.select([sys.stdin], [], [], 0.4)
        if r:
            line = sys.stdin.readline()
            sys.stdout.write("\n")
            return line.strip().lower() not in ("q", "quit", "изход")
        frame += 1


def walk(steps, phase, demo):
    total = len(steps)
    for n, step in enumerate(steps, 1):
        draw(step, n, total, phase)
        if demo:
            print()
            continue
        if not wait_next():
            print("\n  прекъснато\n")
            return False
    return True


def run_pass(restore, which, title, port=None):
    """Один захід заливки. Екран НЕ чиститься після нього: звіти по файлах
    мусять лишитись на очах в оператора."""
    sys.stdout.write(ESC_CLEAR)
    print(f"  {BOLD}РВ-001 · {title}{OFF}\n")
    sys.stdout.flush()          # без цього підпроцес пише поперед нас у лог
    cmd = [restore, which]
    if port:
        cmd += ["--port", port]
    return subprocess.call(cmd)


def port_from_argv(argv):
    """--port /dev/ttyS0 або --port=/dev/ttyS0. Без нього порт шукається сам,
    і лише якщо він у системі один."""
    for i, a in enumerate(argv):
        if a == "--port":
            if i + 1 >= len(argv):
                sys.exit("--port needs a value")
            return argv[i + 1]
        if a.startswith("--port="):
            return a.split("=", 1)[1]
    return None


def main():
    demo = "--demo" in sys.argv
    port = port_from_argv(sys.argv[1:])

    here = os.path.dirname(os.path.abspath(__file__))
    restore = os.path.join(here, "restore.sh")
    if not demo and not os.access(restore, os.X_OK):
        print(f"\n  няма изпълним  {restore}\n")
        return 1

    if not walk(STEPS1, "заход 1 от 2 · подготовка", demo):
        return 1
    if not demo:
        rc = run_pass(restore, "1", "заход 1 · предаване на TEA1", port)
        if rc != 0:
            print()
            for l in box(["", "   ПРЕДАВАНЕТО СПРЯ.", "",
                          "   TEA1 не е минал докрай.",
                          "   НЕ слагай батерията и почни отначало.", ""]):
                print(l)
            print()
            return rc

    if not walk(STEPS2, "заход 2 от 2 · подготовка", demo):
        return 1
    if demo:
        return 0

    rc = run_pass(restore, "2", "заход 2 · останалите пет файла", port)
    if rc != 0:
        print()
        for l in box(["", "   ПРЕДАВАНЕТО СПРЯ.", "",
                      "   Виж по-горе на кой файл се спря.",
                      "   Машинните данни от заход 1 са запазени —",
                      "   батерията е сложена, могат да се доизкарат.", ""]):
            print(l)
        print()
        return rc

    print()
    for l in box(FINAL):
        print(l)
    print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
