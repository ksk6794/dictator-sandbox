"""Writes the content of the owner's page (content.json) from the pairs and their runs' metrics.
   python -I make_content.py <repo root> <out content.json>"""
import json, os, sys

root, out = sys.argv[1], sys.argv[2]
A = 'fall.catch, fall.arm_curve, fall.head, fall.palms = 1'


def fall(tag, side):
    try:
        m = json.load(open(os.path.join(root, 'Saved', 'Films', '%s_%s' % (tag, side), 'metrics.json'), encoding='utf-8-sig'))
    except Exception:
        return None
    f = m['control'].get('fall') or {}
    head = f.get('head_impact_speed_mps')
    return {
        'first': f.get('first_touch_bone') or '-',
        'head': 'not touched' if head is None else ('%.2f m/s' % head) + (' (strikes)' if f.get('head_struck') else ''),
        'ahead': f.get('hands_ahead_first'),
        'rest': f.get('rest_after_s'),
        'energy': (m.get('energy') or {}).get('max_gain_j'),
    }


def facts(tag, switch, look, la, lb):
    a, b = fall(tag, 'a'), fall(tag, 'b')
    rows = [('switch', '<code>%s</code>' % switch), ('look at', look)]
    if a and b:
        rows.append(('head at the floor', '%s: %s &middot; %s: %s' % (la, a['head'], lb, b['head'])))
        rows.append(('first to touch', '%s: <code>%s</code> &middot; %s: <code>%s</code>' % (la, a['first'], lb, b['first'])))
        if a['rest'] is not None and b['rest'] is not None:
            rows.append(('at rest after', '%s: %.2f s, %.1f J &middot; %s: %.2f s, %.1f J' % (la, a['rest'], a['energy'] or 0, lb, b['rest'], b['energy'] or 0)))
    return rows


def item(tag, title, compares, switch, look, la='today', lb='R3 on'):
    return {'tag': tag, 'title': title, 'compares': compares, 'facts': facts(tag, switch, look, la, lb)}


