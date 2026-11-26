# N7: the rise as fast as the recording (the owner's decision of 26 November, 15:55), written before the runs

Four approaches today ended in one finding: the recorded person passes the forward lean in a moment, this body stays in it and the
world holds it (plank 1.61 s with the 3 s after). The owner chose to let the rise go at the recording's speed, knowing the price: more
help from the world for that half second, and likely fewer first attempts.

## Design (behind getup.fast_rise, 0 = today)
- At the start of a get-up the clip is read for when its hands leave the floor for good: the last time either hand is lower than
  getup.fast_hand_cm above the floor in the clip (scaled to the body). From that clip time on (the rise), the clip does not wait for
  the pelvis or a foot (it still waits for a press, getup.hold_force_n), and runs at its rate.
- In the rise the pelvis's assist may pull with up to getup.fast_force_n and turn with up to getup.fast_torque_nm (in place of 90 N and
  200 N*m), eased in over getup.fast_ease_s.
- Everything else as today: lost at 40 cm, the end at upright within 15 degrees and both feet, stall_s and wait_s.

## Limits (F), on the stand of 150 (three seeds), the suite and films
- F1 the point: median of plank_s + plank_after_s at most half of today's (today 1.61 s: at most 0.81).
- F2 the stand: at least 147 of 150 end standing, at least 127 at the first attempt (today 150, 130).
- F3 the suite: with the switch at 0 the hashes of n2; with it on every scenario passes.
- F4 the get-up's median no longer than today's (5.01 s).
- F5 the assist's mean share while getting up at most 0.35 (today 0.28; the owner accepted more for the rise).
- F6 side videos of getup_back and getup_front for the owner; I do not call them natural.

## Variants, values decided now
- F-a: fast_rise 1, fast_hand_cm 9, fast_force_n 400, fast_torque_nm 500, fast_ease_s 0.15 (the pull of 25 November's try)
- F-b: as F-a with fast_force_n 90, fast_torque_nm 200 (no more help, only no waits in the rise)
No other variant without writing it here first.

## Risks
- Without waits the body falls behind its clip and is lost (40 cm): attempts given up (F2).
- A stronger pull makes a blow during the rise less able to fell the body (getup_hit_strong in the suite, F3).

## Results (16:30)
- Suite with the switch at 0: the hashes of n2.
- F-a (400 N, 500 N*m): stand 150, 135 at the first attempt; plank 0.87 + 0.17 after = 1.04 s (today 1.36 + 0.24 = 1.61); get-up 4.01 s
  (5.01); assist 0.33 (0.28); suite 127. F1 MISSED (1.04 against at most 0.81); F2 to F5 met.
- F-b (90 N, 200 N*m, only no waits): 148, 74; plank 1.41; get-up 3.86; assist 0.27; suite: 4 fail. F1, F2, F3 missed.
- Side videos of whole get-ups, today and F-a: Saved\Films\n7_whole_back_today, n7_whole_back_fast, n7_whole_front_today,
  n7_whole_front_fast (film.mp4 each). For the owner; not called natural.
