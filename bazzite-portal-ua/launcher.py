#!/usr/bin/python3
# -*- coding: utf-8 -*-
"""
Портал Bazzite — українська локалізація.

Це НЕ форк: під час запуску скрипт читає штатні
    /usr/bin/yafti_gtk.py        (сам застосунок)
    /usr/share/yafti/yafti.yml   (список дій)
накладає на них переклад у пам'яті й запускає результат.
Отже, після оновлення образу нові дії Bazzite з'являються самі —
просто англійською, доки їх не додано до uk.map.

Згенеровано з launcher.py + uk.map скриптом build.py — правити треба
ті файли, а не цей.

    portal_ua                — запустити портал українською
    portal_ua --no-commands  — без рядків з командами під пунктами
    portal_ua --report       — показати, що лишилось неперекладеним
    portal_ua --english      — запустити оригінал без перекладу

Під кожним пунктом видно команду, яку він виконає; наведення показує її
повністю, ПКМ (або кнопка у вікні дії) відкриває вікно з повним скриптом
і — головне — з тілом рецепта ujust, де й ховається справжня робота.
"""

import os
import sys
import tempfile
import atexit

import yaml

UPSTREAM_APP = "/usr/bin/yafti_gtk.py"
UPSTREAM_CFG = "/usr/share/yafti/yafti.yml"

