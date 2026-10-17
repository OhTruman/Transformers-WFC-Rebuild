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
