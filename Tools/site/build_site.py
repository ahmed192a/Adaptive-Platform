#!/usr/bin/env python3
"""Assemble the GitHub Pages site.

    python3 Tools/site/build_site.py --out site [--api doc/html]

Layout:
    site/index.html          landing page (demo, screenshots, conformance summary)
    site/img/                screenshots, demo GIF and mp4
    site/conformance/*.html  spec-conformance reports (rendered from doc/spec-conformance/*.md)
    site/docs/*.html         guides (rendered from doc/*.md)
    site/api/                Doxygen API reference (copied from --api when given)

The status counts on the landing page are computed from the conformance reports, so the page cannot
drift from them. "Unverified" means the AUTOSAR text for that item could not be read; it is not a
compliance claim.
"""
import argparse
import html
import re
import shutil
from pathlib import Path

import markdown

ROOT = Path(__file__).resolve().parents[2]
STATUSES = ["Implemented", "Partial", "Missing", "Deviation", "Unverified"]

CSS = """
:root{--bg:#0b1020;--card:#121a30;--line:#24304f;--fg:#e6ebf7;--mut:#9aa6c4;--acc:#6aa8ff}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);
font:16px/1.6 -apple-system,Segoe UI,Roboto,Helvetica,Arial,sans-serif}
a{color:var(--acc);text-decoration:none}a:hover{text-decoration:underline}
header{padding:56px 20px 32px;text-align:center;border-bottom:1px solid var(--line)}
h1{margin:0 0 8px;font-size:2.4rem}header p{margin:0 auto;max-width:760px;color:var(--mut)}
nav{margin-top:20px}nav a{margin:0 10px;font-weight:600}
main{max-width:1100px;margin:0 auto;padding:28px 20px 60px}
h2{margin-top:44px;border-bottom:1px solid var(--line);padding-bottom:6px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:16px}
.demo img{width:100%;border-radius:10px;border:1px solid var(--line)}
.gallery{display:grid;grid-template-columns:repeat(auto-fill,minmax(250px,1fr));gap:14px}
.gallery img{width:100%;border-radius:8px;border:1px solid var(--line);background:#000}
.gallery figcaption{font-size:.85rem;color:var(--mut);margin-top:4px}
table{border-collapse:collapse;width:100%;font-size:.93rem}
th,td{border-bottom:1px solid var(--line);padding:7px 10px;text-align:left;vertical-align:top}
th{color:var(--mut);font-weight:600}td.n{text-align:right;font-variant-numeric:tabular-nums}
.ok{color:#5be08a}.warn{color:#f1c24f}.bad{color:#ff7b7b}.mut{color:var(--mut)}
code,pre{background:#0a0f1f;border:1px solid var(--line);border-radius:6px}code{padding:1px 5px}
pre{padding:12px;overflow:auto}pre code{border:0;padding:0}
.doc{max-width:900px;margin:0 auto;padding:24px 20px 60px}
.doc img{max-width:100%}.crumb{font-size:.9rem;margin-bottom:12px}
footer{border-top:1px solid var(--line);color:var(--mut);text-align:center;padding:20px;font-size:.9rem}
"""

PAGE = """<!doctype html><html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>{title}</title>
<style>{css}</style></head><body>{body}</body></html>"""


def render_md(path: Path) -> str:
    text = path.read_text(encoding="utf-8")
    return markdown.markdown(text, extensions=["tables", "fenced_code", "toc", "sane_lists"])


def write_doc(out: Path, kind: str, md: Path, root_rel: str) -> str:
    name = md.stem
    title = name.replace("-", " ").replace("_", " ")
    m = re.search(r"^#\s+(.+)$", md.read_text(encoding="utf-8"), re.M)
    if m:
        title = m.group(1).strip()
    body = (f'<div class="doc"><div class="crumb"><a href="{root_rel}index.html">Home</a> / {kind}</div>'
            f"{render_md(md)}</div><footer>Adaptive Platform</footer>")
    dest = out / kind / f"{name}.html"
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_text(PAGE.format(title=html.escape(title), css=CSS, body=body), encoding="utf-8")
    return title


def count_statuses(md: Path) -> dict:
    counts = {s: 0 for s in STATUSES}
    for line in md.read_text(encoding="utf-8").splitlines():
        if not line.lstrip().startswith("|"):
            continue
        cells = [c.strip().strip("*` ") for c in line.strip().strip("|").split("|")]
        for cell in cells:
            for s in STATUSES:
                if cell.lower() == s.lower() or cell.lower().startswith(s.lower() + " "):
                    counts[s] += 1
                    break
            else:
                continue
            break
    return counts


