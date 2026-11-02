#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Themezer-3DS contributors
"""Keeps the translations in romfs/lang/ in step with the app's English texts.

The keys are the string literals in tr("..."), trf("...", ...) and trn("one", "other", count, ...) in source/ (see
app/strings.hpp), both arms of a ternary included; a trn's key is its other form, and its value in a translation is an
array of the language's plural forms (core/plural.hpp).

    python3 tools/i18n.py check   fails on stale keys, a "{}" too many or too few, plural arrays of the wrong length,
                                  and a tr() of something that isn't a literal; lists what's missing (make test runs it)
    python3 tools/i18n.py sync    writes each language's file with the keys in the order the code has them: missing
                                  ones empty (the app shows the English), stale ones dropped
"""

import json
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SOURCE = ROOT / "source"
LANG = ROOT / "romfs" / "lang"
# app/strings.cpp's codes, and how many plural forms each has
LANGUAGES = {"ja": 1, "fr": 2, "de": 2, "it": 2, "es": 2, "zh-Hans": 1, "ko": 1, "nl": 2, "pt": 2, "ru": 3, "zh-Hant": 1}

TOKEN = re.compile(
    r'(?P<comment>//[^\n]*|/\*.*?\*/)|(?P<string>"(?:[^"\\\n]|\\.)*")|(?P<char>\'(?:[^\'\\\n]|\\.)+\')'
    r"|(?P<name>[A-Za-z_]\w*)|(?P<space>\s+)|(?P<other>.)",
    re.S,
)
ESCAPES = {"n": "\n", "t": "\t", '"': '"', "'": "'", "\\": "\\", "?": "?"}


def unescape(literal):
    def one(match):
        if match.group(1) not in ESCAPES:
            raise ValueError(f"unsupported escape \\{match.group(1)}")
        return ESCAPES[match.group(1)]

    return re.sub(r"\\(.)", one, literal[1:-1])


def tokens(text):
    """(kind, value, line) of the code, without comments and spaces."""
    line = 1
    for match in TOKEN.finditer(text):
        kind = match.lastgroup
        if kind not in ("comment", "space"):
            yield kind, match.group(), line
        line += match.group().count("\n")


def arguments(toks, start):
    """The token lists of a call's arguments, from the token after its "(", and the index after its ")"."""
    args, current, depth, i = [], [], 0, start
    while i < len(toks):
        kind, value, _ = toks[i]
        if kind == "other" and value in "([{":
            depth += 1
        elif kind == "other" and value in ")]}":
            if depth == 0:
                args.append(current)
                return args, i + 1
            depth -= 1
        elif kind == "other" and value == "," and depth == 0:
            args.append(current)
            current = []
            i += 1
            continue
        current.append(toks[i])
        i += 1
    raise ValueError("unclosed call")


def literal(arg):
    """The text of adjacent string literals ("a" "b" is "ab"), or None when the argument is something else."""
    if not arg or any(kind != "string" for kind, _, _ in arg):
        return None
    return "".join(unescape(value) for _, value, _ in arg)


def texts(arg):
    """The literals an argument can be: itself, or both arms of `condition ? "a" : "b"`; None for anything else."""
    if (text := literal(arg)) is not None:
        return [text]
    question = next((i for i, (_, value, _) in enumerate(arg) if value == "?"), None)
    if question is None:
        return None
    colon = question + 1
    while colon < len(arg) and arg[colon][0] == "string":
        colon += 1
    yes, no = literal(arg[question + 1 : colon]), literal(arg[colon + 1 :])
    if colon < len(arg) and arg[colon][1] == ":" and yes is not None and no is not None:
        return [yes, no]
    return None


