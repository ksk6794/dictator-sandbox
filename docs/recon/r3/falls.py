"""Summary of falls from runs' metrics.json: one row a run, then the shares C14-C17 judge.
   python -I falls.py <folder with runs> [<folder> ...]
Reads every metrics.json below each folder (knockdowns, knockdowns_by_hand, a scorecard's runs)."""
import json, os, sys, glob, statistics


def get(o, path, default=None):
    for k in path.split('.'):
        if isinstance(o, dict) and k in o:
            o = o[k]
        else:
            return default
    return o


def rows(folder):
    out = []
    for f in sorted(glob.glob(os.path.join(folder, '**', 'metrics.json'), recursive=True)):
        try:
            m = json.load(open(f, encoding='utf-8-sig'))
        except Exception:
            continue
        fall = get(m, 'control.fall') or {}
        name = os.path.basename(os.path.dirname(f))
        states = [s.get('state') for s in (get(m, 'control.states') or []) if isinstance(s, dict)]
        fell = get(m, 'metrics.fell')
        started = get(m, 'control.fall.started_t')
        out.append(dict(
            name=name,
            fell=fell,
            way=get(m, 'control.fall.way') or get(m, 'control.fall_way'),
            first=fall.get('first_touch_bone'),
            ahead=fall.get('hands_ahead_first'),
            hfirst=fall.get('hands_first'),
            struck=fall.get('head_struck'),
            touched=fall.get('head_touched'),
            head=fall.get('head_impact_speed_mps'),
            hand=fall.get('hand_impact_speed_mps'),
            pelvis=fall.get('pelvis_impact_speed_mps'),
            t_hand=fall.get('time_to_hand_s'),
            t_trunk=fall.get('time_to_trunk_s'),
            rest=get(m, 'control.fall.rest_after_s'),
            energy=get(m, 'energy.max_gain_j'),
            jump=get(m, 'control.pose.command_jump_max_nm'),
            foreseen=get(m, 'control.fall.sense.foreseen_s'),
            ok=m.get('ok'), passed=m.get('pass'), started=started, dirdeg=get(m, 'control.fall.direction_deg'), imp=get(m, 'control.fall.sense.impact_s'),
        ))
    return out


def fmt(v, f='{:.2f}'):
    if v is None:
        return '-'
    if isinstance(v, bool):
        return 'y' if v else 'n'
    if isinstance(v, (int, float)):
        return f.format(v)
    return str(v)


args = []
for a in sys.argv[1:]:
    if a.endswith('*'):
        args += sorted(d for d in glob.glob(a) if os.path.isdir(d))
    else:
        args.append(a)
brief = os.environ.get('BRIEF') == '1'
for folder in args:
    rs = rows(folder)
    print('==', folder, len(rs), 'runs')
    print('{:34s} {:5s} {:12s} {:5s} {:5s} {:5s} {:6s} {:6s} {:6s} {:6s} {:6s} {:6s} {:6s}'.format(
        'run', 'fell', 'first', 'ahead', 'strk', 'touch', 'head', 'hand', 'tHand', 'tTrunk', 'rest', 'J', 'TTI0'))
    for r in ([] if brief else rs):
        print('{:34s} {:5s} {:12s} {:5s} {:5s} {:5s} {:6s} {:6s} {:6s} {:6s} {:6s} {:6s} {:6s}'.format(
            r['name'][-34:], fmt(r['fell']), fmt(r['first'])[:12], fmt(r['ahead']), fmt(r['struck']), fmt(r['touched']),
            fmt(r['head']), fmt(r['hand']), fmt(r['t_hand']), fmt(r['t_trunk']), fmt(r['rest']), fmt(r['energy'], '{:.1f}'),
            fmt(r['foreseen'])))
    fallen = [r for r in rs if r['first'] or r['t_trunk'] is not None]
    if fallen:
        struck = [r for r in fallen if r['struck']]
        heads = sorted([r['head'] for r in fallen if r['head'] is not None])
        p90 = heads[max(int(-(-0.9 * len(heads) // 1)) - 1, 0)] if heads else None
        aheads = [r for r in fallen if r['ahead'] is not None]
        print('falls {}: head struck {}/{} ({:.0f}%), head touched {}, head speed p90 {}, max energy {}, max rest {}, hands ahead first {}/{}'.format(
            len(fallen), len(struck), len(fallen), 100.0 * len(struck) / len(fallen), sum(1 for r in fallen if r['touched']), fmt(p90),
            fmt(max([r['energy'] or 0 for r in fallen]), '{:.1f}'), fmt(max([r['rest'] or 0 for r in fallen])),
            sum(1 for r in aheads if r['ahead']), len(aheads)))
        def way(r):
            a = r['dirdeg']
            if a is None: return '?'
            if 225 < a < 315: return 'back'
            if 45 < a < 135: return 'forward'
            return 'side'
        for kind, pre in (('hand', ('hand_', 'shove')), ('blow160', ('knockdown_spine_04_000_160',) + tuple('knockdown_spine_04_%03d_160' % a for a in range(0, 360, 15))), ('blow240', tuple('knockdown_spine_04_%03d_240' % a for a in range(0, 360, 15))), ('blow', ('blow_', 'knockdown_'))):
            fs = [r for r in fallen if r['name'].startswith(pre) and r['started'] is not None]
            if not fs: continue
            nb = [r for r in fs if way(r) != 'back']
            c14 = sum(1 for r in nb if r['ahead']) / len(nb) if nb else 0
            c14any = sum(1 for r in nb if r['hfirst']) / len(nb) if nb else 0
            c15 = sum(1 for r in fs if r['struck']) / len(fs)
            hs = sorted([r['head'] for r in fs if r['head'] is not None])
            p90 = hs[max(int(-(-0.9 * len(hs) // 1)) - 1, 0)] if hs else None
            print('  {}: C14 {:.0f}% of {} ({:.0f}% anywhere), C15 {:.0f}% of {}, C16 p90 {}, energy max {:.1f}, rest max {:.2f}'.format(
                kind, 100*c14, len(nb), 100*c14any, 100*c15, len(fs), fmt(p90), max(r['energy'] or 0 for r in fs), max(r['rest'] or 0 for r in fs)))
