"""R3's gate figures for one variant, read from the runs of the check set and the stands.
   python -I gate.py <repo root> <check label> <knockdowns by hand folder> <hold1 label> <press label> [<hold2 label>]"""
import glob, json, os, re, sys

root, label, kdh, hold1, press = sys.argv[1:6]
hold2 = sys.argv[6] if len(sys.argv) > 6 else None
J = lambda p: json.load(open(p, encoding='utf-8-sig'))

print('==', label)
for line in open(os.path.join(root, 'Saved', 'Recon', 'r3', 'checks_%s.txt' % label), encoding='utf-8-sig'):
    if line.strip().startswith(('suite', 'matrix', 'knockdowns', 'seeds', 'natural_quick', 'CHECKS')):
        print('  ' + line.strip()[:200])
base = {}
for f in glob.glob(os.path.join(root, 'Saved', 'Baselines', 'r3base', '**', 'metrics.json'), recursive=True):
    m = J(f); base[m['scenario']] = m['hashes']['bodies']
same = moved = 0; jumps = []; fails = []
for f in glob.glob(os.path.join(root, 'Saved', 'Suites', 'checks_' + label, '**', 'metrics.json'), recursive=True):
    m = J(f); p = m['control'].get('pose') or {}
    if m['scenario'] in base:
        same += base[m['scenario']] == m['hashes']['bodies']; moved += base[m['scenario']] != m['hashes']['bodies']
    bad = [c.get('name') for c in m['checks'] if not c.get('pass') and c.get('name') != 'tuning_changes']
    if bad: fails.append((m['scenario'], bad))
    if not m['scenario'].startswith(('measure_', 'tune_')):
        jumps.append((p.get('command_jump_max_nm') or 0, p.get('target_step_max_deg') or 0, m['scenario']))
print('  suite: fails beyond tuning_changes', fails, '; hashes of r3base kept', same, 'moved', moved)
print('  C25 jump %.1f N*m; largest target step %.1f deg (%s)' % (max(jumps)[0], max(j[1] for j in jumps), max(jumps, key=lambda j: j[1])[2]))
def outcomes(d):
    r = {}
    for f in glob.glob(os.path.join(d, '**', 'metrics.json'), recursive=True):
        m = J(f); r[m['scenario']] = (m['control'].get('end_state'), m['metrics'].get('fell'), m['control'].get('recovery_step_count'))
    return r
a = outcomes(os.path.join(root, 'Saved', 'Matrix', 'push', 'runs')); b = outcomes(os.path.join(root, 'Saved', 'Checks', label, 'matrix', 'runs'))
print('  matrix: outcome differs in', len([k for k in a if a[k] != b.get(k)]), 'of', len(a))
r = J(os.path.join(root, 'Saved', 'Natural', 'checks_quick_' + label, 'report.json'))
for row in r.get('rows') or r.get('criteria'):
    if row.get('id') in ('C14', 'C15', 'C16', 'C17'):
        print('  %s %s | %s' % (row['id'], row['verdict'], row['value']))
seeds = open(os.path.join(root, 'Saved', 'Checks', label, 'seeds.txt'), encoding='utf-8-sig').read()
print('  stand:', seeds.splitlines()[0][:140])
print('  ', re.search(r"R2's gate: [^\n]*", seeds).group(0)[:230])
print('   C21 medians', re.findall(r'first standing: median ([0-9.]+) s', seeds))
def stand(lab, pat):
    # The stand's printed lines, kept by the caller under Saved\Recon\r3 (the file whose first lines name the label).
    txt = ''
    for f in glob.glob(os.path.join(root, 'Saved', 'Recon', 'r3', '*.txt')):
        if lab in open(f, encoding='utf-8-sig', errors='ignore').read(4000):
            txt = open(f, encoding='utf-8-sig', errors='ignore').read(); break
    nums = [int(x) for x in re.findall(pat, txt)]
    first = txt.splitlines()[0][:110] if txt else '-'
    return sum(nums), nums, first
print('  one hand pulled free', stand(hold1, r'(\d+) pulled free'))
print('  pressed 300 N stood under it', stand(press, r'(\d+) of 50 bodies stood while'))
if hold2:
    print('  two hands pulled free', stand(hold2, r'(\d+) pulled free'))
