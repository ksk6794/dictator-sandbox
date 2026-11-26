# Recon: plans and patches the journal cites

Moved here from `Saved\Recon` on 26 November 2026 (the owner's decision), so that a clean clone has them. `docs/testing.md`
and the plans below still name them as `Saved\Recon\<name>`; that is this folder. Run outputs under `Saved\` (batches, films,
suites) stay on the reference machine.

| File | What it is |
| --- | --- |
| `window_plan.md` | Plan, risks and limits of the unattended window of 25 to 26 November (N8 A, N2, N3, N6, N4, N5), written before the runs |
| `head_switch_plan.md` | The head as the default body (26 November), written before the runs |
| `n2_switch_plan.md` | N2's jump repairs switched on (26 November), written before the runs |
| `n7_getup_plan.md` | N7 first: a get-up without pauses, its variants and limits (26 November) |
| `n7_unload_plan.md` | N7: unload, then step (26 November; not met, taken out) |
| `n7_rise_plan.md` | N7: the rise from a crouch by the body itself (26 November; stopped before the stand) |
| `n7_handover_plan.md` | N7: catch the lean with a step (26 November; not met, taken out) |
| `n7_fast_plan.md` | N7: the rise at the recording's speed (26 November; not met) |
| `n7_carried_plan.md` | N7: the recording, carried, the forerunner of R1 (26 November) |
| `m4_part5_body.patch` | M4 part 5, first step: the player's body as a capsule (25 November). Not applied; whether it keeps every hash is not checked |
| `n7_unload_step_full.patch` | The code of "unload, then step", taken out |
| `n7_hand_over.patch` | The code of "catch the lean with a step", taken out |
| `getup_step_hold.patch` | The get-up's "foot held until it is free" of 25 November, against a backup of that day |
| `getup_kneel_attempt.patch` | A get-up from the knees by stages (`getup.rise` of 25 November), not kept |
| `trial_corrections.patch` | Diagnostic switches by environment variable (`RECON_FIX`, marked `RECON-TEMP`), used to find the causes of C25's jumps; never to be applied as it is |
| `getup_pauses.py` | Reads a session's `run.log` and lists each get-up's lying time, attempts and pauses (path as its first argument) |

A patch applies to the tree of the day it was made; check with `git apply --check` before using one.
