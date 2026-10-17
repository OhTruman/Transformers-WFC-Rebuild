# Presentation audit 3: versus HUD, end-of-match flow, front-end UI

WFC RE lane, 2026-10-18. Parts are added in order (1), (3), (2), with a commit after each.

**Columns:** **Ours = C** means the entry point's name is invoked as a quoted callback in `Rebuild/src`. That means *invoked, not
visually faithful*. **-** = not invoked.

**Scope:** online-only flows (lobbies, matchmaking, party, gamer cards, bad-connection alerts) are out. Only their shared local
presentation is covered.

## Part (1): versus HUD during a match

**Sources:**
- script: TnHUD (21 movie callbacks), its 32 `TnHudDataObserver*` (callback per observed value), and the `TnHUD` senders;
- ActionScript: tools/as2dis on Hud_GFX, PlayerList_GFX (scoreboard), MultiplayerRespawn_GFX, ChallengeNotify_GFX,
  PauseMenu_GFX, InGameStats_GFX, PreGameCountdown_GFX.

Generator: `tools/gfx_entry_audit.py` (per entry: sounds, interp animations, timeline jumps, attached symbols plus *their* own
sprite script). Data: `notes/data/hud_entry_audit.json`.

**Not in the original (CONFIRMED absence):**
- **No radar or minimap.** There are no radar / minimap / blip symbols or code in any HUD movie. Positional awareness is the
  objective-marker system (`UpdateMarker`, objective-marker doc) and damage arrows.
- Scoreboard, respawn, pregame countdown and in-game stats are separate movies.