groups = [
    {'title': 'A against today', 'lead': 'Left: the body as the files have it (R3 off). Right: variant A, the four switches proposed. '
     'The same scenario, camera and times; the camera stands 5 m across the path of the fall.',
     'items': [
        item('r3A_blow_fwd', 'A blow of 200 N·s from behind: forward', 'The body is thrown forward. With A both arms are placed toward where the floor will be and the hands land ahead of the chest.', A, 'the arms going forward before the floor, the head turned back, the hands giving way as they land'),
        item('r3A_blow_back', 'A blow of 200 N·s on the chest: backward', 'Backward the catch is not made (fall.catch_back 0: the hands behind the hips gained 37 to 50 J); the arms keep the written pose, the neck is braced and the chin drawn in.', A, 'the chin in, the head kept off the floor'),
        item('r3A_blow_side', 'A blow of 200 N·s from the right: to the left side', 'The near arm reaches for the floor under the shoulder; the far arm keeps the fall pose.', A, 'the near hand first, the ear away from the floor'),
        item('r3A_blow_diag', 'A blow of 200 N·s: forward and to the left', 'A diagonal fall: both arms placed, the near one first.', A, 'the palms toward the floor at the touch'),
        item('r3A_blow_backdiag', 'A blow of 200 N·s: forward and to the right', 'The other diagonal, the right hand first.', A, 'how the arm gives way after the touch'),
        item('r3A_hand_fwd', 'A shove by two fists from behind (4 m/s): forward dive', 'The hardest case: the body is thrown nearly level and reaches the floor 0.31 s after it becomes a fall. The arms do not get forward in time; the face meets the floor in both, faster with A in this fall (5.4 against 3.9 m/s). This is why C16 by hand is not met.', A, 'the arms trailing back, the face reaching the floor'),
        item('r3A_hand_back', 'A shove by two fists from the front: backward', 'Backward by hand: the written pose\'s arms, the braced neck.', A, 'the head'),
        item('r3A_hand_diag', 'A shove: forward and to the left', 'A diagonal shove by hand.', A, 'the hands landing'),
        item('r3A_hand_fwddiag', 'A shove: forward, a little to the left', 'A forward-diagonal shove by hand: with A a hand lands ahead of the chest and the head barely touches the floor.', A, 'where the hands land against the chest'),
        item('r3A_head80_fwd', 'A push of 80 N·s on the head from behind', 'A smaller impulse that fells the body (the head knocked over; the chest\'s 80 and 120 N·s are stepped off).', A, 'the whole fall from a lighter hit'),
        item('r3A_head80_back', 'A push of 80 N·s on the head from the front', 'The same backward.', A, 'the head'),
        item('r3A_kd180', 'The knockdown that rests latest with A (200 N·s, 180 degrees)', 'Decision 7: the knockdowns\' slowest rest goes from 1.87 s to 2.94 s (within the 3 s). The body lands on its side on the catching arm and then rolls slowly toward its front over that arm; today it stays on its side. The same roll keeps three blows of 160 N·s of the 96 from rest for 3.1 to 4.0 s.', A, 'the slow roll after the landing'),
        item('r3A_kd090', 'The second latest (200 N·s, 90 degrees)', 'Rests after 2.76 s with A.', A, 'the settling after the landing'),
     ]},
    {'title': 'B against A', 'lead': 'Left: A. Right: B, which adds decision 37\'s blend of the sample\'s reaching poses over the arms (fall.blend 1). '
     'B brings the head\'s speed by hand down (0.52 m/s at the 90th percentile of the scorecard, 5.38 with A) and a hand first in more of the falls by hand on the larger set (72% against 44%), but lets more bodies get up under a press of 300 N (128 of 150 against 97) and two blows rest after 5.0 and 6.4 s.',
     'items': [
        item('r3B_blow_fwd', 'Forward, 200 N·s', 'The reaching poses give the arms a forward start; the catch places them.', 'A and fall.blend = 1', 'the arms before the floor', 'A', 'B'),
        item('r3B_blow_diag', 'Forward and to the left, 200 N·s', 'The diagonal: here B lands on a thigh first and the head strikes (A keeps it off).', 'A and fall.blend = 1', 'the arms and the head', 'A', 'B'),
        item('r3B_hand_fwddiag', 'A shove forward, a little to the left', 'Both land a hand ahead of the chest.', 'A and fall.blend = 1', 'where the hands land', 'A', 'B'),
     ]},
    {'title': 'Each item alone', 'lead': 'One switch at a time, so that each can be judged on its own.',
     'items': [
        item('r3_item_catch', 'b, the catch alone', 'The arms placed in the trunk\'s frame toward where the floor will be, blended by their joints, never chasing a point of the world.', 'fall.catch = 1', 'the arms; nothing else changes', 'today', 'catch'),
        item('r3_item_head', 'c, the head alone', 'The neck braced (3 times) and turned away from the floor by the way. On 96 falls this alone halves the head strikes by hand (44% to 22%).', 'fall.head = 1', 'the head\'s turn and how it meets the floor', 'today', 'head'),
        item('r3_item_curve', 'a, the arms\' strength on the time to impact', 'With the catch on both sides; right, the arms give way (soft and damped) the moment a hand touches. Bracing before the floor was measured and dropped: it held the body up, then gave, and the head struck more.', 'fall.catch = 1 and fall.arm_curve = 1', 'the arms after the touch', 'catch', 'catch and curve'),
        item('r3_item_palms', 'f, the palms to the floor', 'The wrist bent back by 0.6 of a right angle: the palm\'s angle to the floor at the touch from 81 to 63 degrees in the median. The fingers were spread already (decision 53).', 'fall.palms = 1', 'the hands at the touch', 'catch and curve', 'and palms'),
     ]},
    {'title': 'Built, not proposed', 'lead': 'Measured and kept at 0: they did not help.',
     'items': [
        item('r3_item_curl', 'd, curling up after a hard blow', 'Curled when the blow that felled it was 150 N·s or more (your 200 N·s). Arms in, nothing slows the trunk: on 41 blows the head struck more (34% against 27%) and a fall gained 67 J.', 'fall.curl = 1, fall.curl_from_ns = 150', 'the curl', 'A', 'A and curl'),
        item('r3_item_sit', 'e, sitting down going backward', 'The hips and knees fold earlier. It added 37 to 49 J of the muscles\' work and kept no head off the floor that A did not.', 'fall.sit = 1', 'the pelvis landing first', 'A', 'A and sit'),
        item('r3_item_turn_back', 'e, turning to a side: backward and to the right', 'With 0.35 s or more to the floor and a way that leans to a side, the trunk twists toward that side before the floor. It runs in few falls, and where it ran the head struck more.', 'fall.turn = 1', 'the twist before the floor', 'A', 'A and turn'),
        item('r3_item_turn_fwd', 'e, turning to a side: forward and to the left', 'The same going forward, twisted the other way round.', 'fall.turn = 1', 'the left shoulder', 'A', 'A and turn'),
     ]},
]