# ─────────────────────────────────────────────────────────────────────────
# Код, що дописується в кінець yafti_gtk.py: перегляд команд без виконання.
# Оформлений як monkey-patch методів, а не як правка тексту, — так він
# переживає переписування upstream'ом усього, крім імен методів.
# ─────────────────────────────────────────────────────────────────────────
ADDON = r'''

# ─── portal_ua: що саме виконає ця дія ───────────────────────────────────
import re as _ua_re
import subprocess as _ua_sub

_UA_JUSTFILE = "/usr/share/ublue-os/justfile"
_UA_SHOW_COMMANDS = os.environ.get("PORTAL_UA_COMMANDS", "1") != "0"
_ua_recipe_cache = {}


def _ua_clean(script):
    """Згорнути script: до того, що справді робиться."""
    text = (script or "").strip()
    match = _ua_re.match(r"^yad\s.*?&&\s+(.*)$", text, _ua_re.S)
    if match:
        text = match.group(1).strip()
    text = _ua_re.sub(r";\s*status=\$\?;.*$", "", text, flags=_ua_re.S)
    text = _ua_re.sub(r"^nohup\s+setsid\s+", "", text)
    text = _ua_re.sub(r"\s*>\s*&\s*/dev/null.*$", "", text)
    text = _ua_re.sub(r"\s*(1|2)?>\s*/dev/null(\s*2>&1)?\s*$", "", text)
    return _ua_re.sub(r"\s+", " ", text.replace("\n", " ")).strip()


def _ua_scripts(action):
    """[(підпис, команда)] — усе, що ця дія може виконати."""
    items = []
    if (action.get("script") or "").strip():
        items.append(("", action["script"].strip()))
    for option in action.get("options") or []:
        if (option.get("script") or "").strip():
            items.append((option.get("label") or "", option["script"].strip()))
    return items


def _ua_summary(action):
    """Один рядок під заголовком у списку."""
    cleaned = [_ua_clean(script) for _, script in _ua_scripts(action)]
    cleaned = [text for text in cleaned if text]
    if not cleaned:
        return ""
    if len(cleaned) == 1:
        return cleaned[0]
    words = cleaned[0].split()
    for other in cleaned[1:]:
        parts = other.split()
        keep = 0
        while keep < min(len(words), len(parts)) and words[keep] == parts[keep]:
            keep += 1
        words = words[:keep]
    prefix = " ".join(words).strip()
    if prefix:
        return prefix + " …"
    return cleaned[0] + "  (та інші)"


def _ua_tooltip(action):
    lines = []
    for label, script in _ua_scripts(action):
        if label:
            lines.append("[%s]" % label)
        lines.append(_ua_clean(script))
    status = (action.get("status_script") or "").strip()
    if status:
        lines.append("")
        lines.append("перевірка стану: " + _ua_clean(status))
    lines.append("")
    lines.append("ПКМ — показати повний скрипт і рецепт ujust")
    return "\n".join(lines)


def _ua_recipe_names(text):
    found = _ua_re.findall(r"\bujust\s+([A-Za-z0-9][A-Za-z0-9_-]*)", text or "")
    return list(dict.fromkeys(found))


def _ua_recipe_body(name):
    """Тіло рецепта ujust — саме там ховається справжня робота."""
    if name not in _ua_recipe_cache:
        body = None
        try:
            result = _ua_sub.run(
                ["just", "--justfile", _UA_JUSTFILE, "--working-directory", "/",
                 "--show", name],
                capture_output=True, text=True, timeout=10)
            if result.returncode == 0 and result.stdout.strip():
                body = result.stdout.rstrip()
        except Exception:
            body = None
        _ua_recipe_cache[name] = body
    return _ua_recipe_cache[name]


def _ua_mono(text, small=False):
    label = Gtk.Label()
    markup = "<tt>%s</tt>" % escape_markup(text)
    if small:
        markup = "<small>%s</small>" % markup
        label.add_css_class("dim-label")
        label.set_wrap(True)
        label.set_max_width_chars(110)
    label.set_markup(markup)
    label.set_xalign(0)
    label.set_selectable(not small)
    return label


def _ua_heading(text):
    label = Gtk.Label()
    label.set_markup("<b>%s</b>" % escape_markup(text))
    label.set_xalign(0)
    return label


def _ua_show_preview(parent, action):
    """Вікно «що виконається» — нічого не запускає."""
    window = Gtk.Window(title="Що виконає: %s" % action.get("title", ""))
    window.set_transient_for(parent)
    window.set_modal(True)
    window.set_default_size(820, 580)

    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=12)
    set_widget_margins(box, 16, 16, 16, 16)

    everything = ""
    scripts = _ua_scripts(action)
    for label, script in scripts:
        box.append(_ua_heading(label or "Команда"))
        box.append(_ua_mono(script))
        everything += "\n" + script

    status = (action.get("status_script") or "").strip()
    if status:
        box.append(_ua_heading("Перевірка стану (виконується сама, без вашого кліку)"))
        box.append(_ua_mono(status))
        everything += "\n" + status

    if not everything.strip():
        box.append(_ua_mono("Ця дія нічого не виконує."))

    for name in _ua_recipe_names(everything):
        body = _ua_recipe_body(name)
        box.append(_ua_heading("Рецепт «ujust %s» — те, що виконається насправді" % name))
        box.append(_ua_mono(body or "не вдалося прочитати (just --show %s)" % name))

    scrolled = Gtk.ScrolledWindow()
    scrolled.set_vexpand(True)
    scrolled.set_child(box)

    root = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=0)
    root.append(scrolled)

    close_button = Gtk.Button(label="Закрити")
    set_widget_margins(close_button, 8, 12, 12, 12)
    close_button.connect("clicked", lambda *_ignored: window.destroy())
    root.append(close_button)

    keys = Gtk.EventControllerKey.new()
    keys.connect("key-pressed",
                 lambda _c, keyval, *_a: window.destroy() or True
                 if keyval == 65307 else False)
    window.add_controller(keys)

    window.set_child(root)
    window.present()


_ua_orig_create_action_item = YaftiGTK.create_action_item


def _ua_create_action_item(self, action):
    frame = _ua_orig_create_action_item(self, action)
    try:
        button = frame.get_child()
        button.set_tooltip_text(_ua_tooltip(action))

        gesture = Gtk.GestureClick.new()
        gesture.set_button(3)
        gesture.connect("pressed",
                        lambda *_a, act=action: _ua_show_preview(self, act))
        button.add_controller(gesture)

        if _UA_SHOW_COMMANDS:
            summary = _ua_summary(action)
            if summary:
                button.get_child().get_first_child().append(
                    _ua_mono(summary, small=True))
    except Exception:
        pass
    return frame


YaftiGTK.create_action_item = _ua_create_action_item

_ua_orig_dialog_content = YaftiGTK.build_action_dialog_content


def _ua_build_action_dialog_content(self, state, status_token,
                                    status_timed_out=False):
    _ua_orig_dialog_content(self, state, status_token, status_timed_out)
    try:
        action = state["action"]
        root = state["dialog"].get_child()

        # actions_box — єдиний Gtk.Box серед дітей діалогу
        actions_box = None
        child = root.get_first_child()
        while child is not None:
            if isinstance(child, Gtk.Box):
                actions_box = child
                break
            child = child.get_next_sibling()
        if actions_box is None:
            return

        commands = {}
        for option in self.get_action_options(action):
            command = _ua_clean(option.get("script"))
            if option.get("label") and command:
                commands[option["label"]] = command

        pending = []
        button = actions_box.get_first_child()
        while button is not None:
            label = button.get_label() if isinstance(button, Gtk.Button) else None
            if label in commands:
                button.set_tooltip_text(commands[label])
                if _UA_SHOW_COMMANDS:
                    pending.append((button, commands[label]))
            button = button.get_next_sibling()
        for button, command in pending:
            actions_box.insert_child_after(_ua_mono(command, small=True), button)

        preview_button = Gtk.Button(label="Показати, що виконається…")
        preview_button.connect("clicked",
                               lambda *_a: _ua_show_preview(self, action))
        root.insert_child_after(preview_button, actions_box)
    except Exception:
        pass


YaftiGTK.build_action_dialog_content = _ua_build_action_dialog_content
'''