| Movie | Entry point | Called by / payload | Ours | Sound(s) | Animation (interp target time easing {props}) | Spawned element (its own animation / sound) |
|---|---|---|---|---|---|---|
| Hud_GFX | AbilityType0Jammed | AS-internal | - | - | - | - |
| Hud_GFX | AbilityType1Jammed | AS-internal | - | - | - | - |
| Hud_GFX | AbilityType2Jammed | AS-internal | - | - | - | - |
| Hud_GFX | AddObjective | script: TnHUD.AddObjective(desc) | C | HUD_OBJECTIVE_ADDED | - | mc_objectiveNew{GlyphLine 0.5s easeout {_yscale: 0, _alpha: 0}; ObjectiveLine 0.5s easeout {_yscale: 100, _alpha: 100}; GlyphLine 0.5s easeout {_alpha: 100, _x: 0}} |
| Hud_GFX | ApplyBuff | script: TnHUD.ApplyBuff(BuffId, BuffType, BuffTime) | C | - | - | - |
| Hud_GFX | ApplyKillstreakBuff | AS-internal | - | - | KillstreakBuffs[i] 0.3s easeout {_y: (i * IconSpacing)} | mc_killstreakBuffIcon{thisElement 0.4s easeout {callback: removeMe, _yscale: 150, _xscale: 150, _alpha: 0}} |
| Hud_GFX | CausedDamage | script: TnHUD.NotifyCausedDamage() | C | HUD_POSITIVE_HIT_INDICATOR | - | mc_causeDamage |
| Hud_GFX | DisplayRangeFinder | AS-internal | - | - | - | mc_rangeFinderCrosshair{thisElement 0.5s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | DisplayTurretReticule | AS-internal | - | - | - | mc_turretReticule{this 1s easeout {_rotation: 0}; petal1_mc.base_mc 0.2s easeout {_y: -20}; petal2_mc.base_mc 0.2s easeout {_y: -60}} |
| Hud_GFX | FinishMarkerUpdate | script: TnHUD.DrawHUD | C | - | - | - |
| Hud_GFX | GameAnnouncement | script: TnHUD.LocalizedGameAnnouncement (TnGameTypeMessage, survival) | C | - | announcement_mc 0.4s easeout {_yscale: 100, _xscale: 100, _alpha: 100} | - |
| Hud_GFX | GameMessage | script: TnHUD.ColoredLocalizedMessage(CS) → HTML <font color> parts (kill feed: TnGameMessage / KillStreak(Ended)Message) | C | - | messageQueue[i] 0.2s easeout {_y: (i * -22)} | mc_gameMessage_default{this 1s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | GameMessageSpectatorMode | AS-internal | C | - | thisElement 0.5s easeout {_y: (initialPosY - 160)}; thisElement 0.5s easeout {_y: initialPosY} | - |
| Hud_GFX | NotifyAbilityType0Changed | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyAbilityType0UpdateCooldown | AS-internal | C | - | - | - |
| Hud_GFX | NotifyAbilityType1Changed | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyAbilityType1UpdateCooldown | AS-internal | C | - | - | - |
| Hud_GFX | NotifyAbilityType2Changed | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyAbilityType2UpdateCooldown | AS-internal | C | - | - | - |
| Hud_GFX | NotifyActiveGrenadeCount | data observer (per frame on change) | C | - | - | mc_activeGrenadePromptText{this 0.2s easeout {_alpha: 100, _x: -10}; thisElement 0.5s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | NotifyAmmoAdded | script: TnHUD.NotifyAmmoAdded(amount, weaponName) | C | - | - | mc_ammoPickupNotification{_parent.AmmoPickupArray[i] 0.2s easeout {_y: (i * _parent.NotifySpacing)}; thisElement 0.8s easeout {callback: removeMe, _alpha: 0, _y: -50}; this 0.4s easeout {_alpha: 100, _x: 0}} |
| Hud_GFX | NotifyBadConnection | script: TnHUD.DisplayBadConnectionAlert() [online] | - | - | - | mc_networkConnectivityAlert |
| Hud_GFX | NotifyCantTransform | script: TnHUD.NotifyCantTransform() | C | - | - | mc_cantTransform{this 1s easeout {callback: removeMe, _alpha: 0}; this 0.2s easeout {color: 16724787, _alpha: 100, _yscale: 100}} |
| Hud_GFX | NotifyContextualCommand | data observer (per frame on change) | C | - | NotifyArray[i] 0.2s easeout {_y: ((i * NotifySpacing) + NotifySpacing)} | mc_notify{this 0.2s easeout {_alpha: 100}; this 0.5s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | NotifyCurrentFormChanged | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyCurrentWeaponChanged | script: script literal | C | - | - | mc_currentWeaponIcon{this 0.5s easeout {_alpha: 100, _x: 0}; thisElement 0.3s easeout {callback: removeMe, _alpha: 0, _x: 10}}, mc_currentWeaponText{this 0.5s easeout {_alpha: 100, _x: 0}; thisElement 0.5s easeout {callback: removeMe, _alpha: 0, _y: 10}} |
| Hud_GFX | NotifyDamageIncrease | data observer (per frame on change) | C | - | - | mc_crosshairIncreaseDamage |
| Hud_GFX | NotifyDamageIndicatorAdded | AS-internal | C | - | - | mc_damageArrow{this 1s easein {callback: this.deleteMe, _alpha: 0}} |
| Hud_GFX | NotifyDownedWarningFade | AS-internal | - | - | - | mc_hurt{tunnelVision_mc 3s easeout {_alpha: 0}; sideEffect_mc 3s easeout {_alpha: 0}; tunnelVision_mc 3s easeout {_alpha: 30}} |
| Hud_GFX | NotifyFineAimChanged | script: script literal | C | - | - | - |
| Hud_GFX | NotifyGrenadeAmmoChanged | data observer (per frame on change) | C | - | - | mc_grenadeIcon{thisElement 0.4s easeout {callback: removeMe, _yscale: 150, _xscale: 150, _alpha: 0}; this 0.4s easeout {_rotation: 0, _yscale: 100, _xscale: 100, _alpha: 100}} |
| Hud_GFX | NotifyHudScrambledChanged | data observer (per frame on change) | C | - | midCenter_mc 0.5s easeout {_alpha: 0}; midCenter_mc 0.5s easeout {_alpha: 100} | - |
| Hud_GFX | NotifyKillstreakChanged | data observer (per frame on change) | C | - | thisElement 0.2s easeout {_alpha: 100}; thisElement 0.2s easeout {_alpha: 0} | mc_killstreakIcon{iconContainer 0.5s easeout {_x: 20}; thisElement 0.5s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | NotifyLevelUp | script: TnHUD.NotifyLevelUp(level, specialty) | C | MP_LEVEL_UP_MX_STNG | gotoAndPlay(fadeOut) | mc_levelUpNotify |
| Hud_GFX | NotifyLockOnStateChanged | data observer (per frame on change) | C | - | gotoAndPlay(r1) | mc_missileWarningVictimBrackets |
| Hud_GFX | NotifyNormalizedDownedHealthChanged | data observer (per frame on change) | C | - | - | mc_incapacitatedScreen{thisElement 0.1s easeout {_alpha: prcnt}; thisElement 0.1s easeout {_alpha: 100}; incapWidgets_mc 0.5s easeout {_alpha: 100}; gotoAndPlay(1)} |
| Hud_GFX | NotifyOnAttackingTeamChanged | AS-internal | C | - | RewardAnnouncement: reward_mc 0.4s easeout {_y: 18, _alpha: 100} | - |
| Hud_GFX | NotifyOverShieldChanged | data observer (per frame on change) | C | - | healthNumber_txt 0.5s easeout {color: 16777215}; healthNumber_txt 0.5s easeout {color: 9105663}; gotoAndPlay(loop), gotoAndStop(1) | - |
| Hud_GFX | NotifyPlayerRotationChanged | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyProgressBarChanged | AS-internal | C | - | thisElement 0.5s easeout {_y: -140, _alpha: 100}; thisElement 0.5s easeout {_y: -120, _alpha: 0} | - |
| Hud_GFX | NotifyScoringMultiplierChanged | AS-internal | C | PICKUP_DMG_MULTIPLIER, PICKUP_DMG_MULTIPLIER_DECREASE | thisElement 0.4s easeout {color: _global.GetPointMultiplierColor(), _yscale: 100, _xsc}; thisElement 0.8s easeout {color: _global.GetPointMultiplierColor(), _yscale: 100, _xsc} | - |
| Hud_GFX | NotifySegmentedHealthChanged | data observer (per frame on change) | C | - | gotoAndPlay(loop), gotoAndStop(1) | mc_vitalBarActiveBarDrained{thisElement 0.2s easeout {_width: drainedBarEndWidth, _x: drainedBarEndX}; thisElement 0.5s easeout {callback: thisElement.removeMe, _width: 0}}, mc_vitalBarHealthSegment{thisElement 0.5s easeout {color: SegmentColor, _yscale: 100}; highlight_mc 0.2s easeout {_alpha: (100 / segmentIndex)}; bar_mc 0.2s easeout {_alpha: 90}} |
| Hud_GFX | NotifySpectating | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyTargetHealthChanged | AS-internal | C | - | thisClip 0.1s linear {color: healthColor} | - |
| Hud_GFX | NotifyTargetNameChanged | AS-internal | C | - | - | - |
| Hud_GFX | NotifyTargetTypeChanged | data observer (per frame on change) | C | - | crosshairAnchor_mc 0.2s easeout {color: r1} | - |
| Hud_GFX | NotifyTutorialMessage | AS-internal | C | - | - | mc_tutorialMessage{_parent.MinimizedTutorialQueue[i] 0.5s easeout {_y: (midRightCoord.y + (i * 25))}; thisElement 0.5s easeout {callback: removeMe, _y: (thisElement._y - 40), _alpha: 0}; thisElement 0.5s easeout {_y: (midRightCoord.y + (_parent.MinimizedTutorialQueue.lengt}; gotoAndPlay(minimize)} |
| Hud_GFX | NotifyWeaponClipAmmoChanged | data observer (per frame on change) | C | - | AmmoBarClip 0.2s easeout {_yscale: 100, color: BarColor}; updateStatus: AmmoBarClip 0.5s easeout {color: BarColor}; updateStatus: ammoNumber_txt 0.5s easeout {color: 16724787}; updateStatus: ammoNumber_txt 0.5s easeout {color: 16777215} …; gotoAndPlay(loop), gotoAndStop(1) | mc_ammoBar{bar_mc 0.2s easeout {_width: r2}; highlight_mc 0.2s easeout {_alpha: r1, _width: r3}}, mc_vitalBarAmmoSegment |
| Hud_GFX | NotifyWeaponJammedChanged | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | NotifyWeaponMessageChanged | data observer (per frame on change) | C | - | gotoAndPlay(charging), gotoAndPlay(ready) | mc_chargingIndicator |
| Hud_GFX | NotifyWeaponReserveAmmoChanged | data observer (per frame on change) | C | - | thisElement 0.5s easeout {_alpha: 0}; thisElement 0.5s easeout {_alpha: 100}; updateStatus: AmmoBarClip 0.5s easeout {color: BarColor}; updateStatus: ammoNumber_txt 0.5s easeout {color: 16724787} …; gotoAndPlay(loop), gotoAndStop(1) | - |
| Hud_GFX | NotifyWeaponSpreadChanged | data observer (per frame on change) | C | - | - | - |
| Hud_GFX | PointEvent | script: TnHUD.NotifyScoreEvent(-1, ScoreChange) / XpTransactions observer | C | MP_REWARD_DIALOG_BOX | - | mc_pointTransaction{snd MP_REWARD_DIALOG_BOX; points_mc 0.5s easeout {_yscale: 100, _xscale: 100}; points_mc 1s easeout {_y: (this._y - 100)}; this 1s easeout {callback: removeMe, _alpha: 0}} |
| Hud_GFX | ReapplyBuff | script: TnHUD.ReapplyBuff(BuffId, BuffTime) | - | - | - | - |
| Hud_GFX | RewardAnnouncement | script: TnHUD.LocalizedRewardAnnouncement | C | - | reward_mc 0.4s easeout {_y: 18, _alpha: 100} | - |
| Hud_GFX | SetWeaponCrosshair | AS-internal | - | - | CrosshairClip 0.3s easeout {_rotation: 0} | - |
| Hud_GFX | ShowAbilityType0 | AS-internal | - | - | thisClip 0.5s easeout {_alpha: 100}; thisClip 0.5s easeout {_alpha: 0} | - |
| Hud_GFX | ShowAbilityType1 | AS-internal | - | - | thisClip 0.5s easeout {_alpha: 100}; thisClip 0.5s easeout {_alpha: 0} | - |
| Hud_GFX | ShowAbilityType2 | AS-internal | - | - | thisClip 0.5s easeout {_alpha: 100}; thisClip 0.5s easeout {_alpha: 0} | - |
| Hud_GFX | ShowCrosshair | AS-internal | C | - | - | - |
| Hud_GFX | ShowGrenades | AS-internal | - | - | thisElement 0.2s easeout {_alpha: 100}; thisElement 0.2s easeout {_alpha: 0} | - |
| Hud_GFX | StartMarkerUpdate | script: TnHUD.DrawHUD | C | - | - | - |
| Hud_GFX | ToggleSpectatorHudMode | AS-internal | - | - | - | - |
| Hud_GFX | TransformDisrupted | AS-internal | C | - | DisruptedClip 0.2s easeout {_alpha: 100} | mc_transformDisrupt |
| Hud_GFX | UnapplyBuff | script: TnHUD.UnapplyBuff(BuffId) | C | - | - | - |
| Hud_GFX | UnapplyKillstreakBuff | AS-internal | - | - | KillstreakBuffs[i] 0.3s easeout {_y: (i * IconSpacing)} | - |
| Hud_GFX | UpdateAbilityCooldown | script: TnHUD.SetAbilityResource(n, time, normStart) / Ready(n,-1,0) | - | - | - | - |
| Hud_GFX | UpdateMarker | script: TnHUD.UpdateObjectiveMarker(id, dist, x, y, z, type, desc) | C | - | addMarker: r1 1s easeout {_alpha: 100}; trackOffScreen: r1 1s easeout {_alpha: 100} | - |
| Hud_GFX | displayReloadPrompt | AS-internal | - | - | - | - |
| Hud_GFX | hideScope | AS-internal | - | - | thisElement.zoomScope_mc 0.5s easeout {callback: thisElement.removeScope, _alpha: 0}; _root.hud_mc.topLeft_mc 0.5s linear {_alpha: 100}; _root.hud_mc.topCenter_mc 0.5s linear {_alpha: 100}; _root.hud_mc.topRight_mc 0.5s linear {_alpha: 100} … | - |
| Hud_GFX | showScope | AS-internal | - | - | thisElement.zoomScope_mc 0.2s linear {_alpha: 100}; _root.hud_mc.topLeft_mc 0.2s linear {_alpha: 0}; _root.hud_mc.topCenter_mc 0.2s linear {_alpha: 0}; _root.hud_mc.topRight_mc 0.2s linear {_alpha: 0} … | mc_zoomScope |
| Hud_GFX | updateAmmoWarning | AS-internal | - | - | - | - |
| PlayerList_GFX | GivePlayerListInput | engine (movie entry) | - | - | - | - |
| PlayerList_GFX | UpdatePlayerListData | script: TnHUD scoreboard movie (ScoreboardMovie.Invoke) | C | - | BuildPlayerList: r2 0.4s easeout {_y: ListElementPosition} | - |
| MultiplayerRespawn_GFX | SetSpectateTarget | engine (movie entry) | - | - | currentlySpectating_mc 0.5s easeout {_alpha: 100}; currentlySpectating_mc 0.5s easeout {_alpha: 0} | - |
| ChallengeNotify_GFX | ChallengeUnlocked | engine (movie entry) | C | - | - | mc_notifyChallenge{thisElement 0.5s easeout {callback: closeMovie, _y: 20, _alpha: 0}; content_mc 0.2s easeout {_alpha: 100}; ChallengeIconClip 0.5s easeout {_yscale: 60, _xscale: 60, _alpha: 100}} |
| PauseMenu_GFX | DisplayCharacterSelect | engine (movie entry) | - | - | - | - |

**Data-store-bound displays (no entry point; the movie reads and binds `<DataStore:Field>` itself):**
- **Respawn timer** (MultiplayerRespawn_GFX): `<PlayerOwner:TimeToRespawn>`, `<CurrentGame:GameTeamStatus>`;
  `SetSpectateTarget` fades the "currently spectating" label (0.5 s).
- **Pregame / round countdown** (PreGameCountdown_GFX): `CurrentCountdown`, `IsCountingDown`, `CurrentRound` / `Rounds`,
  `GameModeFriendlyName`, `GameModeTag`.
- **Mode score counters** (Hud_GFX): `<CurrentGame:GoalScore>`, `Teams`, `Players`, `<PlayerOwner:Score>`,
  `CompetitiveScoreEnabled`, `CurrentObjectiveCountdown`, `_CurrentWave` / `_NextWaveTime` (survival).
- **Scoreboard** (PlayerList_GFX): `UpdatePlayerListData` from TnHUD (`ScoreboardMovie.Invoke`), plus `<CurrentGame:Players / Teams /
  AttackingTeamIndex / GameModeTag / _bIconicMode>`; rows slide in (0.4 s easeout).
- **In-game stats** (InGameStats_GFX): mode name / rules, `SelectedMapID`.
- Ours: every one of these data-store field names appears in src [C].

**Decoded behaviours (CONFIRMED AS):**
- **Kill feed** (`GameMessage`):
  - text is TnGameMessage's colour parts as HTML `<font color='#RRGGBB'>…</font>`;
  - weapon icons come from `[Tag]` substrings swapped for bitmaps via `setImageSubstitutions` at 0.85 scale, baseline 23. The 29
    tags are listed in the movie, e.g. `[AssRifle]` → `death_AssaultRifle`, `[Melee]`, `[Car] / [Truck] / [Jet] / [Tank]`,
    `[Suicide]`, `[Sentry]`, `[HvyTurret]`;
  - newest line on top; existing lines slide to `y = i × −22` (0.2 s easeout); **lines beyond 5 fade out**; each line fades
    (1 s easeout) and is removed;
  - in spectator mode the whole feed shifts up 160 px (0.5 s).
- **Hit confirm** (`CausedDamage`): plays **`HUD_POSITIVE_HIT_INDICATOR`** and spawns `mc_causeDamage`.
- **Damage direction:**
  - `NotifyDamageIndicatorAdded` spawns `mc_damageArrow` (fades over 1 s easein, then deletes);
  - `NotifyPlayerRotationChanged` (observer) rotates the arrows with the view.
- **Point / XP events** (`PointEvent`): spawns `mc_pointTransaction`, sound **`MP_REWARD_DIALOG_BOX`**; points pop (0.5 s), rise
  100 px (1 s), fade (1 s).
- **Medal / reward banner** (`RewardAnnouncement`): `reward_mc` slides to y 18, alpha 100 (0.4 s).
- **Game announcement** (`GameAnnouncement`): `announcement_mc` scales to 100 % and fades in (0.4 s).
- **Killstreak** (`NotifyKillstreakChanged`): `mc_killstreakIcon` (icon slides 20 px, 0.5 s; fades out 0.5 s). Active killstreak
  buffs are stacked icons (`ApplyKillstreakBuff`, AS-internal from ApplyBuff).
- **Level up** (`NotifyLevelUp(level, specialty)`): `mc_levelUpNotify` + **`MP_LEVEL_UP_MX_STNG`**.
- **Ammo pickup** (`NotifyAmmoAdded(amount, weapon)`): `mc_ammoPickupNotification` stacks (0.2 s), slides in (0.4 s), fades up 50
  px (0.8 s).
- **Scoring multiplier** (`NotifyScoringMultiplierChanged`): `PICKUP_DMG_MULTIPLIER` / `_DECREASE`, a colour pulse.
- **Health** (`NotifySegmentedHealthChanged`): per-segment colour (0.5 s); a drained-bar ghost shrinks (0.2 s, then 0.5 s).
  Overshield recolours the health number (0.5 s).
- **Can't transform** (`NotifyCantTransform`): `mc_cantTransform` flashes red (0x FF3333, 0.2 s) then fades (1 s).
- **Downed warning fade** (`NotifyDownedWarningFade`, AS-internal): tunnel-vision overlay 3 s.
- **Pause:** PauseMenu_GFX is a menu movie (part 2). `DisplayCharacterSelect` is its in-match class change.

**Findings (part 1):**
1. **Engine-called entries not driven by ours:**
   - `ReapplyBuff`: a buff refresh, so a jam timer restart won't redraw;
   - `UpdateAbilityCooldown`: ours calls the per-slot `NotifyAbilityTypeNUpdateCooldown` directly, which is equivalent;
   - `GivePlayerListInput` (scoreboard pad input);
   - `SetSpectateTarget` (respawn spectate label);
   - `DisplayCharacterSelect` (pause class change);
   - `NotifyBadConnection` (online, out of scope).
2. **Update to audit 2 part (1):** ours now **does** send `ApplyBuff` / `UnapplyBuff` (Hud.cpp ~202). The jam-display gap is
   closed; only `ReapplyBuff` remains.
3. **HUD sounds in the movie** (`HUD_POSITIVE_HIT_INDICATOR`, `MP_REWARD_DIALOG_BOX`, `MP_LEVEL_UP_MX_STNG`,
   `PICKUP_DMG_MULTIPLIER(_DECREASE)`, `HUD_OBJECTIVE_ADDED`) play through `Sound.PlaySound`. Ours implements that call [C], so
   they sound if our movie host runs the AS. H: depends on Frontend's Scaleform host.
4. Observers with no AS definition in Hud_GFX (`NotifyFirstSpawn`, heat and normalized heat, `NotifyPrimary / Secondary /
   MeleeWeaponChanged`, `NotifyWeaponReticuleTypeChanged`, `SetAbility`, `WaveformData`, `CompleteObjective`) are **no-ops in
   the original**. Don't implement visuals for them.

## Part (3): end-of-match flow (versus)

**Sources:**
- script: TnGame / TnMultiplayerGame (TransContent), TnVersusGameOverMessage, TnUIController;
- AS: EndGameStats_GFX (dumped with `tools/as2dis.py`; the movie has **no `_global` entry points**, it drives itself from its
  timeline and ExternalInterface reads).

**C = invoked / present in our src, not visually faithful.**

| # | Step (original) | Timing / payload | Sound | Ours |
|---|---|---|---|---|
| 1 | Score reaches `GoalScore` (`CheckScore` → `ReachedGoalScore`: GoalScore > 0 and score ≥ GoalScore), or the time limit / rules end → **`EndGame(Winner, Reason)`** | TnGame.EndGame: voice mute reset; `HandleGameCompletionStats` (per PC: `PRI.AddGameCompletion(elapsed − start, type)`, class-form tracking off, **`PRI.SendGameStatsToClient`**); winner = the given one or `GetWinningPRI()`. TnMultiplayerGame: `RecordSessionComplete`, `GameRulesModifiers.HandleEndGame`, → state **MatchOver** | — | `Match.h` (C) |
| 2 | **Game-over message** (`TnVersusGameOverMessage.ClientReceive`, per client) | Switch 2 → "Enemy team forfeits"; 1 → "Tie game"; else, with a winner, "Your team won" / "Your team lost" by the client's team → `GRI.SetGameOverMessage` (`<CurrentGame:GameOverMessage>`) | Announcer **`MP_GameAutobotWin`** (winner team 0) or **`MP_GameDecepticonWin`**, the same for every client; plays whenever there is a winner, forfeit included | C (MatchAudio.h; ours marks it PARTIAL "not decompiled": **it is now**, rules here) |
| 3 | **MatchOver.BeginState** | `AwardMVPAchievement` (if > 1 PRI and XP can be gained: every PRI with the top score gets achievement 37); GRI status 5; **UI event 9 to all controllers**; voice mute reset; stats write; **timer `MatchOverCountdown` = 15 s → `OnCountdownComplete`** | — | `matchOverCountdown 15` (C) |
| 4 | **UI event 9** → TnUIController InGame / Spectating / Paused `.OnEndGame` → state **GameEnded** | `SetHudVisible(false)`; `OpenUI(EndGameUI)` = `EndGameStats_GFX` | — | `UIController::endGameUI` (C) |
| 5 | **EndGameStats movie opens** | Fades in (1 s easeout); game-mode icon `mc_gameModeIcon<TAG>` (CCP / CP / CTF / DM / DOM / EXT / KOTH / RO / SV / TDM) spins in from −60°, ×2.8 scale, 0.5 s. **Non-iconic mode starts on the "earnings" (XP) view; iconic mode starts on the scoreboard** (xpOnly / IconicMode). Title slides in (0.5 s, x 380 → 412) | — | movie hosted (C) |
| 6 | **XP tally** (per specialty card Scout / Soldier / Scientist / Leader, sprite 58) | Reads `GetXpEarnedForSpecialtyLastMatch`, `GetXpEarnedForSpecialty` (final), `GetLevelForSpecialty`, `GetXpNeededForLevel`. Rewinds the start level until start XP ≥ the level cap. **After 800 ms** the glow fades in and **CurrentXp counts linearly to FinalXp over 2.5 s**. Each level crossed: level + 1, caps re-read, the bar / title / strip flash to 0xFF9333 (0.6 s), the level number pops from 500 % → 100 % (0.6 s), and `levelUpStrip.levelUp()` slides the "LEVEL UP" strip in (0.5 s, y 115). The bar width is `(xp − lastCap) / (nextCap − lastCap)`, with "+N" earned. At 2.5 s → `finishedUpdatingXp` snaps to final; then **every 800 ms / 1.5 s it toggles "EARNED +N" ↔ "N TO NEXT LEVEL"** (0.4 s scale flips). Max level shows `LevelMax` | none in the movie (only BUTTON_BACK on close) | XP reads implemented in FrontendRuntime (C) |
| 7 | **View toggle** (X button) | earnings ↔ scoreboard: 0.5 s fades and header / footer moves. Scoreboard title = GameOverMessage (non-campaign), else "FINAL SCORES". The scoreboard is the PlayerList movie loaded inside (`ViewMode 'InGame'`, cycling specialty levels) | — | AS-internal |
| 8 | **Unlock reveal** | **None in the original:** `unlockedItem(Item)` exists in the movie and is **empty**. Unlocks show via the in-match challenge popup (`ChallengeUnlocked`, ChallengeNotify_GFX) and level-up (`NotifyLevelUp` + `MP_LEVEL_UP_MX_STNG`) | — | ChallengeUnlocked (C) |
| 9 | **B / back** | `closeEndGameStats`: `BUTTON_BACK`, restores the parent menu | BUTTON_BACK | AS |
| 10 | **15 s after MatchOver** → `ReturnToGameLobby` | travel back to the game lobby. Lobby, next-map vote and countdown are **online-lobby flow: out of scope**. **There is no "next map" countdown in the end-of-match movie itself**; the 15 s timer is server-side and not displayed | — | `ReturnToGameLobby` (C) |

**Findings (part 3):**
1. **The end screen has no countdown display.** The 15 s `MatchOverCountdown` is invisible. If ours shows a countdown, that's an
   addition.
2. **The XP tally rule** above (800 ms delay, 2.5 s linear count, per-level flash / pop, the 1.5 s / 800 ms alternating label) is
   all movie-side. It's faithful as long as our host runs the AS and answers the four `Customize.*` XP calls (ours implements
   them).
3. `TnVersusGameOverMessage` is now decoded (step 2). Ours can drop its PARTIAL tag: forfeit / tie precedence and the winner
   announcer for everyone.
4. The MVP achievement goes to every player tied for the top score, only with more than one PRI and XP enabled.

## Part (2): front-end UI sounds and menu transitions

Generator: `tools/gfx_sound_map.py`, writing `notes/data/frontend_ui_sounds.json` / `.md`. It covers every `Sound.PlaySound` cue in
16 offline front-end and shared movies, with the function it sits in, plus the open / close / select / focus interps.

**Ours** = the cue name in Rebuild src (C) / data (D).

**The original's whole front-end UI sound vocabulary is five movie-side cues** (CONFIRMED, exhaustive over those movies):

| UI cue | Ours | Movies | Where (function@sprite, first 4) |
|---|---|---|---|
| BUTTON_ACCEPT | C | Brightness, CampaignGameOver, CampaignSelect, ChallengeMenu, CharacterSelect, CustomTransformers, DifficultySelect, ExtrasMenu, FrontEnd, MessagePrompt, PauseMenu, PlayerList, SettingsMenu, SharedComponents, TextPrompt | <anon>@63; inputEvent@130; inputEvent@16; inputEvent@17 |
| BUTTON_BACK | C | Brightness, CampaignGameOver, CampaignSelect, ChallengeMenu, CharacterSelect, CustomTransformers, DifficultySelect, EndGameStats, ExtrasMenu, FrontEnd, MessagePrompt, PauseMenu, PlayerList, SettingsMenu, SharedComponents, TextPrompt | closeEndGameStats@None; inputEvent@130; inputEvent@17; inputEvent@173 |
| BUTTON_DOWN | C | Brightness, CampaignGameOver, CampaignSelect, ChallengeMenu, CharacterSelect, CustomTransformers, DifficultySelect, ExtrasMenu, FrontEnd, MessagePrompt, PauseMenu, PlayerList, SettingsMenu, SharedComponents, TextPrompt | <anon>@59; findDirectionalFocus@130; findDirectionalFocus@17; findDirectionalFocus@173 |
| BUTTON_START | C | FrontEnd | inputEvent@23 |
| BUTTON_UP | C | Brightness, CampaignGameOver, CampaignSelect, ChallengeMenu, CharacterSelect, CustomTransformers, DifficultySelect, ExtrasMenu, FrontEnd, MessagePrompt, PauseMenu, PlayerList, SettingsMenu, SharedComponents, TextPrompt | <anon>@59; customInputEvent@99; findDirectionalFocus@130; findDirectionalFocus@17 |

**Rules:**
- **Focus move** = `BUTTON_UP` / `BUTTON_DOWN`, from each movie's `findDirectionalFocus` (and selector `customInputEvent`):
  - by direction moved, not per element;
  - left / right selectors reuse up / down (SharedComponents);
  - there is **no separate tab sound**: Customize's tabs use the same focus path.
- **Accept** = `BUTTON_ACCEPT` (inputEvent buttonA); **back** = `BUTTON_BACK` (buttonB, and `closeEndGameStats`); **title "press
  start"** = `BUTTON_START` (FrontEnd only).
- **Cue resolution:** `PlaySound('X')` → `BL_HUD_INTERFACE.X` SoundCues (H: the interface prefixes the HUD package):
  - BUTTON_ACCEPT −18 dB, two layers: front `BUTTON_ACCEPT_03` + `_LSRS` rear at −6 dB;
  - BUTTON_BACK −18 dB, `BUTTON_BACK_02` + `_LSRS`;
  - BUTTON_UP / BUTTON_DOWN −15 dB, single `*_FLANGE`;
  - BUTTON_START −15 dB, `BUTTON_ACCEPT_02` + `_LSRS`;
  - all 2D, unpausable, priority 255.
  - Ours: `SoundCues.inc` carries all five with both layers (the LSRS rear at −6 dB, 0.707) [C], which matches.
- **No other front-end UI sound exists in the movies.** Engine-side menu music and stingers (e.g. `MP_LEVEL_UP_MX_STNG`,
  announcer) are game events, covered in parts (1) and (3).

**Menu transitions** (the movies' own interps; all easeout unless noted). Ours runs these when its Scaleform host executes the AS.
Driven-by-ours is not separable by grep (AS-internal):

| Movie | Transition interps (open / close / show / hide / focus …) |
|---|---|
| FrontEnd_GFX | - |
| CampaignSelect_GFX | UpdateSelectorState: background_mc 0.5s easeout {callback: thisClip.assignSelectorFocus, _height: r}; UpdateSelectorState: frameBottom_mc 0.5s easeout {_y: (r1 / 2)}; UpdateSelectorState: frameTop_mc 0.5s easeout {_y: (r1 / -2)}; factionSelected: autobotBG_mc 0.5s easeout {_alpha: 0}; factionSelected: autobot_mc 0.5s easeout {_alpha: 0}; factionSelected: currentFocus 0.5s easeout {_yscale: 360, _xscale: 360, _alpha: 10, _x: -240}; factionSelected: currentFocus.glow_mc 0.5s easeout {_alpha: 0}; factionSelected: currentFocus.name_txt 0.5s easeout {_alpha: 0} |
| CharacterSelect_GFX | - |
| CustomTransformers_GFX | closeMenu: _parent.footer_mc.customizeButtonHints_mc 0.2s easeout {_alpha: 100}; closeMenu: _parent.header_mc 0.2s easeout {_alpha: 100}; occluderClose: topMenuElements[r3] 0.2s easeout {_alpha: 100}; onClick: topMenuElements[r3] 0.2s easeout {_alpha: 0}; setTabColor: tab_mc 0.2s easeout {color: parseInt(tabColor)}; shiftCenter: charLevel_txt 0.2s easeout {color: 9105663}; shiftCenter: thisClip 0.4s easeout {callback: thisClip.expandMenu, _y: _parent.overvie}; showCountdown: thisClip 0.5s easeout {_alpha: 0, _y: (InitPositionY + 10)} |
| DifficultySelect_GFX | - |
| ExtrasMenu_GFX | selectionUpdated: ConceptButtonArray[r2] 2s easeout {_alpha: 50}; selectionUpdated: ConceptImageClip 0.4s easeout {_alpha: 100, _x: 0}; selectionUpdated: centerFooter_mc 2s easeout {_alpha: 100}; selectionUpdated: leftArrow_mc 0.5s easeout {_alpha: 0}; selectionUpdated: r3 0.4s easeout {callback: r3.removeMe, _alpha: 0, _x: -50}; selectionUpdated: r3 0.4s easeout {callback: r3.removeMe, _alpha: 0, _x: 50}; selectionUpdated: rightArrow_mc 0.5s easeout {_alpha: 0} |
| SettingsMenu_GFX | closeMenu: _backMenuRef 0.2s easeout {_alpha: 100}; selectCloseMenu: _backMenuRef 0.2s easeout {_alpha: 100}; selectionUpdated: this 0.2s easeout {_y: (0 - this.currentFocus._y)} |
| PauseMenu_GFX | closeMenu: _root.bg2_mc 1s easeout {_alpha: 0}; selectionUpdated: this 0.2s easeout {_y: (0 - this.currentFocus._y)} |
| ChallengeMenu_GFX | - |
| Brightness_GFX | - |
| MessagePrompt_GFX | - |
| TextPrompt_GFX | - |
| SharedComponents_GFX | currentSelectionChanged: arrowLeft_mc 0.2s easeout {color: 16777215, _alpha: 100}; currentSelectionChanged: arrowLeft_mc 0.2s easeout {color: 5289429, _alpha: 50}; currentSelectionChanged: arrowLeft_mc 0.5s easeout {color: 5289429, _alpha: 25}; currentSelectionChanged: arrowRight_mc 0.2s easeout {color: 16777215, _alpha: 100}; currentSelectionChanged: arrowRight_mc 0.2s easeout {color: 5289429, _alpha: 50}; currentSelectionChanged: arrowRight_mc 0.5s easeout {color: 5289429, _alpha: 25}; currentSelectionChanged: seperator_mc.arrowLeft_mc 0.2s easeout {color: 16777215, _alpha: 100}; currentSelectionChanged: seperator_mc.arrowLeft_mc 0.3s easeout {color: 16777215, _alpha: 25} |
| EndGameStats_GFX | showXpTotal: xpBar_mc.xpEarned_mc 0.4s easeout {_alpha: 0, _yscale: 0}; showXpTotal: xpBar_mc.xpEarned_mc 0.4s easeout {_alpha: 100, _yscale: 100}; showXpTotal: xpBar_mc.xpToNextLevel_mc 0.4s easeout {_alpha: 0, _yscale: 0}; showXpTotal: xpBar_mc.xpToNextLevel_mc 0.4s easeout {_alpha: 100, _yscale: 100} |
| PlayerList_GFX | - |
| CampaignGameOver_GFX | - |

- **Common pattern:**
  - list scroll on selection: `this._y → −currentFocus._y` over 0.2 s (Settings, Pause);
  - closing a sub-menu fades the parent back in (0.2 s);
  - Pause close fades `bg2_mc` out over 1 s;
  - shared selector arrows tint white / α100 on change (0.2 s) and settle to 0x50B5D5 α50 → α25 (0.5 s);
  - CampaignSelect faction pick: the selector frame resizes (0.5 s); the unpicked faction fades 0.5 s and the picked one scales
    up.
- FrontEnd_GFX, CharacterSelect, DifficultySelect, Challenges, Brightness and the prompts animate through timeline frames
  (gotoAndPlay labels) rather than named interps. Not enumerated here.

**Findings (part 2):**
1. The UI sound set is complete and matches in ours: 5 cues, both layers, levels.
2. There's no hidden per-menu or tab sound to add.
3. Transitions are movie-side. Fidelity depends on the Scaleform host running the AS interp controller
   (`_global.addInterp` / `updateInterpObjects`: per-frame easeout or linear tweening with overshoot). That's the one shared
   piece every transition above relies on.

**Audit 3 complete.**
