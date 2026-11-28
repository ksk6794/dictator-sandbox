"""Builds the owner's page of the videos of the window of 28 November (night2): each video embedded as a data URI, with what it
   compares, the switch and the decision.
   python -I make_page.py <repo root> <out.html>"""
import base64, html, os, sys

root, out = sys.argv[1], sys.argv[2]

ITEMS = [
    {
        'title': 'Рука по команде «дай руку» (give_hand)',
        'videos': [('command_give_hand_pair', 'pair.mp4', 'слева give_hand 0, справа 1 (как в файлах)')],
        'compares': 'Напуганное тело лежит закрытым, игрок рядом. В 4,2 с — команда give_hand. Через 0,3 с тело раскрывается и тянет левую руку к открытой ладони игрока. С 5,9 с ладонь идёт к его кисти и в 6,4 с берёт её; ещё через 0,3 с начинается подъём, к 10,9 с тело стоит. Слева та же сцена при выключенном переключателе: команда отклонена, тело лежит.',
        'facts': [('Переключатель', '<code>behaviour.give_hand</code>, 1 в файлах (решение 71, по делегированию)'),
                  ('Цифры', 'рука пошла через 0,30 с (с ней же расходится укрытие, решение 74); до руки игрока 8 см (C39 — 10 см); подъём 4,2 с (медиана C21 на стенде 3,2 с: сценарный помощник отстаёт, и темп клипа падает до 0,32)'),
                  ('Ваше слово', 'смотреть в шлеме: кнопка X, открытая ладонь у плеча, взять кисть и тянуть вверх')],
    },
    {
        'title': 'Ногу, которую взяли, не закрывают (protect_held_leg, решение 73)',
        'videos': [('r10_heldleg_pair', 'pair.mp4', 'волочение за щиколотки: слева правило 0, справа 1 (как в файлах)'),
                   ('r10_heldleg_rising_pair', 'pair.mp4', 'удар 50 Н·с по ноге на коленях: слева 0, справа 1')],
        'compares': 'Две руки берут лежащего за щиколотки и тащат. Вжимаясь в стопы до смыкания захвата, они дают телу 40,6 Н·с, и при правиле 0 оно закрывает ногу: садится, обхватывает голени, и его тащат сидя. С правилом нога, которую взяли, не закрывается: тело лежит, руки на груди. Второе видео — удар по ноге на коленях: укрытие начинается на 0,15 с позже, и рука доходит до ноги за 0,56 с вместо 1,07.',
        'facts': [('Переключатель', '<code>behaviour.protect_held_leg</code>, 1 в файлах (решение 73, по делегированию); выключить: <code>Sandbox.Tune behaviour.protect_held_leg=0</code>'),
                  ('Цифры', 'при 0 все хэши прежние; при 1 сдвигаются 6 сценариев, стенды на трёх сидах (обычный, с игроком рядом, с get_up, с одной рукой) — все 150 прогонов прежние; C34 для ноги на коленях выполнен (0,56 с); C25 — 439, как было'),
                  ('Ваше слово', 'посмотреть; в шлеме — взять лежащего за щиколотки')],
    },
    {
        'title': 'Укрытие отпускается через время реакции (command_cover_react, решение 74)',
        'videos': [('r10_cover_react_pair', 'pair.mp4', 'слева 0, справа 1 (как в файлах); команда get_up в 4,0 с')],
        'compares': 'Напуганное тело лежит, закрыв голову, игрок рядом. В 4,0 с — команда get_up. При 0 укрытие начинает расходиться в тот же тик, что и команда, без реакции, и уходит за 0,5 с; подъём начинается в 4,97 с. При 1 укрытие расходится с 4,3 с, это и есть первое движение в ответ на команду; подъём начинается в 5,27 с.',
        'facts': [('Переключатель', '<code>behaviour.command_cover_react</code>, 1 в файлах (решение 74, по делегированию)'),
                  ('Цифры', 'стенд с игроком рядом и get_up: все 150 отвечают через 0,30 с (было 146 из 150 в пределах 1,0 с, худший 1,74); в конце стоят 149 (было 150: одно тело у мебели встало в 39,0 с и шагнуло в 39,6, на конце испытания)'),
                  ('Ваше слово', 'посмотреть; в шлеме — кнопка A рядом с лежащим')],
    },
    {
        'title': 'Борьба при волочении (behaviour.struggle, решение 72)',
        'videos': [('r10_struggle_ankles_pair', 'pair.mp4', 'за щиколотки: слева борьба 0 (как в файлах), справа 1'),
                   ('drag_lying_wrists_struggles', 'film.mp4', 'за запястья, борьба включена (сценарий включает её сам; снято до 45/70 см)'),
                   ('drag_lying_wrists_still', 'film.mp4', 'за запястья, борьба 0')],
        'compares': 'За щиколотки: тело тащат с 2,7 с. С борьбой в 3,6–4,1 с ноги вырываются из рук, руки игрока соскальзывают со стоп по своему правилу. Тело лежит, перекатывается и с 6,2 с встаёт через четвереньки. Без борьбы его тащат лёжа. За запястья: с борьбой колени согнуты, пятки упираются, но руки не вырываются. Без неё ноги вытягиваются прямо.',
        'facts': [('Переключатель', '<code>behaviour.struggle</code>, 0 в файлах'),
                  ('Цифры', 'одна попытка до конца волочения; ни один захват не сорван; наибольший скачок момента 146 Н·м, как без борьбы (граница C25 439). С решением 73 на 20 сидах: борьба разыграна в 9 из 20 волочений каждого рода; за щиколотки все 9 попыток освобождают тело (оба захвата срываются по своим правилам, скачок до 225 Н·м), за запястья — ни одна; всего 9 из 18, 50%'),
                  ('Ваше слово', 'решение 72 — ваше. Новый вариант (e), рекомендован: включить как есть. При подтягивании кисти до 25 см и стопы до 45 см были перевороты цели до 162°. При 45 и 70 см (теперь в файлах) их нет: в сьюте с борьбой проходят все, кроме пяти, которые включают её сами, скачки до 287 Н·м, двумя руками за грудь вырывается 0 из 150. Включить: <code>Sandbox.Tune behaviour.struggle=1</code>')],
    },
    {
        'title': 'R3 и защита после удара на подъёме',
        'videos': [('protect_hit_rising_trunk_pair', 'pair.mp4', 'слева R3 0 (файлы), справа вариант A')],
        'compares': 'Удар 50 Н·с в грудь стоящему на коленях телу. С вариантом A первой идёт ловля руками, и туловище закрывается позже: в 4,3–5,0 с правая рука ещё вытянута, левая уже у груди.',
        'facts': [('Переключатели', '<code>fall.catch</code>, <code>fall.arm_curve</code>, <code>fall.head</code>, <code>fall.palms</code>, все 0'),
                  ('Цифры', 'туловище закрыто через 0,73 с с A (0,56 без; C34 — 0,6); голова вниз 2,06 м/с (1,95)'),
                  ('Решение', '58 по делегированию: пока нет (C14 руками и C34 хуже)')],
    },
]


