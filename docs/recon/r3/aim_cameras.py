"""Points each pair's camera across the fall: from the B side's run, the fall's direction and distance (control.fall), the camera 5 m
off the middle of the path, perpendicular to it, at 110 cm, looking at the path's middle at 60 cm.
   python -I aim_cameras.py <repo root> <pairs.json in> <pairs.json out>"""
import json, math, os, sys

root, src, dst = sys.argv[1:4]
pairs = json.load(open(src, encoding='utf-8'))
for p in pairs:
    run = os.path.join(root, 'Saved', 'Films', p['tag'] + '_b', 'metrics.json')
    try:
        m = json.load(open(run, encoding='utf-8-sig'))
        f = m['control']['fall']
        angle = math.radians(f['direction_deg']); dist = f['distance_cm'] or 100.0
    except Exception as e:
        print('no fall for', p['tag'], e); continue
    loc = m.get('body', {}).get('location_cm') if isinstance(m.get('body'), dict) else None
    sx, sy = 0.0, 0.0
    scen = json.load(open(os.path.join(root, p['file']), encoding='utf-8-sig'))
    if 'location_cm' in scen.get('body', {}):
        sx, sy = scen['body']['location_cm'][0], scen['body']['location_cm'][1]
    half = min(dist, 220.0) * 0.5
    cx, cy = sx + math.cos(angle) * half, sy + math.sin(angle) * half
    # Perpendicular to the path, on the side away from the room's middle being irrelevant on the bare floor: the left of the path.
    px, py = -math.sin(angle), math.cos(angle)
    cam = (cx + px * 500.0, cy + py * 500.0, 110.0)
    p['camera'] = '%.0f,%.0f,%.0f;%.0f,%.0f,60' % (cam[0], cam[1], cam[2], cx, cy)
    print(p['tag'], round(f['direction_deg']), round(dist), p['camera'])
json.dump(pairs, open(dst, 'w', encoding='utf-8'), indent=1)
