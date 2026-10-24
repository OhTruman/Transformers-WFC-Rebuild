# Final gate a2e9fbd9 - pre (headless) PASS; timed rows NOT RUN - 2026-10-24 10:32

Head work/ab/fast_a2e9fbd (a2e9fbd9) vs base work/ab/fast_af4d073_final12 (af4d0731 + FINAL_BASE_ITEMS 01-21 from a61249c0); WFC_ASSETS = work/final_assets (the nine DLC maps), render = work/final_render. Log work/finalgate/a2e9fbd/finalgate_pre.log.

## Same match (headless lockstep 600-9600, seed 1234): head = base on every row; the negative control differs on every row

```
04:29:01 map 508 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:29:23 map 506 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
04:29:40 map 505 same match: lockstep.head.r1: head: steps 1998 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
04:30:24 map 511 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:31:03 map 512 same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:31:24 map MP_ESC_Tranquillity same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
04:31:41 map MP_ESC_District same match: lockstep.head.r1: head: steps 1621 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
04:32:18 map MP_IAC_Horizon same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:32:55 map MP_IAC_Metropolis same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:33:35 map MP_KON_Sector same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 9000 (600-9600), same match as 'base' False
04:47:28 map MP_ESC_Pulse same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
04:47:51 map MP_ESC_Static same match: lockstep.head.r1: head: steps 9000 (600-9600), same match as 'base' True; negative control: lockstep.neg.r1: neg: steps 0 (600-9600), same match as 'base' False
```

(505 Remnant ESC and District ESC end early in both arms - the all-dead loss - and match over the steps run.)

## Stability set (broad-pass -SkipBuild on the head)

```
04:47:57 CAMPAIGNTEST exit 0: CAMPAIGNTEST SUMMARY: 24/24 checks passed
04:47:58 KISMETTEST exit 0: KISMETTEST SUMMARY: 10/10 checks passed
04:48:14 VOICETEST exit 0: VOICETEST SUMMARY: PASS (0 failed)
04:48:17 D5_OME seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
04:48:19 D5_OME seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
04:48:28 A1_IAC seed 7 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
04:48:34 A1_IAC seed 1234 exit 0, steps , errors 0, strands 0, moved back 0, suicides 0, game-over 0, squad 0
04:48:39 ESC MP_ESC_Remnant seed 7 exit 0, kills 0 (world 0), waves 1/0, errors 0, 
04:48:41 ESC MP_ESC_BrokenHope seed 7 exit 0, kills 3 (world 0), waves 1/0, errors 0, game over at wave 1
04:48:41 ESC MP_ESC_Tranquillity seed 7 exit 1, kills 0 (world 0), waves 0/0, errors 2, 
04:48:42 ESC MP_ESC_District seed 7 exit 1, kills 0 (world 0), waves 0/0, errors 2, 
04:48:46 ESC MP_ESC_Remnant seed 1234 exit 0, kills 0 (world 0), waves 1/0, errors 0, 
04:48:49 ESC MP_ESC_BrokenHope seed 1234 exit 0, kills 5 (world 0), waves 1/0, errors 0, game over at wave 1
04:48:49 ESC MP_ESC_Tranquillity seed 1234 exit 1, kills 0 (world 0), waves 0/0, errors 2, 
04:48:49 ESC MP_ESC_District seed 1234 exit 1, kills 0 (world 0), waves 0/0, errors 2, 
04:50:42 TDM: runs 28, non-zero exit 8, not at time 8, errors 16, warns 160
```

The 8 non-zero TDM exits and the Tranquillity / District ESC exit 1 rows are a harness gap: broad-pass.ps1 does not pass WFC_ASSETS, so the four DLC maps it lists (Havoc, Fortress, Tranquillity, District) fail with 'glb: cannot read ExtractedAssets/.../world.glb' at both seeds. All 10 shipped MP maps are clean (rc 0, at time, 0 errors) at both seeds; the DLC maps are covered by the same-match rows above (with the DLC root).

## Timed rows: NOT RUN

The hold was set 05:05; the gate waited in 'waiting for a clear card' until stopped at 10:31 on Integration's order: its busy() check counted the Codex desktop app's long-lived node.exe processes (alive since 23:29), which never exit. No timed row ran; no exe was alive in the window. busy() now excludes those runtimes (this commit). The timed rows need a new slot.
