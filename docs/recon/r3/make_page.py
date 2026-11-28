"""Builds the owner's page of R3's videos: every pair embedded as a data URI, with what it compares, the switch and the decision.
   python -I make_page.py <repo root> <content.json> <out.html>"""
import base64, html, json, os, sys

root, content_file, out = sys.argv[1], sys.argv[2], sys.argv[3]
content = json.load(open(content_file, encoding='utf-8'))


def video(tag):
    path = os.path.join(root, 'Saved', 'Films', tag, 'pair.mp4')
    data = base64.b64encode(open(path, 'rb').read()).decode('ascii')
    return f'<video controls muted loop playsinline preload="metadata" src="data:video/mp4;base64,{data}"></video>'


def esc(text):
    return html.escape(text, quote=True)


parts = []
for group in content['groups']:
    cards = []
    for item in group['items']:
        rows = ''.join(f'<div class="kv"><dt>{esc(k)}</dt><dd>{v}</dd></div>' for k, v in item['facts'])
        cards.append(f'''<article class="card" id="{esc(item['tag'])}">
  <h3>{esc(item['title'])}</h3>
  <div class="frame">{video(item['tag'])}</div>
  <p class="cap">{item['compares']}</p>
  <dl>{rows}</dl>
  <p class="path"><code>Saved\\Films\\{esc(item['tag'])}\\pair.mp4</code></p>
</article>''')
    parts.append(f'''<section class="group">
  <header><h2>{esc(group['title'])}</h2><p>{group['lead']}</p></header>
  <div class="grid">{''.join(cards)}</div>
</section>''')

decisions = ''.join(f'''<li><span class="tag {esc(d['kind'])}">{esc(d['label'])}</span><div><strong>{esc(d['title'])}</strong><p>{d['text']}</p></div></li>'''
                    for d in content['decisions'])
figures = ''.join('<tr>' + ''.join(f'<td>{c}</td>' for c in row) + '</tr>' for row in content['figures']['rows'])
head = ''.join(f'<th>{esc(c)}</th>' for c in content['figures']['head'])