# ─────────────────────────────────────────────────────────────────────────
# Рядки самого інтерфейсу (заміни в коді yafti_gtk.py).
# Ключ — точний фрагмент оригінального коду, значення — заміна.
# Якщо фрагмент більше не знайдено (upstream переписав рядок), лишається
# англійський текст, а --report про це повідомляє.
# ─────────────────────────────────────────────────────────────────────────
UI = {
    "APP_TITLE = 'Bazzite Portal'":
        "APP_TITLE = 'Портал Bazzite'",
    "AUTOSTART_FILE = os.path.join(AUTOSTART_DIR, 'bazzite-portal.desktop')":
        "AUTOSTART_FILE = os.path.join(AUTOSTART_DIR, 'bazzite-portal-ua.desktop')",
    "Name=Bazzite Portal\nComment=Helps you setup Bazzite\nExec=yafti_gtk.py /usr/share/yafti/yafti.yml":
        "Name=Портал Bazzite\nComment=Допомагає налаштувати Bazzite\nExec=/usr/local/bin/portal_ua",
    'set_placeholder_text(" Search Apps and Actions")':
        'set_placeholder_text(" Пошук програм і дій")',
    'Gtk.Label(label="Launch at startup")':
        'Gtk.Label(label="Запускати при вході")',
    '"⏳ Checking..."':
        '"⏳ Перевіряю..."',
    '"⏳ Fetching..."':
        '"⏳ Запитую..."',
    'header.set_markup("<b>Search results</b>")':
        'header.set_markup("<b>Результати пошуку</b>")',
    'Gtk.Label(label="No matches found")':
        'Gtk.Label(label="Нічого не знайдено")',
    'Gtk.Label(label="Loading...")':
        'Gtk.Label(label="Завантаження...")',
    'Gtk.Button(label="Close")':
        'Gtk.Button(label="Закрити")',
    "option.get('label', 'Run')":
        "option.get('label', 'Виконати')",
    "action.get('title', 'Action')":
        "action.get('title', 'Дія')",
    '"<span foreground=\'red\'><b>Status check timed out. You can still run the action.</b></span>"':
        '"<span foreground=\'red\'><b>Перевірка стану не вклалася в час. Дію все одно можна виконати.</b></span>"',
    '"Autostart error", f"Could not create autostart entry:\\n{e}"':
        '"Помилка автозапуску", f"Не вдалося створити запис автозапуску:\\n{e}"',
    '"Autostart error", f"Could not remove autostart entry:\\n{e}"':
        '"Помилка автозапуску", f"Не вдалося вилучити запис автозапуску:\\n{e}"',
    '"Configuration file not found",\n                f"Could not find {config_file} in the current directory."':
        '"Файл конфігурації не знайдено",\n                f"Не вдалося знайти {config_file}."',
    '"YAML parsing error"':
        '"Помилка розбору YAML"',
    '"No terminal available",\n                f"{error_message}\\n\\nCould not open a terminal automatically.\\nYou can also run the following command manually:\\n\\n{script}"':
        '"Немає доступного термінала",\n                f"{error_message}\\n\\nНе вдалося відкрити термінал автоматично.\\nЦю команду можна виконати вручну:\\n\\n{script}"',
    '"No terminal available",\n            f"{result}\\n\\nCould not open a terminal automatically.\\nYou can also run the following command manually:\\n\\n{script}"':
        '"Немає доступного термінала",\n            f"{result}\\n\\nНе вдалося відкрити термінал автоматично.\\nЦю команду можна виконати вручну:\\n\\n{script}"',
    'display_text = "Unknown"':
        'display_text = "Невідомо"',
    'display_text = "Installed"':
        'display_text = "Встановлено"',
    'display_text = "Enabled"':
        'display_text = "Увімкнено"',
    'display_text = "Not installed"':
        'display_text = "Не встановлено"',
    'display_text = "Disabled"':
        'display_text = "Вимкнено"',
    'display_text = "Removed"':
        'display_text = "Вилучено"',
    "display_text = status_token.capitalize()":
        'display_text = {"active": "Активно", "inactive": "Неактивно", '
        '"add": "Додано", "remove": "Вилучено", "upgraded": "Оновлено", '
        '"mismatch": "Невідповідність", "unset": "Не задано", '
        '"game": "Ігровий режим", "desktop": "Робочий стіл"'
        "}.get(token_lower, status_token.capitalize())",
    'parser = argparse.ArgumentParser(description="Bazzite Portal")':
        'parser = argparse.ArgumentParser(description="Портал Bazzite")',
    "if __name__ == '__main__':":
        ADDON + "\n\nif __name__ == '__main__':",
}

