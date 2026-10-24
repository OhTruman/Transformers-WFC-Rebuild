# Split screen: Frontend plan (next build; plan only, no code until Rendering's view-list interface)

User 2026-10-23 ~21:30 (via Integration): two local players on one PC, either two pads or keyboard / mouse plus a pad. RE
(SPLITSCREEN_RE.md): **the original has no split screen** (one local player; every MP mode is online / system link), so the
modes and layout are a rebuild decision built on the stock UE3 split machinery. Decisions (Integration, DLC_PLAN.md): seat 1 =
keyboard / mouse or the first pad, seat 2 = the next other pad that presses Start; a per-seat profile and customization; one
GFx HUD per view, title-safe per view; two local seats = two PRIs on one connection, counted against MaxPlayers; a Settings
row Split Layout, Horizontal (default) or Vertical.

## 1. Seats and input
- **SeatManager** (FrontendRuntime): seat 1 = keyboard / mouse plus the first pad that pressed Start at press-start (today's
  binding); seat 2 = unbound.
- **Seat 2 joins** when another pad (not seat 1's) presses Start where joining is allowed: the party lobby and the game lobby,
  offline or as host. It doesn't join mid-match, at the press-start screen or in a campaign chapter. A PC text line shows
  "Player 2 joined (pad n)".
- **Seat 2 leaves** with Back in a lobby (confirm popup), or when its pad disconnects (a popup in the lobby; mid-match the seat
  pauses with "Reconnect the controller").
- **Input routing:**
  - in menus, the seat that owns the open per-seat screen drives it (section 3); everything else is seat 1, as the original's
    one UI;
  - in a match, each seat's InputFrame goes to its own player.
  - Unbound pads are ignored (the original's IsValidController rule).
- The platform layer has to give each pad its own InputFrame (XInput index). Today pads merge into one frame.

## 2. Profiles and settings
- Seat 2 gets its own local profile `wfc_profile_2.ini` (named "Player 2" until renamed) and its own `wfc_characters_2.ini`.
  The same LocalProfile class, loaded from a different file.
- **Per seat:** controls (bindings, sensitivity, invert, remaps), customization / unlocks / XP, voice mute list.
- **Global (seat 1's profile):** display / graphics / audio / language, plus the new **Split Layout** row ([PCSettings]
  SplitLayout: 1 Horizontal default, 2 Vertical), written only when not default.
- **Campaign saves, checkpoints and achievements** stay with seat 1 (the original keys them to GamePlayers[0]).

## 3. Menus with two local seats
- **The menus stay one UI driven by seat 1** (the original's single-seat movies).
- **Seat 2 gets per-seat screens** that open for it and take its pad:
  - Choose Character / Create a Character (CustomTransformers_GFX, CharacterSelect_GFX in campaign / iconic) and its profile's
    Controls page;
  - seat 2 opens them from the lobby (Y "Player 2: Choose Character", a PC hint) or when the launch needs its pick.
- **Lobby lists:** both locals appear as two rows (two PRIs). Claims cover both: two trio picks in co-op, iconic picks in
  Escalation.
- **Player counts** include both seats against MaxPlayers (co-op 3, Escalation 4, versus 10). A full lobby refuses the join
  with the original's full-lobby text.

## 4. Online with two local seats
- One session member carries 2 humans. **Systems' ISession:** SessionMemberInfo gets `localSeats` (1 / 2) and seat 2's display
  name and character; the host's reservation and MaxPlayers count both.
- The launch record lists both humans in seat order. Each local seat gets a lockstep match seat (Systems / Gameplay: the
  driver feeds two local input streams).
- Join-in-progress, leavers and the late-seat rules stay per human: seat 2 leaving hands its slot to a bot as today.

## 5. In match
- **One UIController per seat for the in-match states** (WaitingOnGameStart, InGame, Spectating / respawn, Paused,
  CampaignGameOver, GameEnded).
  - Respawn, death and spectating screens are per view; the scoreboard is per view and highlights its own name.
  - **Pause:** either seat's Start pauses offline play for both; one pause menu, for the seat that paused. Online it's that
    seat's menu only, as today.
  - The end-of-match screen is shown once (full screen, seat 1 drives); XP for each seat goes to its own profile.
- **HUD:** one Hud_GFX instance per seat, laid out in that seat's view rect from Rendering's view list.
  - It uses that view's size, with the title-safe 0.9 inset per view, and the HUD movie's own anchoring for a smaller stage.
  - The kill feed and objective messages go to both HUDs (each from its own player's perspective: "you" lines).
- **Loading screens, movies and cinematics** stay full screen, shown once.

## 6. Dependencies
- **Rendering:** the view list (count, per-view rect, per-view camera), the layout switch (horizontal / vertical), and a per-view
  GFx compositing target.
- **Gameplay:** two local players in World (localMatchPlayer per seat), per-seat HudGameState and match events, and Kismet
  `GetLocalPlayerController` callers moved to the owning player (RE §3).
- **Systems:** per-seat input frames from the platform layer, two audio listeners (nearest wins), and ISession localSeats.
- **AssetTools / RE:** nothing new beyond SPLITSCREEN_RE.md.

## 7. Order and tests
1. SeatManager + per-pad input frames + seat 2 profile files (headless test: Start on pad 2 joins seat 2; the profile files are
   separate).
2. Lobby rows and counts for two local seats + the per-seat character pick (FakeSession test: one member with two humans; the
   MaxPlayers count).
3. Per-seat UIController + HUD instances on Rendering's view list (one card run: a two-seat private TDM in both layouts).
4. Online two-seat member (Systems' localSeats; pt9-style two-window run with a two-seat member).
5. Split Layout Settings row (profile round trip, applied through Rendering's switch).
