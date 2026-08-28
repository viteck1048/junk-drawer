#!/usr/bin/python3
# -*- coding: utf-8 -*-
"""
Збирає dist/portal_ua з launcher.py + uk.map + чинного yafti.yml.

У готовий скрипт запікається знімок англійських рядків: якщо після
оновлення образу Bazzite оригінал змінився, портал покаже англійський
текст замість застарілого перекладу, а `portal_ua --report` скаже, що
саме розійшлося. Після оновлення образу достатньо перезапустити цей
скрипт.
"""

import os
import pprint
import sys

import yaml

HERE = os.path.dirname(os.path.abspath(__file__))
LAUNCHER = os.path.join(HERE, "launcher.py")
MAPFILE = os.path.join(HERE, "uk.map")
OUTPUT = os.path.join(HERE, "dist", "portal_ua")
UPSTREAM_CFG = "/usr/share/yafti/yafti.yml"


def load_map():
    translations = {}
    with open(MAPFILE, "r", encoding="utf-8") as handle:
        for number, line in enumerate(handle, 1):
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            if " = " not in line:
                sys.exit("uk.map:%d: рядок без роздільника ' = ': %s"
                         % (number, line))
            key, value = line.split(" = ", 1)
            key = key.strip()
            if key in translations:
                sys.exit("uk.map:%d: повторний ключ %s" % (number, key))
            translations[key] = value
    return translations


def collect_english(cfg):
    """Ключ -> англійський текст із чинного yafti.yml."""
    english = {}
    labels = []
    for index, screen in enumerate(cfg.get("screens") or []):
        if screen.get("title"):
            english["scr:%d:title" % index] = screen["title"]
        if screen.get("description"):
            english["scr:%d:desc" % index] = screen["description"]
        for action in screen.get("actions") or []:
            action_id = action.get("id") or ""
            if action.get("title"):
                english["act:%s:title" % action_id] = action["title"]
            if action.get("description"):
                english["act:%s:desc" % action_id] = action["description"]
            for option in action.get("options") or []:
                if option.get("label"):
                    labels.append(option["label"])
    return english, sorted(set(labels))


def main():
    translations = load_map()
    with open(UPSTREAM_CFG, "r", encoding="utf-8") as handle:
        cfg = yaml.safe_load(handle) or {}

    english, labels = collect_english(cfg)

    table = {}
    missing = []
    for key, source in english.items():
        if key in translations:
            table[key] = (source, translations[key])
        else:
            missing.append((key, source))

    options = {}
    for label in labels:
        key = "opt:" + label
        if key in translations:
            options[label] = translations[key]
        else:
            missing.append((key, label))

    known = set(english) | {"opt:" + label for label in labels}
    stale = sorted(set(translations) - known)

    with open(LAUNCHER, "r", encoding="utf-8") as handle:
        src = handle.read()
    if "# @@DATA@@" not in src:
        sys.exit("launcher.py: не знайдено маркер '# @@DATA@@'")

    data = ("TR = " + pprint.pformat(table, width=100, sort_dicts=True)
            + "\n\nOPT = " + pprint.pformat(options, width=100, sort_dicts=True))
    src = src.replace("# @@DATA@@", data)

    os.makedirs(os.path.dirname(OUTPUT), exist_ok=True)
    with open(OUTPUT, "w", encoding="utf-8") as handle:
        handle.write(src)
    os.chmod(OUTPUT, 0o755)

    print("Зібрано %s" % OUTPUT)
    print("  перекладено рядків конфігурації: %d" % len(table))
    print("  перекладено кнопок: %d з %d" % (len(options), len(labels)))
    if missing:
        print("\n  БЕЗ ПЕРЕКЛАДУ (%d) — показуватимуться англійською:"
              % len(missing))
        for key, text in missing:
            print("    %s = %s" % (key, text))
    if stale:
        print("\n  ЗАЙВЕ В uk.map (%d) — таких рядків уже немає в yafti.yml:"
              % len(stale))
        for key in stale:
            print("    %s" % key)


if __name__ == "__main__":
    main()
