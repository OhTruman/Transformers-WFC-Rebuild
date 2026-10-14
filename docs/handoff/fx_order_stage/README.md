# FX order staging (user report 2026-10-14: "players are always in front of any effects")

Staged with the diagnostic hook in `tools/systems/fxstage` (never part of a patch) on integration 56290b7d,
Streets, RT off, bots frozen, the local pawn 6 m from a bot.

| file | case | status |
|---|---|---|
| 1_smoke_on_bot_56290b7d.png | weapon smoke in front of / behind a bot | verified correct on 56290b7d |
| 2_muzzle_flash_over_bot_56290b7d.png | the local muzzle flash in front of a bot | verified correct on 56290b7d |
| UNCONFIRMED_3_*, UNCONFIRMED_3b_*, UNCONFIRMED_4_* | the barrier grid | UNCONFIRMED: the barrier was forced on a non-Leader pawn (Car4); its opaque look is that staging, not the product |

The barrier case itself was confirmed by Rendering in their working scene (a Leader-owned barrier): a bot behind
the grid is drawn over it. The fix is `docs/handoff/SYSTEMS_FX_ORDER_3A.patch` (against 56290b7d):

- the barrier's material named from its package path (`WEP_Shield_p.Barrier.` + the glTF material name): the export
  has no `extras.wfc_material`, so it fell back to the opaque glTF material;
- 3a: `IRenderer::flushQueuedCharacters()`, called once in `World::draw` before the barrier / sentry meshes and the
  effects (those draw immediately; only skinned characters with a light environment are queued).

Rendering verifies the before / after in their scratch tree. Baseline change in all modes; SIMHASH unaffected.
