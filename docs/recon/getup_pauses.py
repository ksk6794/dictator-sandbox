"""The get-ups of the headset session of 26 November, from its log: how long the body lay before it began, how long each
attempt took, and how much of it the clip stood still waiting for the body."""
import re, sys
sys.stdout.reconfigure(encoding='utf-8')
log = open(sys.argv[1] if len(sys.argv) > 1 else 'Saved/Play/room_stand_npc/run.log', encoding='utf-8', errors='replace').read().splitlines()
lines = [l[l.index('Body controller: ') + 17:] for l in log if 'Body controller: ' in l]
falls = []
cur = None
for l in lines:
    m = re.match(r'(falling|lying|standing|stagger|flinch|get_up|getting up) at ([0-9.]+) s', l)
    if l.startswith('falling at'):
        cur = {'fall': float(m.group(2)), 'lying': None, 'attempts': [], 'stood': None}
        falls.append(cur)
    elif cur is None:
        continue
    elif l.startswith('lying at') and cur['lying'] is None:
        cur['lying'] = float(m.group(2))
    elif l.startswith('getting up at'):
        g = re.match(r'getting up at ([0-9.]+) s: clip ([0-9.]+) s', l)
        t, clip = float(g.group(1)), float(g.group(2))
        if not cur['attempts'] or (clip == 0.0 and cur['attempts'][-1]['samples'] and cur['attempts'][-1]['samples'][-1][1] > 0.0):
            cur['attempts'].append({'samples': []})
        cur['attempts'][-1]['samples'].append((t, clip))
    elif l.startswith('standing at') and 'got up' in l and cur['stood'] is None:
        cur['stood'] = float(m.group(2))
        cur['took'] = float(re.search(r'got up in ([0-9.]+) s', l).group(1))
print('falls with a get-up:', sum(1 for f in falls if f['attempts']))
for f in falls:
    if not f['attempts']:
        continue
    first = f['attempts'][0]['samples'][0][0]
    lay = (first - f['lying']) if f['lying'] is not None else None
    parts = []
    for a in f['attempts']:
        s = a['samples']
        still = 0.0
        for (t0, c0), (t1, c1) in zip(s, s[1:]):
            if c1 <= c0 + 1e-6:
                still += t1 - t0
        parts.append('%.1f s long, clip stood %.1f s of it' % (s[-1][0] - s[0][0], still))
    print('fall at %6.2f: lay %s s before the get-up began; %d attempt(s): %s; stood %s' % (
        f['fall'], '%.2f' % lay if lay is not None else '?', len(f['attempts']), '; '.join(parts),
        'after %.2f s' % f['took'] if f['stood'] else 'not'))