def video(tag, name):
    path = os.path.join(root, 'Saved', 'Films', tag, name)
    data = base64.b64encode(open(path, 'rb').read()).decode('ascii')
    return f'<video controls muted loop playsinline preload="metadata" src="data:video/mp4;base64,{data}"></video>'


def esc(text):
    return html.escape(text, quote=True)


cards = []
for item in ITEMS:
    frames = ''.join(f'<figure>{video(tag, name)}<figcaption>{esc(caption)}</figcaption></figure>' for tag, name, caption in item['videos'])
    rows = ''.join(f'<div class="kv"><dt>{esc(k)}</dt><dd>{v}</dd></div>' for k, v in item['facts'])
    paths = ', '.join(f'<code>Saved\\Films\\{esc(tag)}\\{esc(name)}</code>' for tag, name, _ in item['videos'])
    cards.append(f'''<article class="card">
  <h2>{esc(item['title'])}</h2>
  <div class="frames">{frames}</div>
  <p class="cap">{esc(item['compares'])}</p>
  <dl>{rows}</dl>
  <p class="path">{paths}</p>
</article>''')

page = f'''<title>Видео окна 28 ноября</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Inter:wght@400;500;600&family=Manrope:wght@700;800&display=swap">
<style>
/* Layout: one column of cards, each a comparison with its videos side by side, then its figures. */
:root {{ --bg:#f4f5f3; --surface:#ffffff; --fg:#1c2421; --muted:#5d6a64; --line:#d8ded9; --accent:#1f6f8b;
  --display:"Manrope","Inter",system-ui,sans-serif; --body:"Inter",system-ui,-apple-system,"Segoe UI",sans-serif; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --bg:#121615; --surface:#1a201e; --fg:#e3eae6; --muted:#9aaaa3; --line:#2b3532; --accent:#6cc0dc; color-scheme:dark; }} }}
:root[data-theme="dark"] {{ --bg:#121615; --surface:#1a201e; --fg:#e3eae6; --muted:#9aaaa3; --line:#2b3532; --accent:#6cc0dc; color-scheme:dark; }}
* {{ box-sizing:border-box; }}
body {{ background:var(--bg); color:var(--fg); font-family:var(--body); font-size:15px; line-height:1.55;
  padding-left:max(16px, env(safe-area-inset-left, 0px)); padding-right:max(16px, env(safe-area-inset-right, 0px)); padding-block:28px 56px; }}
main {{ max-width:1000px; margin:0 auto; display:grid; gap:26px; }}
h1, h2 {{ font-family:var(--display); margin:0; text-wrap:balance; }}
h1 {{ font-size:28px; }} h2 {{ font-size:19px; }}
.lede {{ color:var(--muted); max-width:70ch; margin:6px 0 0; }}
.card {{ background:var(--surface); border:1px solid var(--line); border-radius:10px; padding:16px; display:grid; gap:12px; min-width:0; }}
.frames {{ display:grid; grid-template-columns:repeat(auto-fit, minmax(280px, 1fr)); gap:12px; }}
figure {{ margin:0; display:grid; gap:6px; min-width:0; }}
video {{ width:100%; max-width:100%; border-radius:6px; background:#000; }}
figcaption, .path {{ font-size:13px; color:var(--muted); }}
.cap {{ margin:0; max-width:75ch; }}
dl {{ margin:0; display:grid; gap:6px; }}
.kv {{ display:grid; grid-template-columns:150px 1fr; gap:10px; }}
dt {{ color:var(--muted); font-size:13px; }} dd {{ margin:0; min-width:0; }}
code {{ font-size:13px; }}
@media (max-width:520px) {{ .kv {{ grid-template-columns:1fr; gap:2px; }} }}
</style>
<main>
<header><h1>Видео окна 28 ноября</h1>
<p class="lede">Что включено за окно и что ждёт вашего слова. Каждая карточка: что сравнивает, переключатель, цифры и решение. Страница VR-сессии: https://claude.ai/artifact/XatvKGwU2Ru7fJ3KE1ASYh</p></header>
{''.join(cards)}
</main>'''
open(out, 'w', encoding='utf-8').write(page)
print('written', out)
