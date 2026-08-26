# Документація на стійку РВ-001

Оригінальні мануали Siemens. Покладені сюди 2026-08-05, коли стійку нарешті
ідентифікували: це **SINUMERIK 810 GA3 / 820 GA3**.

| Файл | Що це | Звідки брали |
|---|---|---|
| `338_810_820_GA3_Installation_Instructions.pdf` | Installation Instructions, вид. 01.93 | розд. 10.9 Block transfer (BTR), розд. 9 сетинг-дані 5010–5029, розд. 11.7 аларми V.24 |
| `UNIV_SS_1295_en.pdf` | System 800 Universal Interface, Planning Guide, вид. 12.95 | розд. 5.2.1 значення бітів SD, розд. 6.1/6.2 тайминг ліній, розд. 7.1.4 роз'єми |
| `373_810T_820T_GA3_Programming.pdf` | 810T/820T GA3 Programming, вид. 09.91 | розд. 1.6.3 Leader, розд. 1.7 формат програми |
| `368_810T_GA3_Operating.pdf` | Operator's Guide 810T GA3, вид. 01.93 | єдиний, де описані органи керування: розд. 2.1.1.6 і 2.1.2.1 `Einzelsatz`, розд. 2.5 глосарій софткнопок, розд. 3.2.3 `PROGRAMMBEEINFLUSSUNG` |

Прямі посилання на те, що вже лежить тут:

* `368` — `https://cache.industry.siemens.com/dl/files/438/21903438/att_26661/v1/368_810T_GA3_Operating.pdf`

Не качали, лежить як запас:
`https://cache.industry.siemens.com/dl/files/648/21903648/att_7232/v1/371_810G_820G_GA3_Programming_Guide.pdf`
(810G/820G GA3 Programming Guide, вид. 05.92).

## Як їх качати й читати

Прямі посилання — на `cache.industry.siemens.com`. Через
`support.industry.siemens.com` ті самі файли віддають **403**, це не блокування
за регіоном, просто інший хост:

```
curl -sSL -A "Mozilla/5.0" -o FILE.pdf https://cache.industry.siemens.com/dl/files/...
```

Розбирати `pdftotext -layout`. Дві пастки:

* титульні сторінки й діаграми виходять як шум із літер `a` — це нормально,
  шукати треба grep-ом по тексту, а не гортати;
* **таблиці бітів читати без схлопування пробілів.** Якщо прогнати через
  `sed 's/ \{4,\}/ /g'`, колонки зʼїжджають і біти читаються не ті. Саме так
  виглядають таблиці SD 5016/5017 — вісім підписів у два-три яруси над номерами
  бітів.