def extract():
    """{key: {"plural": bool, "one": str, "where": [file:line]}} in the order of the code, and the errors."""
    keys, errors = {}, []
    files = sorted(p for p in SOURCE.rglob("*") if p.suffix in (".cpp", ".hpp") and p.stem != "strings")
    for path in files:
        toks = list(tokens(path.read_text(encoding="utf-8")))
        for i, (kind, value, line) in enumerate(toks):
            if kind != "name" or value not in ("tr", "trf", "trn") or i + 1 >= len(toks) or toks[i + 1][1] != "(":
                continue
            before = "".join(tok[1] for tok in toks[max(0, i - 2) : i])
            if before.endswith((".", "->", "::")):  # a member or another namespace's
                continue
            where = f"{path.relative_to(ROOT)}:{line}"
            args, _ = arguments(toks, i + 2)
            if value == "trn":
                one, other = (literal(args[0]), literal(args[1])) if len(args) >= 3 else (None, None)
                if one is None or other is None:
                    errors.append(f"{where}: trn() needs literals for its one and other forms")
                    continue
                found = [(other, True, one)]
            else:
                found = [(text, False, None) for text in texts(args[0]) or []]
                if not found:
                    errors.append(f"{where}: {value}() of something that isn't a literal, which can't be translated")
            for text, plural, one in found:
                entry = keys.setdefault(text, {"plural": plural, "one": one, "where": []})
                if entry["plural"] != plural:
                    errors.append(f"{where}: {text!r} is both a plural and a plain text")
                entry["where"].append(where)
    return keys, errors


def placeholders(text):
    return text.count("{}")


def load(path):
    def no_duplicates(pairs):
        seen = {}
        for key, value in pairs:
            if key in seen:
                raise ValueError(f"{key!r} is there twice")
            seen[key] = value
        return seen

    return json.loads(path.read_text(encoding="utf-8"), object_pairs_hook=no_duplicates)


def check():
    keys, errors = extract()
    for code, forms in LANGUAGES.items():
        path = LANG / f"{code}.json"
        if not path.exists():
            errors.append(f"{path.relative_to(ROOT)} is missing: run tools/i18n.py sync")
            continue
        try:
            translations = load(path)
        except ValueError as error:
            errors.append(f"{path.relative_to(ROOT)}: {error}")
            continue
        missing = 0
        for key, value in translations.items():
            where = f"{path.relative_to(ROOT)}: {key!r}"
            if key not in keys:
                errors.append(f"{where} isn't in the code any more")
                continue
            if keys[key]["plural"]:
                if not isinstance(value, list) or len(value) != forms or not all(isinstance(t, str) for t in value):
                    errors.append(f"{where} isn't an array of {code}'s {forms} plural forms")
                    continue
                texts_ = value
            elif isinstance(value, str):
                texts_ = [value]
            else:
                errors.append(f"{where} isn't a text")
                continue
            for text in texts_:
                if text and placeholders(text) != placeholders(key):
                    errors.append(f"{where}: {text!r} has {placeholders(text)} {{}} instead of {placeholders(key)}")
            if not all(texts_):
                missing += 1
        missing += sum(key not in translations for key in keys)
        if missing:
            print(f"{code}: {missing} of {len(keys)} texts aren't translated")
    for error in errors:
        print(error, file=sys.stderr)
    return 1 if errors else 0


def sync():
    keys, errors = extract()
    for error in errors:
        print(error, file=sys.stderr)
    LANG.mkdir(parents=True, exist_ok=True)
    for code, forms in LANGUAGES.items():
        path = LANG / f"{code}.json"
        old = load(path) if path.exists() else {}
        new = {}
        for key, entry in keys.items():
            value = old.get(key)
            if entry["plural"]:
                if not isinstance(value, list) or len(value) != forms:
                    value = [""] * forms
            elif not isinstance(value, str):
                value = ""
            new[key] = value
        path.write_text(json.dumps(new, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return 1 if errors else 0


if __name__ == "__main__":
    command = sys.argv[1] if len(sys.argv) > 1 else ""
    if command not in ("check", "sync"):
        sys.exit(__doc__)
    sys.exit(check() if command == "check" else sync())