page = f'''<title>{esc(content['title'])}</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=IBM+Plex+Sans:wght@400;500;600&family=IBM+Plex+Mono:wght@400;500&family=Archivo:wght@600;700&display=swap">
<style>
/* Layout: a review sheet - decisions first, the figures, then the films in groups, two to a row on wide screens. */
:root {{
  --bg: #f3f4f1; --surface: #ffffff; --fg: #1c211d; --muted: #5d665f; --line: #d5dad3;
  --accent: #2f6b4f; --warn: #9a5b12; --off: #6b5a8e;
  --display: "Archivo", "Segoe UI", system-ui, sans-serif;
  --body: "IBM Plex Sans", "Segoe UI", system-ui, sans-serif;
  --mono: "IBM Plex Mono", ui-monospace, Consolas, monospace;
}}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{
  --bg: #141815; --surface: #1c221e; --fg: #e4e9e3; --muted: #9aa59c; --line: #2f3832;
  --accent: #7cc4a0; --warn: #e0a85a; --off: #b8a6dc; color-scheme: dark }} }}
:root[data-theme="dark"] {{
  --bg: #141815; --surface: #1c221e; --fg: #e4e9e3; --muted: #9aa59c; --line: #2f3832;
  --accent: #7cc4a0; --warn: #e0a85a; --off: #b8a6dc; color-scheme: dark }}
body {{ background: var(--bg); color: var(--fg); font: 15px/1.55 var(--body); }}
.wrap {{ max-width: 1180px; margin: 0 auto; padding-block: 28px 64px; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); display: grid; gap: 36px; }}
h1, h2, h3 {{ font-family: var(--display); text-wrap: balance; margin: 0; }}
h1 {{ font-size: clamp(26px, 4vw, 36px); letter-spacing: -0.01em; }}
h2 {{ font-size: 21px; }}
h3 {{ font-size: 16px; }}
p {{ margin: 0; max-width: 72ch; }}
.lead {{ display: grid; gap: 10px; }}
.lead p {{ color: var(--muted); }}
.eyebrow {{ font: 500 12px/1 var(--mono); letter-spacing: 0.08em; text-transform: uppercase; color: var(--accent); }}
ol.decisions {{ list-style: none; margin: 0; padding: 0; display: grid; gap: 12px; }}
ol.decisions li {{ display: grid; grid-template-columns: 7.5em minmax(0, 1fr); gap: 14px; align-items: start; background: var(--surface); border: 1px solid var(--line); border-radius: 6px; padding: 14px 16px; }}
ol.decisions p {{ color: var(--muted); margin-top: 4px; }}
.tag {{ font: 500 11px/1.3 var(--mono); letter-spacing: 0.06em; text-transform: uppercase; padding: 4px 8px; border-radius: 4px; border: 1px solid currentColor; text-align: center; }}
.tag.yours {{ color: var(--accent); }}
.tag.taken {{ color: var(--warn); }}
.tag.no {{ color: var(--off); }}
.tablewrap {{ overflow-x: auto; background: var(--surface); border: 1px solid var(--line); border-radius: 6px; }}
table {{ border-collapse: collapse; width: 100%; font-size: 13.5px; font-variant-numeric: tabular-nums; }}
th, td {{ text-align: left; padding: 8px 12px; border-bottom: 1px solid var(--line); vertical-align: top; }}
th {{ font: 500 12px/1.3 var(--mono); color: var(--muted); letter-spacing: 0.04em; }}
tr:last-child td {{ border-bottom: 0; }}
.group {{ display: grid; gap: 16px; }}
.group header {{ display: grid; gap: 6px; border-top: 2px solid var(--fg); padding-top: 12px; }}
.group header p {{ color: var(--muted); }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(min(100%, 520px), 1fr)); gap: 18px; }}
.card {{ background: var(--surface); border: 1px solid var(--line); border-radius: 6px; padding: 14px; display: grid; gap: 10px; min-width: 0; }}
.frame {{ background: #000; border-radius: 4px; overflow: hidden; aspect-ratio: 32 / 9; }}
video {{ width: 100%; height: 100%; display: block; object-fit: contain; }}
.cap {{ color: var(--fg); }}
dl {{ margin: 0; display: grid; gap: 4px; }}
.kv {{ display: grid; grid-template-columns: 8.5em minmax(0, 1fr); gap: 10px; }}
dt {{ font: 500 12px/1.6 var(--mono); color: var(--muted); }}
dd {{ margin: 0; }}
code {{ font: 12.5px var(--mono); overflow-wrap: anywhere; }}
.path {{ color: var(--muted); }}
@media (max-width: 560px) {{ ol.decisions li {{ grid-template-columns: 1fr; }} .kv {{ grid-template-columns: 1fr; gap: 0; }} }}
@media (prefers-reduced-motion: reduce) {{ * {{ scroll-behavior: auto; }} }}
</style>
<div class="wrap">
  <div class="lead">
    <span class="eyebrow">{esc(content['eyebrow'])}</span>
    <h1>{esc(content['heading'])}</h1>
    {''.join(f'<p>{p}</p>' for p in content['lead'])}
  </div>
  <section class="group">
    <header><h2>Decisions for you</h2><p>{content['decisions_lead']}</p></header>
    <ol class="decisions">{decisions}</ol>
  </section>
  <section class="group">
    <header><h2>{esc(content['figures']['title'])}</h2><p>{content['figures']['lead']}</p></header>
    <div class="tablewrap"><table><thead><tr>{head}</tr></thead><tbody>{figures}</tbody></table></div>
  </section>
  {''.join(parts)}
</div>
'''
open(out, 'w', encoding='utf-8').write(page)
print('PAGE', out, round(len(page) / 1e6, 2), 'MB')
