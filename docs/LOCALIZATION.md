# Localization Guide

The Gothic Multiplayer client loads its UI strings from JSON files located in
`GMP_Client/resources/Multiplayer/Localization`. Each language file is a simple
JSON object where the keys correspond to the identifiers defined in
`CLanguage::STRING_ID` and the values are localized phrases. Example:

```json
{
  "LANGUAGE": "English (English)",
  "WRITE_NICKNAME": "Type your ingame nickname:",
  "MMENU_CHSERVER": "Play"
}
```

## Adding a new language

1. Duplicate `en.json` and rename it to the ISO-like identifier you want (e.g.
   `es.json`).
2. Update the `LANGUAGE` entry to the display name shown in the language picker.
3. Translate each key.
4. Append the new file name to the `Localization/index` file. The launcher uses
   this manifest to build the language selection list.

When a translation is missing, the client falls back to the English string at
runtime and logs a warning via `spdlog`, so you can safely ship partially
translated files. However, we aim to keep the default set complete.

## Updating existing translations

* Modify the relevant JSON entries. Formatting is not important, but keeping the
  alphabetical order makes diffs easier to read.
* Run the validation script described below to ensure no keys are missing.

## Validation script

The repository ships with `tools/check_translations.py` which scans all JSON
files and checks that every `CLanguage::STRING_ID` has a translation. Run it
before submitting patches or shipping a build:

```bash
python tools/check_translations.py
```

The script exits with a non-zero status if it encounters missing keys, making it
suitable for CI pipelines or pre-release checklists.
