# Transient lights windowed check (integration 76b077e8, SLOT B 2026-10-16)

Run: tools/systems/tl_ab.ps1 (Molten TDM 4 v 5 + playerbot, lockstep, seed 7, 45 s, WFC_SHOTMATCH steps 600-1800 every 20,
muted + NOINTERACT) with lights on vs WFC_NOTRANSIENTLIGHTS=1 at WFC_RT_MODE 2 (Realistic) and 0 (Off); tools/systems/tl_diff.py
pairs the frames. Each crop: lights on | off | difference x4.

| mode | frames lit (of 61) | max share of brighter pixels | darker pixels (max) |
|---|---|---|---|
| RT_MODE 2 | 22 | 23 % (+60-80/255 where lit) | 1.6 % (the RT shadows: the robot's own back and its floor shadow cut the added light) |
| RT_MODE 0 | 22 | 26 % (unshadowed: the robot's far side is lit too) | 0.01 % |

Verdict: PASS. The muzzle lights light the floor and the shooter in both modes, are shadowed in Realistic only, and are absent
with WFC_NOTRANSIENTLIGHTS=1. Faithful was not run separately.
