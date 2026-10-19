"""Builds the WhiteoutDex manual: Markdown pages -> one HTML file and one PDF
per language.

    python docs/tools/build_docs.py              every language
    python docs/tools/build_docs.py en de        only these
    python docs/tools/build_docs.py --no-pdf en  HTML only

Layout of docs/:
    manual.json            chapter order, page ids, languages
    content/<lang>/<id>.md one page per id; a missing page falls back to English
    content/<lang>/ui.json the manual's own words (Contents, Tip, Figure, ...)
    images/<lang>/         screenshots of the translated interface
    images/shared/         pictures that are the same in every language
    theme/manual.css       look of the HTML and the PDF
    out/<lang>/            result (not in git)

Markdown extras:
    ![Caption](name.png)             figure; looked up in images/<lang>, then
                                     images/en, then images/shared
    !!! tip|note|warning "Title"     callout box (indented body)
    [[ui:Button text]]               an interface label, shown in bold
    [[t:key]]                        the label with this key from the plugin's
                                     own catalog (WhiteoutDexLocalization.ms),
                                     in the page's language; [[t:key#2]] takes
                                     item 2 of a "a|b|c" list
    [[key:Ctrl+S]]                   a key
"""
import json
import re
import shutil
import subprocess
import sys
from html import escape
from pathlib import Path

import markdown

sys.path.insert(0, str(Path(__file__).resolve().parent))

