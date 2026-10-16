# Pawn cloak light (TnPawn.CloakLight): RE result

Gameplay lane, 2026-10-16 03:51. Read-only. Sources: cooked script (TransGame.xxx, work/pass11/ue3dis.py), default properties
(AssetTools props.py), every cooked package's name table (2438 packages), and the native image (RE-Workspace fnref.py plus a
read-only headless Ghidra decompile). Raw dumps: Gameplay work/pass29/cloak/ (cloakdis.txt, scan.txt, q.out).

## Answer: no cloak-light values or trigger points exist. The light never turns on in the shipped game.

The cloak light is wired up but never fired. Nothing in script, native code or content ever calls `TriggerLight` on the
cloak light manager. The faithful behaviour is **no cloak light**. Do not wire one: any values used would be invented.

## Evidence

1. **Set up, never triggered (script).** These are the only script references to the cloak light:
   - `TnPawn.AddServices`: `_Services.Add('TnCloakLightService', _CloakLightManager)`
   - `TnPawn.set_CloakLightManager`: `_Services.Replace('TnCloakLightService', Value)`
   - `TnPawn.ApplyTransformer`: `_CloakLightManager.Initialize(CloakLight)`, which only stores the component
   - `TnPawn.Tick`: `_CloakLightManager.UpdateLight(DeltaTime)`

   `TriggerLight` is called in exactly two places, and both get `GetService('TnDeathLightService')`, the death light:
   - `TnAnimNotify_TriggerDeathLight.Notify`, using `DLPs`
   - `TnDeathModifierTriggerLight.OnStart`, using `LightProperties`

   No script function calls `GetService('TnCloakLightService')`, and none calls `_CloakLightManager.TriggerLight`.
2. **No caller in native code.** The exe has a hardcoded FName global for `TnCloakLightService` (0x8382a628). Its one code
   reference is in 0x82928D98, the native `AddServices`: 12 `Services.Add` calls in the same order as the script, with the
   cloak manager at pawn+0x714. That is registration only. `TnDeathLightService` has two more references, 0x828FBC98 and
   0x82964D50, which are the two death-light triggers above. `TriggerLight`, `UpdateLight`, `StartCloaking` and
   `StopCloaking` have no hardcoded FName in the exe, so native code cannot call them by name. I did not do an exhaustive
   scan of every native use of field +0x714, but `TriggerLight` is a non-native script function.
3. **No cloak values anywhere in content.**
   - **Script:** the cloak code never builds a `RampLightProperties`.
   - **Packages:** every package whose name table has `RampLightProperties` holds only death-light objects:
     `TnDeathModifierTriggerLight` in `TR_Deaths_p.Shared{Robot,Car,Tank}Deaths`, `TR_SpiderDec_ANM_p.SpiderDeaths`, and
     the ROBODEF / map / UI packages.
   - **Pawn archetypes:** every `CloakLight0` / `CloakLightManager0` in content is an empty override with no tagged
     properties. That covers `TnPlayerPawnMultiplayer` in all MP BASE maps and the campaign AI pawns.
4. **The light stays off at runtime.**
   - `Default__TnPawn.CloakLight0` (PointLightComponent): `bEnabled=false`, `RadiusOfInfluence 1013.76`,
     `LightColor` RGB (250,220,120), no shadows, `bForceDynamicLight`. Brightness 1 / Radius 1024 / Falloff 2 are not
     overridden; they are the component class defaults.
   - `TnRampLightManager.UpdateLight` returns at once while `CurrentTime == -1`. `-1` is the class default, and only
     `TriggerLight` sets `CurrentTime` to 0. So with no trigger, the per-tick update does nothing and `SetEnabled(true)`
     is never reached.

## Do not reuse these as cloak values

`Default__TnRampLightManager.CurrentRLPs` and `Default__TnDeathModifierTriggerLight.LightProperties` both hold the struct
defaults: Brightness 25, Radius 512, Falloff 10, white, RampIn 0.5, RampOut 0, LightTime 10, Translation 0. These are generic
or death-light values. They are not cloak values.

## Ramp law (for reference: what the death light uses)

- **TriggerLight(RLPs):** stores RLPs, sets `CurrentTime=0`, writes Radius and FalloffExponent to the component, calls
  `SetTranslation(RLPs.Translation)`, then `SetEnabled(true)`.
- **UpdateLight(dt):** `CurrentTime += dt`, then:
  - if `CurrentTime > LightTime`: disable the light and set `CurrentTime = -1`.
  - else if `CurrentTime >= LightTime - RampOutTime`: brightness = `(1 - (CurrentTime - (LightTime - RampOutTime)) / RampOutTime) * Brightness`.
  - else: brightness = `min(CurrentTime / RampInTime, 1) * Brightness`.
  - It then calls `SetLightProperties(brightness, LightColor)`.

## What the original cloak does show

These are the cloak's real presentation channels:

- **Buff:** `TnBuffCloak.Apply` calls `TnPawn.StartCloaking`. That sets `_IsCloaking` and runs `CharacterMaterials.Start(0)`.
- **Material:** `TnCloakMaterialOverrider` switches to `TR_Autobot_Cloak_MATINST` or `TR_Decepticon_Cloak_MATINST`.
  `StartOverride` sets the mesh to `CastShadow=false`, and `StopOverride` turns shadows back on.
- **Particles:** `Cloaking_FX` plays at `PositionSocket` on both apply and unapply. It does not loop. It is suppressed
  when the owner has `TnSkillDisableCloakEffects`.
- **Sounds:** apply is `CLOAK_{AUTOBOT,DECEPTICON}_START_LP` and unapply is `CLOAK_*_OFF`.
  `OnlyPlaySoundOnLocalPlayer=false`.
- **Replication and timing:** `kBuffReplicateToAll`, `BuffTime` 20 s / 15 s, ability cooldown 15 s / 15 s.
- **Unapply:** `TnBuffCloak.Unapply` calls `StopCloaking`. That runs `CharacterMaterials.Stop(0)`, and on the authority
  only, adds `TnBuffIncreaseDamageAfterCloak` / `TnBuffDecreaseDamageTakenAfterCloak` when the matching skills are owned.
- **Hidden or blocked while cloaked:** warcry and regen-streak FX (`ShouldFxBeVisible`), vehicle FX
  (`TnVehicleForm.get_FxAllowed`), highlight (`TnTransformerHighlighterComponent.CanHighlight`), and enemy target
  picking (`TnPlayerTargeting.IsValidTarget` / `IsActorEligibleForTargetPicking`). The enemy label is also disabled
  (`SetupEnemyMarker`: `HasBuff(TnBuffCloak)` leads to `DisableLabel`).
