# Campaign HUD additions: plan (Frontend, 2026-10-17; design, code only behind a campaign flag)

Input: CAMPAIGN_HUD_OBJECTIVES_RE.md (RE lane, b8f071e). The campaign uses the same TnHUD and movie
(UI_GFxHud_p.Hud_GFX_1) as MP; its additions are extra callbacks and data observers on that movie. This plan says, per
element:
- the GFx symbol and the AS entry point (from the shipped Hud_GFX; RE-Workspace work/q/gfx/Hud_GFX.as.txt);
- what our frontend already drives (src/frontend/Hud.h / Hud.cpp HudController, src/core/Application_Frontend.cpp);
- the C++ hook the campaign runtime calls;
- the headless test;
- the order of work.

Package 7 is untouched:
- every new path runs only when the match is a campaign match (a `campaign` flag on the frontend's match state, set
  from the campaign game modes SOLO / CP / CCP; until the campaign boots, only the tests set it);
- versus keeps sending exactly what it sends today.

## 0. Summary

| # | element | GFx symbol / AS entry | driven today | hook needed | test | order |
|---|---|---|---|---|---|---|
| E1 | New objective | sprite 607, `_global.AddObjective(Objective)` -> `mc_objectiveNew`, sound HUD_OBJECTIVE_ADDED (AS calls Sound.PlaySound) | `HudController::addObjective(text)` queues the call; **nothing calls it**; `<CurrentGame:ActiveObjectives>` answers "" | objective bank + current index (CampaignObjectives), localisation, pause-menu stores | hud.campaign_objective_* | 1 |
| E2 | Interact / contextual prompts | `_global.NotifyContextualCommand(0, Action, Text)` -> `mc_notify` stack, glyph tokens via setImageSubstitutions | **yes**: HudFrame.contextualPrompts diffed into add / remove / clear (hud.prompts_and_cant_transform) | campaign interactables (turrets, terminals, buddies) fill the same list | extend hud.prompts_* with campaign texts and the cinematic clear | 5 |
| E3 | Revive progress bar | sprite 560 / 652 group, `_global.NotifyProgressBarChanged(Description, NormProgress)` | **yes**: progressObserver / progress with the TransGame.int label, "Reviving `p" (hud.progress_revive_name) | Gameplay's buddy revive state into the existing observer | add the AI-buddy case ("Reviving", no name) | 6 |
| E4 | Own downed screen | `_global.NotifyNormalizedDownedHealthChanged(h)`: h < 1 attaches `mc_incapacitatedScreen`, `ToggleSpectatorHudMode(true)`, reads `<Strings:TransGame.ContextualCommands.DownedSuicide>`; h back to 1 calls `.Revived()` | send path **yes** (HudFrame.downedHealth); World never sets it (versus has no downed state); **`<Strings:...>` store is not answered** | Gameplay's downed health; DataStores `<Strings:File.Section.Key>` | hud.campaign_downed_* + datastores.strings_store | 4 |
| E5 | Tutorial messages | `_global.NotifyTutorialMessage(TutorialName)` (Hud_GFX 03fa), `mc_tutorialMessage` 3000 ms then the mid-right stack (25 px, 200 ms gap) | **no** | HudController::tutorialMessage + once-per-profile record + localisation | hud.campaign_tutorial_* | 2 |
| E6 | Subtitles | none in Hud_GFX (engine canvas, PARTIAL in RE) | **no** | a subtitle overlay (PROPOSAL, section E6) fed by Systems' dialogue playback | hud.subtitles_* | 7 |
| E7 | Cinematic mode HUD hide | UI events 12 / 13 (ShowHudFalse / ShowHudTrue); Hud_GFX fades topCenter_mc / bottomCenter_mc itself on spectator mode | **yes** for events 12 / 13 (UIController); nothing raises them from Kismet | SeqAct_ToggleCinematicMode -> flow.onUIEvent(12 / 13) + Gameplay's input gate | ui.cinematic_hide_show | 3 |

Not built, because the original has no HUD element for them (RE section 3):
- checkpoint reached: no HUD text; the profile write only;
- mission complete: the end screen and outro FMV path;
- objective complete: `_global.CompleteObjective` does not exist in Hud_GFX, so no visual.