DOCS = Path(__file__).resolve().parents[1]
EDGE_CANDIDATES = [
    Path(r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Microsoft\Edge\Application\msedge.exe"),
    Path(r"C:\Program Files\Google\Chrome\Application\chrome.exe"),
]


CATALOG = DOCS.parent / "src" / "pre_startup_scripts" / "WhiteoutDexLocalization.ms"
CATALOG_BLOCKS = {"en": "enData", "zh": "zhData", "de": "deData", "ru": "ruData", "ja": "jaData",
                  "ko": "koData", "fr": "frData", "es": "esData", "it": "itData", "pt-br": "ptbrData"}
_catalogs = {}


def catalog(lang):
    """key -> text of the plugin's UI catalog for one language."""
    if not _catalogs:
        src = CATALOG.read_text(encoding="utf-8-sig")
        starts = sorted((src.index(f"local {b} = #("), code) for code, b in CATALOG_BLOCKS.items())
        for i, (pos, code) in enumerate(starts):
            end = starts[i + 1][0] if i + 1 < len(starts) else len(src)
            table = {}
            for m in re.finditer(r'#\("([^"]+)",\s*"((?:[^"\\]|\\.)*)"\)', src[pos:end]):
                text = m.group(2).replace('\\"', '"').replace("\\n", " ").replace("\\\\", "\\")
                table[m.group(1)] = text
            _catalogs[code] = table
    return _catalogs.get(lang, _catalogs["en"])


def load_json(path, default=None):
    if path.exists():
        return json.loads(path.read_text(encoding="utf-8"))
    return default


class Book:
    def __init__(self, lang):
        self.lang = lang
        self.manual = load_json(DOCS / "manual.json")
        self.ui = dict(load_json(DOCS / "content" / "en" / "ui.json", {}))
        self.ui.update(load_json(DOCS / "content" / lang / "ui.json", {}))
        self.out = DOCS / "out" / lang
        self.figure_no = 0
        self.missing = []

    # ---- pages -------------------------------------------------------------
    def page_source(self, page_id):
        for lang in (self.lang, "en"):
            p = DOCS / "content" / lang / f"{page_id}.md"
            if p.exists():
                return p.read_text(encoding="utf-8"), lang
        self.missing.append(page_id)
        return f"# {page_id}\n\n(missing)\n", "en"

    def page_exists(self, page_id):
        return any((DOCS / "content" / lang / f"{page_id}.md").exists() for lang in (self.lang, "en"))

    def find_image(self, name):
        for folder in (self.lang, "en", "shared"):
            p = DOCS / "images" / folder / name
            if p.exists():
                return p
        return None

    # ---- markdown extras ---------------------------------------------------
    def label(self, m):
        key, _, item = m.group(1).partition("#")
        text = catalog(self.lang).get(key) or catalog("en").get(key)
        if text is None:
            self.missing.append(f"t:{key}")
            text = key
        if item:
            parts = text.split("|")
            text = parts[int(item) - 1] if int(item) <= len(parts) else text
        return f'<span class="ui">{escape(text.strip())}</span>'

    def icon(self, m):
        """[[icon:name]]: a WhiteoutDex icon (repo whiteoutdex_icons/name.png) inline."""
        src = DOCS.parent / "whiteoutdex_icons" / (m.group(1) + ".png")
        if not src.exists():
            self.missing.append(f"icon:{m.group(1)}")
            return ""
        dst = self.out / "img" / "icons" / src.name
        dst.parent.mkdir(parents=True, exist_ok=True)
        if not dst.exists():
            shutil.copy2(src, dst)
        return f'<img class="icon" src="img/icons/{src.name}" alt="">'

    def pre(self, text):
        text = re.sub(r"\[\[icon:([\w.-]+)\]\]", self.icon, text)
        text = re.sub(r"\[\[t:([^\]]+)\]\]", self.label, text)
        text = re.sub(r"\[\[ui:([^\]]+)\]\]", r'<span class="ui">\1</span>', text)
        text = re.sub(r"\[\[key:([^\]]+)\]\]",
                      lambda m: "+".join(f"<kbd>{k}</kbd>" for k in m.group(1).split("+")), text)
        return text

    def figure(self, m):
        caption, name = m.group(1), m.group(2)
        src = self.find_image(name)
        self.figure_no += 1
        label = f'{self.ui.get("figure", "Figure")} {self.figure_no}'
        if src is None:
            self.missing.append(name)
            body = f'<div class="placeholder">{escape(name)}</div>'
        else:
            rel = f"img/{src.parent.name}/{src.name}"
            dst = self.out / rel
            dst.parent.mkdir(parents=True, exist_ok=True)
            if not dst.exists() or dst.stat().st_mtime < src.stat().st_mtime:
                shutil.copy2(src, dst)
            body = f'<img src="{rel}" alt="{escape(caption)}">'
        cap = f"<figcaption><b>{label}</b> {caption}</figcaption>" if caption else ""
        return f'\n<figure>{body}{cap}</figure>\n'

    # ---- numbered screenshots -------------------------------------------
    # [[shot:name|Caption]] shows images/<lang>/name.png with a numbered
    # badge on every control that a [[ctl:<id>]] below it (up to the next
    # shot) names. capture.ps1 stores the controls' rectangles in name.json.
    def shots(self, text):
        tags = list(re.finditer(r"\[\[shot:([^|\]]+)\|?([^\]]*)\]\]", text))
        if not tags:
            return text
        out = [text[:tags[0].start()]]
        for i, m in enumerate(tags):
            name, caption = m.group(1).strip(), m.group(2).strip()
            body = text[m.end():tags[i + 1].start() if i + 1 < len(tags) else len(text)]
            # A control is named by its id; "7#2" is the second control with
            # id 7 in window order (floaters repeat ids across rollouts).
            ids = []
            for cid in re.findall(r"\[\[ctl:(-?\d+(?:#\d+)?)\]\]", body):
                key = cid if "#" in cid else cid + "#1"
                if key not in ids:
                    ids.append(key)
            controls = {}
            src = self.find_image(name + ".png")
            meta = src.with_suffix(".json") if src else None
            if meta and meta.exists():
                seen = {}
                for c in json.loads(meta.read_text(encoding="utf-8"))["controls"]:
                    seen[c["id"]] = seen.get(c["id"], 0) + 1
                    controls[f'{c["id"]}#{seen[c["id"]]}'] = c
            marks = [dict(n=n, **{k: controls[cid][k] for k in ("x", "y", "w", "h")})
                     for n, cid in enumerate(ids, 1) if cid in controls]
            for cid in ids:
                if cid not in controls:
                    self.missing.append(f"{name}:ctl {cid}")
            img_name = name + ".png"
            if src is not None and marks:
                from annotate import annotate
                dst = self.out / "img" / "marked" / f"{name}.png"
                dst.parent.mkdir(parents=True, exist_ok=True)
                annotate(src, marks, dst)
                img_name = None
                fig = self.figure_html(caption, dst, "img/marked/" + dst.name)
            else:
                fig = None

            def caption_of(c):
                """A field without text of its own (edit, combo, spinner) is
                named by the label left of it on the same row, else above it."""
                if not c:
                    return ""
                if c.get("text", "").strip() and c["class"] not in ("Edit", "ComboBox"):
                    return c["text"]
                # A host frame (dotNetControl) carries no text; the .NET
                # control filling it does.
                for o in controls.values():
                    if o is not c and o["text"].strip() and (o["x"], o["y"], o["w"], o["h"]) == (c["x"], c["y"], c["w"], c["h"]):
                        return o["text"]
                cy = c["y"] + c["h"] / 2
                best, best_d = None, 1e9
                for o in controls.values():
                    if not ("Static" in o["class"] or "LINKCTRL" in o["class"]) or not o["text"].strip() or o is c:
                        continue
                    oy = o["y"] + o["h"] / 2
                    if abs(oy - cy) < max(c["h"], o["h"]) * 0.6 and o["x"] + o["w"] <= c["x"] + 4:
                        d = c["x"] - (o["x"] + o["w"])
                    elif o["y"] + o["h"] <= c["y"] + 4 and o["x"] < c["x"] + c["w"] and o["x"] + o["w"] > c["x"]:
                        d = (c["y"] - (o["y"] + o["h"])) * 2 + 1000
                    else:
                        continue
                    if d < best_d:
                        best, best_d = o, d
                return best["text"].rstrip(":") if best and best_d < 1100 else ""

            def ctl(mm):
                cid = mm.group(1) if "#" in mm.group(1) else mm.group(1) + "#1"
                n = ids.index(cid) + 1
                label = " ".join(caption_of(controls.get(cid)).replace("&", "").split())
                # Rollout labels carry dot leaders ("Radius ......") and
                # dashes ("- Model File -"): keep only the words.
                label = re.sub(r"(?:\s*[.·…]){3,}", " ", label).strip(" -:·.…")
                if len(label) > 60:
                    label = ""
                badge = f'<span class="badge">{n}</span>'
                return badge + (f' <span class="ui">{escape(label)}</span>' if label else "")
            body = re.sub(r"\[\[ctl:(-?\d+(?:#\d+)?)\]\]", ctl, body)
            if fig is None:
                fig = f"![{caption}]({img_name})"
            out.append("\n" + fig + "\n" + body)
        return "".join(out)

    def figure_html(self, caption, path, rel):
        self.figure_no += 1
        label = f'{self.ui.get("figure", "Figure")} {self.figure_no}'
        cap = f"<figcaption><b>{label}</b> {caption}</figcaption>" if caption else ""
        return f'<figure><img src="{rel}" alt="{escape(caption)}">{cap}</figure>'

    def render_page(self, page_id, chapter_no, page_no):
        text, src_lang = self.page_source(page_id)
        text = self.pre(text)
        text = self.shots(text)
        text = re.sub(r"^!\[([^\]]*)\]\(([^)\s]+)\)\s*$", self.figure, text, flags=re.M)
        md = markdown.Markdown(extensions=["tables", "admonition", "attr_list", "md_in_html",
                                           "sane_lists", "toc"],
                               extension_configs={"toc": {"permalink": False}})
        html = md.convert(text)
        # Admonition titles default to the English type name; use the UI words.
        for kind in ("tip", "note", "warning", "important"):
            html = html.replace(f'<p class="admonition-title">{kind.capitalize()}</p>',
                                f'<p class="admonition-title">{escape(self.ui.get(kind, kind.capitalize()))}</p>')
        anchor = f"p-{page_id}"
        # The page's first heading becomes the TOC entry.
        m = re.search(r"<h1[^>]*>(.*?)</h1>", html)
        title = re.sub("<[^>]+>", "", m.group(1)) if m else page_id
        html = re.sub(r'<h1(?: id="[^"]*")?>', f'<h1 id="{anchor}" data-no="{chapter_no}.{page_no}">',
                      html, count=1)
        note = ""
        if src_lang != self.lang:
            note = f'<p class="untranslated">{escape(self.ui.get("untranslated", "This page is not translated yet."))}</p>'
        return title, anchor, f'<section class="page" lang="{src_lang}">{note}{html}</section>'

    # ---- book --------------------------------------------------------------
    def build_html(self):
        self.out.mkdir(parents=True, exist_ok=True)
        toc, body = [], []
        for ci, chapter in enumerate(self.manual["chapters"], 1):
            ctitle = self.ui.get(chapter["title_key"], chapter["title_key"])
            entries = []
            pages = []
            written = [p for p in chapter["pages"] if self.page_exists(p)]
            self.missing.extend(f"page {p}" for p in chapter["pages"] if p not in written)
            if not written:
                continue
            for pi, page_id in enumerate(written, 1):
                title, anchor, html = self.render_page(page_id, ci, pi)
                entries.append(f'<li><a href="#{anchor}"><span class="no">{ci}.{pi}</span> {escape(title)}</a></li>')
                pages.append(html)
            toc.append(f'<li class="chapter"><a href="#c-{ci}"><span class="no">{ci}</span> {escape(ctitle)}</a><ol>{"".join(entries)}</ol></li>')
            intro = self.ui.get(chapter["title_key"] + "_intro", "")
            body.append(f'<div class="chapter-title" id="c-{ci}"><span class="no">{ci}</span>'
                        f'<h1 class="chapter">{escape(ctitle)}</h1><p>{escape(intro)}</p></div>')
            body.extend(pages)

        logo = DOCS / "images" / "shared" / "whiteoutdex_icon.png"
        if logo.exists():
            (self.out / "img").mkdir(parents=True, exist_ok=True)
            shutil.copy2(logo, self.out / "img" / "logo.png")
        css = (DOCS / "theme" / "manual.css").read_text(encoding="utf-8")
        version = (DOCS.parent / "version.txt").read_text(encoding="utf-8").strip() \
            if (DOCS.parent / "version.txt").exists() else ""
        u = self.ui
        html = f"""<!doctype html>
<html lang="{self.lang}">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{escape(u.get("doc_title", "WhiteoutDex Manual"))}</title>
<style>{css}</style>
</head>
<body>
<div class="cover">
  <img class="logo" src="img/logo.png" alt="">
  <h1>WhiteoutDex</h1>
  <p class="subtitle">{escape(u.get("doc_subtitle", ""))}</p>
  <p class="meta">{escape(u.get("version", "Version"))} {escape(version)} &middot; 3ds Max 2016&ndash;2027</p>
</div>
<nav class="toc">
  <h1>{escape(u.get("contents", "Contents"))}</h1>
  <ol>{"".join(toc)}</ol>
</nav>
<main>
{"".join(body)}
</main>
</body>
</html>
"""
        name = f"WhiteoutDex_Manual_{self.lang}.html"
        (self.out / name).write_text(html, encoding="utf-8")
        return self.out / name

    def build_pdf(self, html_path):
        edge = next((p for p in EDGE_CANDIDATES if p.exists()), None)
        if edge is None:
            print("  no Edge/Chrome found, PDF skipped")
            return None
        pdf = html_path.with_suffix(".pdf")
        profile = DOCS / "out" / ".browser-profile"
        cmd = [str(edge), "--headless=new", "--disable-gpu", "--no-first-run",
               f"--user-data-dir={profile}", "--no-pdf-header-footer",
               "--virtual-time-budget=20000",
               f"--print-to-pdf={pdf}", html_path.resolve().as_uri()]
        subprocess.run(cmd, check=False, timeout=600,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return pdf if pdf.exists() else None


def main(argv):
    manual = load_json(DOCS / "manual.json")
    pdf = "--no-pdf" not in argv
    langs = [a for a in argv if not a.startswith("--")] or manual["languages"]
    for lang in langs:
        book = Book(lang)
        html = book.build_html()
        out = [str(html)]
        if pdf:
            p = book.build_pdf(html)
            out.append(str(p) if p else "PDF FAILED")
        print(f"{lang}: " + " | ".join(out))
        if book.missing:
            print(f"  missing: {', '.join(sorted(set(book.missing)))}")


if __name__ == "__main__":
    main(sys.argv[1:])
