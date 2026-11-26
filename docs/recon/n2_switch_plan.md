# N2's jump repairs "all but reach" switched on (owner's decision of 26 November 2026, question 2)

Written before the runs. On the head as the default body (commit 55ae1a9, baseline `head`).

## What changes
`Config/Tuning/body.json`: `balance.lift_smooth` 1, `settle_smooth` 1, `release_smooth` 1, `land_ease_s` 0.05, `fall_ease_s` 0.1
(`reach_smooth`, `stagger_brake_follow_s`, `lift_frame_s` stay 0).

## Limits
- Suite green; a scenario that fails is explained, and set anew only where the change is what the repair is for (written down).
  Known before: `effector_hit_head` falls again (by -Tune, earlier today).
- Push matrix 160; knockdowns 8 and 8; stand on three seeds 147 of 150 or more and 129 or more at the first attempt; grab pulls
  93 with at most 3 falls; record-replay -Twice and -Takes, the session's self-test and grab demo pass; tests pass.
- C25: the largest jump under 512 N*m (the baseline the owner set today); by -Tune it was 440, 10 scenarios over 200.
- The films of a hand held on the chest, from the side and the front, with and without: looked at and said plainly.
