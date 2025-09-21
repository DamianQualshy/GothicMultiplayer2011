#!/usr/bin/env python3
"""Validate Gothic Multiplayer localization files.

The script ensures that every JSON localization provides translations for each
`CLanguage::STRING_ID`. Missing keys are reported per language and cause a
non-zero exit code so it can be wired into CI or pre-release checks.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path
from typing import Iterable, List

REPO_ROOT = Path(__file__).resolve().parents[1]
LANG_DIR = REPO_ROOT / "GMP_Client" / "resources" / "Multiplayer" / "Localization"
CLANGUAGE_HEADER = REPO_ROOT / "GMP_Client" / "CLanguage.h"


def parse_string_ids(header: Path) -> List[str]:
    content = header.read_text(encoding="utf-8")
    match = re.search(r"#define\s+CLANGUAGE_FOR_EACH_STRING_ID\(X\)(.*?)enum STRING_ID", content, re.S)
    if not match:
        raise RuntimeError("Unable to locate CLANGUAGE_FOR_EACH_STRING_ID macro in CLanguage.h")

    macro_body = match.group(1)
    ids = re.findall(r"X\(([^)]+)\)", macro_body)
    if not ids:
        raise RuntimeError("No string identifiers found in CLanguage.h")
    return ids


def load_localization(path: Path) -> dict:
    try:
        with path.open(encoding="utf-8") as fh:
            data = json.load(fh)
    except FileNotFoundError as exc:
        raise RuntimeError(f"Missing localization file: {path}") from exc
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"Invalid JSON in {path}: {exc}") from exc

    if not isinstance(data, dict):
        raise RuntimeError(f"Localization file {path} must contain a JSON object")
    return data


def validate_languages(ids: Iterable[str]) -> int:
    errors = 0
    ids = list(ids)
    for json_file in sorted(LANG_DIR.glob("*.json")):
        data = load_localization(json_file)
        missing = [key for key in ids if key not in data]
        extras = sorted(set(data) - set(ids))
        if missing:
            errors += 1
            print(f"{json_file.relative_to(REPO_ROOT)} is missing {len(missing)} keys:")
            for key in missing:
                print(f"  - {key}")
        if extras:
            print(f"{json_file.relative_to(REPO_ROOT)} contains {len(extras)} unexpected keys:")
            for key in extras:
                print(f"  - {key}")
    return errors


def main(argv: List[str]) -> int:
    parser = argparse.ArgumentParser(description="Validate localization JSON files")
    parser.parse_args(argv)  # no options yet, placeholder for future use

    string_ids = parse_string_ids(CLANGUAGE_HEADER)
    errors = validate_languages(string_ids)

    if errors:
        print(f"Found missing translations in {errors} localization file(s).", file=sys.stderr)
        return 1
    print("All localization files contain the expected keys.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