content = {
    'title': 'R3 Fall Review',
    'eyebrow': 'DictatorSandbox · R3, the fall · night of 27 to 28 November 2026',
    'heading': 'The fall as the references drive it: what to look at, what to decide',
    'lead': [
        'R3 is built behind switches that are all 0 in the files: with them off every one of the 168 earlier scenarios keeps its hashes. '
        'Nothing is switched on. Each video below puts today\'s body (or the variant named) on the left and the switch on the right.',
        'Proposed: variant <b>A</b>, four switches: the catch in the trunk\'s frame, the arms giving way at the touch, the head kept off the '
        'floor, the palms toward the floor. Variant <b>B</b> adds the reaching poses of decision 37. The curl and the sit were built and '
        'are not proposed.',
    ],
    'decisions_lead': 'Yours to decide after the videos, and the ones taken for you in the night (to confirm or reverse).',
    'decisions': [
        {'kind': 'yours', 'label': 'yours', 'title': 'Switch R3 on: A, B, or not yet',
         'text': 'Recommended: <b>A</b> (<code>fall.catch</code>, <code>fall.arm_curve</code>, <code>fall.head</code>, <code>fall.palms</code> = 1). '
                 'On the scorecard the head strikes in 0% of the falls by blows (63% today) and 9% by hand (55%); on 96 more falls 29% by blows (29%) and 16% by hand (44%). '
                 'It leaves the get-up\'s figures as they are. B also brings the head\'s speed by hand down, but a press of 300 N holds fewer bodies down (128 of 150 stand under it, 98 today) and two blows rest after 5.0 and 6.4 s.'},
        {'kind': 'yours', 'label': 'yours, 58', 'title': 'C16 by hand, and C14 by hand on more falls, are not met',
         'text': 'A shove of two fists at 4 m/s throws the body nearly level; it reaches the floor 0.31 s after it falls, and no arm gets forward in that time: the dive\'s face meets the floor at 5.4 m/s with A (3.9 today). '
                 'On the scorecard\'s six falls by hand a hand lands first and ahead in 83% (as today); on the 18 of the larger set in 44% (61% today; 80% asked). '
                 'Recommended: switch on with these written as known; an earlier answer belongs to the stagger, before the fall (R4\'s reflexes, R5).'},
        {'kind': 'yours', 'label': 'yours', 'title': 'Remove N6\'s fall.aim',
         'text': 'R3\'s catch replaces it, as planned; it is still in the code and off. Recommended: remove it once R3 is on.'},
        {'kind': 'taken', 'label': 'taken, 55', 'title': 'The values A switches on',
         'text': 'The arms\' strength flat until the touch, soft at it; no catch backward; the catch let go when the fall turns its way by 60 degrees; the neck braced 3 times, the head turned 35/50/35 degrees; the palm bent back by 0.6.'},
        {'kind': 'taken', 'label': 'taken, 56', 'title': 'The catching arm blended by its joints',
         'text': 'Blended by where the hand is, the elbow\'s target turned over by up to 175 degrees in a frame. By the joints, with the elbow bent about its own anatomical axis: no arm\'s target over 52 degrees in a frame in 120 falls, the head struck in 9% of the scorecard\'s falls by hand. <code>fall.catch_joint_blend</code> switches it.'},
        {'kind': 'no', 'label': 'taken, 57', 'title': 'The curl, the sit and the turn kept at 0',
         'text': 'Both measured worse; the videos at the bottom show them.'},
    ],
    'figures': {
        'title': 'The gate, measured after the last change',
        'lead': 'Scorecard falls (8 blows of 200 N·s, 16 shoves by hand), 96 more falls (blows of 160 and 240 N·s, shoves at 3.5 and 4.5 m/s, 24 directions), the stand of 150 on three seeds, the push matrix; after two independent readings of the code and their fixes. Limits: C14 80% or more, C15 15% or less, C16 2.0 m/s by hand and under 3.2 by blows, C17 at rest within 3 s and 40 J.',
        'head': ['', 'today', 'A', 'B'],
        'rows': [
            ['C14 a hand first, ahead of the chest: by hand / by blows', '83% / 100%', '<b>83%</b> / 100%', '67% / 100%'],
            ['C15 the head strikes: by hand / by blows', '55% / 63%', '<b>9%</b> / <b>0%</b>', '<b>9%</b> / 25%'],
            ['C16 the head\'s speed, 90th percentile: by hand / by blows', '4.16 / 3.00 m/s', '5.38 / <b>0.19</b>', '<b>0.52</b> / <b>1.56</b>'],
            ['C17 at rest, energy (blows; by hand)', '1.87 s, 14.2 J; 2.53 s, 32.1 J', '2.94 s, 15.3 J; 2.42 s, 27.5 J', '2.30 s, 20.1 J; 2.39 s, 29.6 J'],
            ['96 more falls by hand: C14 / C15 / C16', '61% / 44% / 5.00', '44% / 16% / 5.52', '72% / 12% / 2.96'],
            ['96 more falls by blows: C14 / C15 / C16; the slowest rest', '62% / 29% / 5.23; 3.56 s', '85% / 29% / 4.02; 3.96 s', '75% / 20% / 3.21; 6.43 s'],
            ['C25 the largest command jump', '439 N·m', '439 N·m', '439 N·m'],
            ['Push matrix', '160', 'same outcome in all 160', 'same outcome in all 160'],
            ['Stand of 150: standing, first attempt', '150, 143', '150, 144', '150, 142'],
            ['One hand pulled free / pressed 300 N stood under it', '31 / 98', '46 / 97', '55 / 128'],
            ['The three C26 scenarios (a hit, a push, a hold while it catches)', '-', 'pass', 'pass'],
        ],
    },
    'groups': groups,
}
json.dump(content, open(out, 'w', encoding='utf-8'), indent=1, ensure_ascii=False)
print('content written')
