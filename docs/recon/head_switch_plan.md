# The head as the default body (owner's decision of 26 November 2026, question 1 of the night's report)

Written before the runs.

## What changes
1. `Specs/physics/npc_body.json` gets the head's spec (output still `PA_SandboxNPC`); today's body is kept beside it as
   `npc_body_neck.json` -> `PA_SandboxNPCNeck`; `npc_body_head.json` goes. Every scenario and tool keeps naming `PA_SandboxNPC`.
2. `Config/Tuning/body.json`: `assist.head.angular_nm_per_rad` 10 -> 0, `muscles.neck.damping_s` 0.1 -> 0.05 (the values the
   night measured the head with).
3. What a scenario or tool does to `neck_01` meaning the head it does to `head` (what the night's `SANDBOX_HEAD_BODY=head` did in
   copies): `hit_head_30_stands`, the push matrix's, the compliance table's and the grab pulls' bodies, the stand's blows on the
   head; `measure_push_neck`, `measure_drop_head_first` with the eight below. Pose joints (`poses.*.neck_01`) stay: a pose's neck
   angles are shared out between the two joints.
4. `SANDBOX_HEAD_BODY` turns round: it names the bone that stands for the head in an asset whose head is part of the neck
   (`PA_SandboxNPCNeck`, `PA_SandboxNPCWrist`, `PA_SandboxNPCToes`), and a scenario's `head` becomes that bone in the copies.
5. The eight scenarios that the night found failing on the head are set anew, each written down with what changed and why
   (`docs/testing.md`, N8 A): effector_hit_head, palm_slap_head, measure_drop_head_first, measure_push_neck,
   strike_punch_shoulder, room_movable_fall_gets_up, tune_live_file, tune_live_override.

## Limits (the night's figures on the head, to be met again on the default body)
- Every scenario not edited ends with the hashes the night's battery `bat_head2` had for it (the same asset and tuning).
  A scenario that differs is explained before anything else.
- Push matrix 160 of 160; knockdowns 8 of 8 by a blow and 8 of 8 by hand; stand on three seeds 147 of 150 or more, 129 or more
  at the first attempt (the night: 150, 134); grab pulls 93 with at most 3 falls (the night: 0); standing within a tenth of
  today's drift, feet's speed and assist (A1).
- Tests pass; suite all green after the eight are set anew; record-replay -Twice and -Takes, the session's self-test and
  grab demo pass; the bootstrap passes.
- A new baseline `head` once all of this holds.

## Risks
- The light neck is whipped when the head strikes the floor (37 to 46 rad/s where today has 16 to 36): known, accepted by the
  owner with the decision; any scenario's joint-speed limit it breaks is listed, not raised in silence.
- A scenario written for today's head shape (a point of the world struck) misses the narrower head: listed with the eight.