## E1. New objective

- **Original:**
  - TnWorldInfo._ObjectiveBank, an ordered list of keys localised from TransGame.int [Objectives] (e.g. D1_ORB_OBJ01
    "Escape the crash site").
  - SeqAct_TnSetCurrentObjective(Description = key) -> GRI.SetCurrentObjective -> the bank index replicates.
  - The client queues "complete" for the previous one and "add" for the new one.
  - TnHUD invokes `_global.AddObjective(text)`, and `_global.CompleteObjective(text)`, which is a no-op in the movie.
  - An empty description means no objective.
- **Today:** `HudController::addObjective(text)` exists (events_ queue -> `_global.AddObjective`); nothing calls it.
  `<CurrentGame:ActiveObjectives>` returns "" (the pause menu's Current Objective is hidden).
- **Hook:** `frontend::CampaignObjectives` (new, frontend side):
  - `setBank(keys)` from the map's exported _ObjectiveBank (AssetTools' campaign map export: open question C1);
  - `setCurrent(key)` called by the campaign Kismet runner for SeqAct_TnSetCurrentObjective. It localises the key,
    calls `hud.addObjective(text)` when the index changes, and keeps the completed list (everything before the
    current index, as the original's LastCompletedObjective = Current - 1).
  - Data stores: `<CurrentGame:ActiveObjectives>` = the current text, plus the "|"-joined completed list for the pause
    menu (`GetCompletedObjectives`).
  - `<CurrentGame:CurrentObjectiveCountdown>` stays -1 unless a Kismet countdown sets it (open question C2).
- **After a HUD reset** (map load, rejoin): the current objective is sent once more. **PC choice:** the original
  replayed its queue only on change. Why: a reopened HUD would otherwise show nothing until the next objective.
- **Test hud.campaign_objective_*** (headless HudController + the recording presenter the hud.* tests use):
  - setCurrent(D1_ORB_OBJ01) -> exactly `_global.AddObjective(Escape the crash site)`;
  - the same key again -> no call;
  - the next key -> one call; ActiveObjectives = its text; completed list = the earlier entries;
  - an empty key -> no AddObjective and ActiveObjectives "";
  - an unknown key -> no call, a trace line (the original logs an error);
  - after reset() the current one is re-sent once;
  - versus (campaign flag off): no call from any of the above.

## E2. Interact / contextual prompts

- **Original:**
  - TnHudDataObserverContextualCommands pops PC.ContextualCommander notifications, each a localised description with
    bindings turned into {x}-style tokens.
  - The movie call is `NotifyContextualCommand(0, Action, Text)`: Action 0 adds, 1 removes; an empty Text clears all.
  - `mc_notify` stacks newest on top, -27 px apart; a removal fades out and re-stacks in 0.2 s.
- **Today:** fully driven for versus. HudFrame.contextualPrompts is diffed into add / remove; a device switch clears
  and re-sends (promptsChanged); the text goes through the prompt translator (^COMMAND tokens to glyphs / key text).
  Test hud.prompts_and_cant_transform.
- **Hook:** none new. The campaign's interactables (turrets: TnInteractActionHideHud also hides the HUD; hacking
  terminals; buddy revive "hold" prompts) add their text to the same per-frame prompt list Gameplay already fills
  (Application_Frontend: `hf.contextualPrompts = o.contextual`).
- **Test:** extend hud.prompts_* with three stacked campaign texts (add order, remove the middle one: one remove call),
  and the cinematic case: E7's event 12 clears the stack with `(0, 1, "")`, and event 13 re-sends what is still active.

## E3. Revive progress bar

- **Original:**
  - TnHudDataObserverReviveBuddy: label "Reviving `p" with the buddy's name, or "Reviving" for AI; 0 when done.
  - The same bar serves hacking, bomb defuse and domination capture: `NotifyProgressBarChanged(Description,
    NormProgress)`.
  - The bar slides in at y -140 and out at y -120; bar width = background width x progress.