# ─────────────────────────────────────────────────────────────────────────
# Тексти всередині script:-рядків конфігурації (те, що видно в терміналі
# та у вікнах yad). Замінюються лише текстові аргументи, самі команди —
# ні.
# ─────────────────────────────────────────────────────────────────────────
SCRIPT_TEXT = {
    'echo "Press Enter to close..."':
        'echo "Натисніть Enter, щоб закрити..."',
    '--title="Tip"': '--title="Підказка"',
    '--title="Info"': '--title="Інформація"',
    "You can launch Bazaar anytime from your pinned apps or the app menu.":
        "Bazaar можна будь-коли запустити із закріплених програм або з меню програм.",
    "Proton Plus will be installed from Flathub.":
        "Proton Plus буде встановлено з Flathub.",
    "Proton Plus will be added to Steam as a Non-Steam application, and supports controller naviagtion":
        "Proton Plus буде додано до Steam як стороння програма; підтримується керування контролером.",
    "You can launch Bazzite Updater anytime from your pinned apps or the app menu.":
        "Bazzite Updater можна будь-коли запустити із закріплених програм або з меню програм.",
    "This will add Bazzite Updater as a Non-Steam App, allowing updates with your controller from Big Picture Mode.":
        "Bazzite Updater буде додано до Steam як сторонню програму, щоб оновлюватися контролером у Big Picture.",
    '--button="I get it"': '--button="Зрозуміло"',
    '--button="Got it"': '--button="Зрозуміло"',
    '--button="Nice!"': '--button="Чудово!"',
    '--button="Nice"': '--button="Чудово"',
}

# @@DATA@@

_missing = []
_drift = []


def tr(key, current):
    """Переклад із контролем розсинхрону з оригіналом."""
    entry = TR.get(key)
    if entry is None:
        _missing.append((key, current))
        return current
    source, target = entry
    if source != current:
        _drift.append((key, source, current))
        return current
    return current if target == "=" else target