def build(out: Path, api: Path | None) -> None:
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)

    img_src = ROOT / "doc" / "img"
    img_out = out / "img"
    if img_src.is_dir():
        shutil.copytree(img_src, img_out)

    # guides
    guides = []
    for name in ["demo", "dashboard", "ROADMAP", "nm", "tsync", "crypto-keyslots"]:
        md = ROOT / "doc" / f"{name}.md"
        if md.exists():
            guides.append((name, write_doc(out, "docs", md, "../")))

    # conformance reports
    rows = []
    for md in sorted((ROOT / "doc" / "spec-conformance").glob("*.md")):
        title = write_doc(out, "conformance", md, "../")
        rows.append((md.stem, count_statuses(md)))

    if api and api.is_dir():
        shutil.copytree(api, out / "api")

    gallery = ""
    names = {
        "01-overview": "Overview", "02-execution-management": "Execution Management", "03-phm": "Health monitoring",
        "04-diagnostics": "Diagnostics", "05-persistency": "Persistency", "06-crypto": "Crypto",
        "07-network-management": "Network Management", "08-time-sync": "Time synchronization",
        "09-diagnostics-lab": "Diagnostics lab", "10-software-update": "Software update",
        "11-key-slots": "Key slots", "12-demo-apps": "Demo applications",
    }
    for stem, cap in names.items():
        if (img_out / f"{stem}.png").exists():
            gallery += (f'<figure style="margin:0"><a href="img/{stem}.png"><img loading="lazy" '
                        f'src="img/{stem}.png" alt="{cap}"></a><figcaption>{cap}</figcaption></figure>')

    table = "".join(
        f'<tr><td><a href="conformance/{n}.html"><code>ara::{html.escape(n)}</code></a></td>'
        + "".join(f'<td class="n {cls}">{c[s]}</td>' for s, cls in
                  zip(STATUSES, ["ok", "warn", "bad", "bad", "mut"]))
        + "</tr>"
        for n, c in rows)
    guide_links = " &middot; ".join(f'<a href="docs/{n}.html">{html.escape(t)}</a>' for n, t in guides)
    gif = (img_out / "platform-demo.gif").exists()
    mp4 = (img_out / "platform-demo.mp4").exists()
    demo = ""
    if gif:
        demo = '<div class="demo"><a href="img/platform-demo.mp4"><img src="img/platform-demo.gif" alt="Platform demo"></a></div>'
    if mp4:
        demo += '<p class="mut">Click the animation for the full-quality video (<a href="img/platform-demo.mp4">mp4</a>).</p>'

    body = f"""
<header><h1>Adaptive Platform</h1>
<p>An implementation of the AUTOSAR Adaptive Platform on Linux: functional clusters (<code>ara::*</code>),
platform processes, a live web console and runnable demo applications.</p>
<nav><a href="#demo">Demo</a><a href="#status">Conformance</a><a href="docs/demo.html">Run it</a><a href="api/index.html">API reference</a></nav></header>
<main>
<h2 id="demo">Live demo</h2>{demo}
<p>The web console drives the real in-process libraries and the demo binaries; nothing on screen is mocked.
See <a href="docs/demo.html">how to run and re-record it</a>.</p>
<h2>Screenshots</h2><div class="gallery">{gallery}</div>
<h2 id="status">Specification conformance</h2>
<p>Per-requirement reports against AUTOSAR Adaptive Platform R25-11, falling back to the closest earlier release where a
chapter was unavailable (the release is recorded per requirement). <b>Unverified</b> means the specification text for that
item could not be read; it is not a claim of compliance.</p>
<div class="card"><table><thead><tr><th>Cluster</th>{''.join(f'<th class="n">{s}</th>' for s in STATUSES)}</tr></thead>
<tbody>{table}</tbody></table></div>
<h2>Guides</h2><p>{guide_links}</p>
<h2>API reference</h2><p><a href="api/index.html">Doxygen documentation</a> generated from the sources on every push to <code>master</code>.</p>
</main><footer>Adaptive Platform</footer>"""
    (out / "index.html").write_text(PAGE.format(title="Adaptive Platform", css=CSS, body=body), encoding="utf-8")
    (out / ".nojekyll").write_text("")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default="site")
    ap.add_argument("--api", default=None, help="Doxygen HTML directory to publish under /api")
    a = ap.parse_args()
    build(Path(a.out), Path(a.api) if a.api else None)
    print(f"site written to {a.out}")