- **Today:** driven. progressObserver = "TnHudDataObserver" + labelId, label from TransGame.int, the name for `p, the
  label kept while it fades (tests hud.progress_bar_label, hud.progress_end_keeps_label, hud.progress_revive_name).
- **Hook:** Gameplay's campaign revive state (Pawn._RevivePercentDone / _ReviveTarget) into the existing progress
  fields (labelId "ReviveBuddy", the name, 0..1).
- **Test:** add the AI-buddy case. An empty name gives "Reviving", as the original's AI branch.

## E4. Own downed screen

- **Original (Hud_GFX 046c..05d7):**
  - h < 1 the first time: `ToggleSpectatorHudMode(true)`, read `<Strings:TransGame.ContextualCommands.DownedSuicide>`
    (the suicide prompt), attach `mc_incapacitatedScreen` at alpha = downed percent, then `UpdateDownedHealth(h)` on each
    change.
  - h back to 1: `ToggleSpectatorHudMode(false)` and `IncapacitatedScreenClip.Revived()`.
  - NotifyDownedWarningFade drives the HurtScreen warning.
- **Today:** the send path exists (HudFrame.downedHealth -> `NotifyNormalizedDownedHealthChanged` on change). World
  reports -1 (no downed state in versus), so nothing is sent.
  - **Gap:** DataStores does not answer `<Strings:File.Section.Key>`; the movie would get "" for the suicide prompt.
- **Hooks:**
  1. Gameplay's campaign downed health (0..1, 1 = up) into hf.downedHealth.
  2. DataStores: `<Strings:TransGame.Section.Key>` answered from the catalog (Catalog::localize(file, section, key)).
     Generic, and harmless for versus.
- **Tests:**
  - hud.campaign_downed_*: 1 -> 0.4 -> 0.2 -> 1 gives exactly three NotifyNormalizedDownedHealthChanged calls with those
    values, none while it stays at 1, and nothing in versus;
  - datastores.strings_store: `<Strings:TransGame.ContextualCommands.DownedSuicide>` = the INT text.
  - The movie-side visuals (spectator HUD mode, the screen clip) are checked once in a windowed capture when the
    campaign boots.

## E5. Tutorial messages

- **Original:**
  - TnSeqAct_DisplayTutorialMessage(MessageType) -> every PC -> TnHUD.DisplayTutorialMessage.
  - **Shown once per profile** (HasShownTutorialMessage / MarkTutorialMessageAsShown).
  - Text: Localize("TutorialMessages", <enum>, "TransGame"), e.g. TMT_SeeHealthTutorial "Health", with command -> button
    translation.
  - Sound BL_LVL_HUD_INTERFACE.WHSH_TUTORIAL_BOX (script side).
  - `_global.NotifyTutorialMessage(TutorialName)`: shown 3000 ms, then minimised to the mid-right stack (25 px apart),
    with the "Tutorial Online" / "View in Pause Menu" texts ($UIText.Hud.TutorialAvailable / TutorialPause).
  - The pause menu's Tutorials page lists them ($UIText.PauseMenu.Tutorials, TutHealth ...).
- **Today:** nothing.
- **Hooks:**
  - `HudController::tutorialMessage(text)`: one event, `_global.NotifyTutorialMessage`, like the other event
    observers.
  - The once-per-profile record: a `[Tutorials]` section in wfc_profile.ini (shown enum names), checked before the
    call and written after it.
  - Called by the campaign Kismet runner with the enum name; the frontend localises and translates commands.
  - The sound goes to Systems' UI sound path (IFrontendAudio::playUiSound, "WHSH_TUTORIAL_BOX").
- **Test hud.campaign_tutorial_*:**
  - the first TMT_SeeHealthTutorial -> one `_global.NotifyTutorialMessage(Health)`, the profile records it;
  - the same type again (same session, and a fresh LocalProfile loaded from the saved file) -> nothing;
  - a second type -> one call;
  - versus -> nothing.

## E6. Subtitles (RE PARTIAL; placement is a PROPOSAL)

- **Known:**
  - subtitles come from Engine.SubtitleManager (High Moon script: QueueSubtitles with a priority and a speaker);
  - Hud_GFX has no subtitle handler and TnHUD never draws them, so they are drawn on the engine canvas;
  - the profile's Subtitles setting turns them on / off (default "False" in our profile defaults).
- **Proposal (marked PROPOSAL until RE traces the canvas path):**
  - **Where:** bottom centre, inside the HUD's own safe margins: the Hud_GFX bottomCenter_mc anchor
    (updateStageScaling, sprite 654: alignBottom from safeMarginY). Placed above that anchor's content so the HUD's
    bottom-centre elements are never covered.
  - **Look:** at most two lines, centred. "Speaker: text" with the speaker name in the HUD's team colour (blue
    Autobots / red Decepticons, the kill-feed colours), white text with the HUD's soft shadow.
  - **Basis:** stock UE3 draws subtitles centred near the bottom of the title-safe region, and High Moon's queue
    carries a speaker.
  - **How:** the overlay text-field technique of the FPS counter / DLSS credit (a text field in $NormalFont on its own
    overlay movie, drawn after the HUD), so no Hud_GFX change.
  - **Timing:** the line shows for the dialogue cue's duration, which Systems' dialogue playback reports; a
    higher-priority line replaces a lower one, as SubtitleManager's priority does.
  - **Hidden** in cinematic mode only if the cinematic hides the HUD and the player has subtitles off. Matinee
    cinematics keep subtitles (they are the main use).
- **Hook:** `frontend::Subtitles::show(speaker, text, seconds, priority)` / `clear()`, called by Systems' dialogue
  player.
- **Test hud.subtitles_*:** show -> the overlay text = "Speaker: text"; a lower priority while one is up -> ignored; a
  higher one replaces it; the time runs out -> cleared; profile Subtitles False -> nothing shown.

## E7. Cinematic mode HUD hide

- **Original:**
  - SeqAct_ToggleCinematicMode: bHideHUD hides the HUD; when it comes back (and no full-screen movie plays), UI event
    13 goes to the UIController.
  - It also disables move / look / buttons, hides the player, and revives a downed player (a WFC extra).
  - Turrets and the hacking terminal hide the HUD through TnInteractActionHideHud.
- **Today:** UIController handles 12 (ShowHudFalse: hide, hideHud_ kept) and 13 (ShowHudTrue: show). Nothing raises
  them outside full-screen movies.
- **Hooks:**
  - the campaign Kismet runner, for SeqAct_ToggleCinematicMode: `flow.onUIEvent(12)` on enable, `13` on disable
    unless a full-screen movie plays (the original's condition);
  - the input / pawn parts are Gameplay's.
  - The HUD's prompt stack is cleared on 12 (E2).
- **Test ui.cinematic_hide_show:**
  - InGame + 12 -> HUD hidden;
  - 13 -> shown;
  - 13 while fullScreenMovie() -> stays hidden until the movie ends;
  - 12 / 13 during Paused keep the pause menu.

## Order and why

1. **E1 objective.** Pure frontend plus data; the first thing a campaign map shows. It needs the objective bank
   export (C1).
2. **E5 tutorials.** Pure frontend plus the profile record; the first campaign map (Orbital / Kaon) fires them early.
3. **E7 cinematic hide.** A two-line hook on existing UI events, needed by every campaign intro.
4. **E4 downed screen.** The `<Strings:>` store is a frontend-only fix that can land now; the downed health waits on
   Gameplay.
5. **E2 prompts.** No frontend code, only tests; the texts come with Gameplay's campaign interactables.
6. **E3 revive bar.** The same; waits on Gameplay's revive.
7. **E6 subtitles.** Last: it needs Systems' dialogue playback and timing, and the RE trace of the canvas placement
   (C3).

## Open questions

- **C1** Objective bank: does AssetTools' campaign map export carry TnWorldInfo._ObjectiveBank (the ordered keys)? If
  not, it is one more field in that export.
- **C2** Objective countdowns (`objectiveTime_mc`, `<CurrentGame:CurrentObjectiveCountdown>`): which campaign Kismet
  sets them, and from where.
- **C3** Subtitles: the canvas placement and font in the original (UE3 SubtitleManager's region on this title) - the
  RE lane's trace, to replace the E6 PROPOSAL.
- **C4** Tutorial record scope: per profile (the original) - and per platform or portable (the cross-play profile
  doc keeps progression portable; tutorials are presentation, so per system is proposed).
