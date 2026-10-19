# Translating the WhiteoutDex manual

The English pages in `content/en/` are the source. A translation goes into
`content/<lang>/` with **the same file names**. A missing page falls back to
English, so a language can be translated page by page.

Languages: `de` German, `es` Spanish, `fr` French, `it` Italian, `ja` Japanese,
`ko` Korean, `pt-br` Brazilian Portuguese, `ru` Russian, `zh` Simplified Chinese.

## What to translate

- All normal text, headings, table cells, captions and callout bodies.
- `content/en/ui.json` → `content/<lang>/ui.json`: translate the values, keep
  the keys.
- The caption inside `[[shot:name|Caption.]]` (only the part after `|`).
- The alt text inside `![Caption](file.png)` (keep the file name).

## What to keep exactly as it is

| Markup | Example | Rule |
|---|---|---|
| Screenshot | `[[shot:importer|…]]` | Keep `shot:importer|`. |
| Control number | `[[ctl:1052]]`, `[[ctl:7#2]]` | Keep exactly. The build inserts the number and the label in the right language. |
| Plugin label | `[[t:menu_importer]]`, `[[t:set_art_tier_items#2]]` | Keep exactly. The build inserts the translated label. |
| Key | `[[key:Ctrl+Z]]` | Keep. German only: write `Strg` instead of `Ctrl`. |
| Icon | `[[icon:wdx_sb_import]]` | Keep. |
| Callout | `!!! tip`, `!!! note`, `!!! warning` | Keep the line in English and the 4-space indent of the body. |
| Code | `` `Textures\` ``, file names, paths, `.mdx` | Keep. |
| Tables | | Same number of rows and columns, same order. |
| Links to pages | *Scene problems and Fix all* | Translate the page title as it is translated in that language. |

3ds Max menu paths (*Create > Geometry > Box*, *Auto Key*, *Material Editor*)
stay in English: the screenshots show the English 3ds Max.

## How to write

The manual uses plain, simple language (the rules of Simplified Technical
English, applied to every language):

- Short sentences: at most about 20 words. One instruction per sentence.
- Active voice, present tense, the reader addressed directly.
- One word for one thing. Use the words of the plugin's own interface in that
  language (see `src/pre_startup_scripts/WhiteoutDexLocalization.ms`, block
  `<lang>Data`) so the manual and the program say the same.
- No idioms, no jokes, no rare words.

Form of address (as the plugin's interface does, except German):

| Language | Form |
|---|---|
| de | du (decided by the project) |
| fr | vous |
| es | tú |
| it | tu |
| pt-br | você |
| ru | вы |
| ja | です／ます (polite, neutral) |
| ko | 합니다 / 하세요 (polite) |
| zh | 你 |

## Check your work

    python docs/tools/build_docs.py --no-pdf <lang>

The build lists everything it cannot resolve under `missing:`. A translation
must add nothing to that list.