def translate_config(cfg):
    for index, screen in enumerate(cfg.get("screens") or []):
        for field in ("title", "description"):
            if screen.get(field):
                short = "title" if field == "title" else "desc"
                screen[field] = tr("scr:%d:%s" % (index, short), screen[field])
        for action in screen.get("actions") or []:
            action_id = action.get("id") or ""
            for field in ("title", "description"):
                if action.get(field):
                    short = "title" if field == "title" else "desc"
                    action[field] = tr("act:%s:%s" % (action_id, short), action[field])
            for option in action.get("options") or []:
                label = option.get("label")
                if label:
                    if label in OPT:
                        option["label"] = OPT[label]
                    else:
                        _missing.append(("opt:" + label, label))
                if option.get("script"):
                    option["script"] = translate_script(option["script"])
            if action.get("script"):
                action["script"] = translate_script(action["script"])
    return cfg


def translate_script(script):
    for source, target in SCRIPT_TEXT.items():
        script = script.replace(source, target)
    return script


def patch_source(src):
    unmatched = []
    for source, target in UI.items():
        if source not in src:
            unmatched.append(source)
            continue
        src = src.replace(source, target)
    return src, unmatched


def report(unmatched_ui):
    print("Портал Bazzite — стан перекладу\n")
    print("  застосунок: %s" % UPSTREAM_APP)
    print("  конфігурація: %s\n" % UPSTREAM_CFG)

    if _drift:
        print("РОЗСИНХРОН — англійський оригінал змінився, показується він, "
              "а не переклад (%d):" % len(_drift))
        for key, was, now in _drift:
            print("  %s\n    було: %s\n    стало: %s" % (key, was, now))
        print("  → перезберіть портал: ./build.py\n")

    if _missing:
        print("БЕЗ ПЕРЕКЛАДУ (%d) — додайте в uk.map:" % len(_missing))
        for key, text in _missing:
            print("  %s = %s" % (key, text))
        print()

    if unmatched_ui:
        print("НЕ ЗНАЙДЕНО В КОДІ (%d) — upstream переписав ці рядки, "
              "вони лишаться англійськими:" % len(unmatched_ui))
        for source in unmatched_ui:
            print("  %s" % source.replace("\n", "\\n")[:100])
        print()

    if not (_drift or _missing or unmatched_ui):
        print("Усе перекладено, розсинхрону немає.")


def main():
    argv = sys.argv[1:]

    if "--english" in argv:
        argv.remove("--english")
        os.execv(UPSTREAM_APP, [UPSTREAM_APP, UPSTREAM_CFG] + argv)

    want_report = "--report" in argv
    if want_report:
        argv.remove("--report")

    if "--no-commands" in argv:
        argv.remove("--no-commands")
        os.environ["PORTAL_UA_COMMANDS"] = "0"

    for path in (UPSTREAM_APP, UPSTREAM_CFG):
        if not os.path.exists(path):
            sys.exit("Не знайдено %s — цей образ Bazzite не містить "
                     "bazzite-portal." % path)

    with open(UPSTREAM_CFG, "r", encoding="utf-8") as handle:
        cfg = yaml.safe_load(handle) or {}
    cfg["title"] = "Портал Bazzite"
    cfg = translate_config(cfg)

    with open(UPSTREAM_APP, "r", encoding="utf-8") as handle:
        src = handle.read()
    src, unmatched_ui = patch_source(src)

    if want_report:
        report(unmatched_ui)
        return

    tmp = tempfile.NamedTemporaryFile(
        mode="w", suffix=".yml", prefix="portal-ua-", delete=False,
        encoding="utf-8")
    yaml.safe_dump(cfg, tmp, allow_unicode=True, sort_keys=False,
                   default_flow_style=False, width=10 ** 6)
    tmp.close()
    atexit.register(lambda: os.path.exists(tmp.name) and os.unlink(tmp.name))

    sys.argv = ["portal_ua", tmp.name] + argv
    scope = {"__name__": "__main__", "__file__": UPSTREAM_APP,
             "__package__": None, "__doc__": None}
    exec(compile(src, UPSTREAM_APP, "exec"), scope)


if __name__ == "__main__":
    main()
