// Clean-room reconstruction — the Systems audio interface the frontend owner drives (menus, lobbies, loading,
// match start / return). Sources: AssetTools frontend_audio.json / frontend_flow.json / frontend_loading.json and
// the decompiled script (TnSoundActionScriptBinding, HmPlayerController.StopSound, HmMusicPlayer), read-only.
//   * UI sounds [script CONF, lookup HIGH]: GFx ActionScript Sound.PlaySound(name) ->
//     TnSoundActionScriptBinding.PlaySound -> PlayerController.GetUISound(name) (native lookup; AssetTools: the
//     name is a SoundCue name of BL_HUD_INTERFACE / BL_LVL_HUD_INTERFACE) -> CreateAudioComponent(cue, bPlay true).
//     StopSound(name, fade): HmPlayerController.StopSound fades out the FIRST AudioComponent playing that cue
//     (the rebuild takes the oldest instance [HIGH]).
//   * Music [CONF]: the level's SeqAct_PlayMusic / SeqAct_StopMusic -> MusicPlayer (HmMusicPlayer). Authored
//     frontend tracks: UI_FrontEnd_m FRONTEND_MX_ORBIT_01 (FadeIn 0.25), UI_Lobby_m / UI_CampaignLobby_m
//     MP_LOBBY_MX (FadeIn 0), UI_PartyLobby_m MP_PARTY_LOBBY_MX (FadeIn 0, FadeOut 0). frontendTrack() returns them.
//   * Loading [CONF data]: no music is authored for loading (the loading movie's audio is inside the Bink file).
//   * Level change [HIGH]: the WorldInfo-owned music player goes with the level -> onLevelChange().
// Not here (frontend owner / other lanes): UI screens, GFx, movies, Kismet sequencing of the UI levels; the UI
// levels' ambience / PlayPlayerPositionalSound / SeqAct_Reverb need a UI-level audio manifest (AssetTools) and then
// load through the same map-audio path (World::loadMapAudio / AmbientAudio).
#pragma once
#include "game/MusicPlayer.h"
#include "game/SoundCues.h"

namespace game {

class FrontendAudio {
public:
    explicit FrontendAudio(SoundCues& cues) : cues_(cues), music_(cues) {}

    // TnSoundActionScriptBinding.PlaySound(name). Returns the cue instance (-1: no such UI sound).
    int playUiSound(const char* name);
    // TnSoundActionScriptBinding.StopSound(name, fade): the oldest instance of that cue fades out.
    bool stopUiSound(const char* name, float fadeSeconds);
    // The SoundCue a GFx sound name resolves to ("" if none).
    std::string uiCueFor(const char* name) const;

    MusicPlayer& music() { return music_; }
    // The authored SeqAct_PlayMusic track of a UI level (UI_FrontEnd_m / UI_Lobby_m / UI_CampaignLobby_m /
    // UI_PartyLobby_m); false if the level authors none.
    static bool frontendTrack(const char* uiLevel, MusicTrack& out);

    void tick(float dt) { music_.tick(dt); }
    // Level change (frontend -> match, match -> frontend): music + stinger stop at once.
    void onLevelChange() { music_.onOwnerDestroyed(); }

private:
    SoundCues& cues_;
    MusicPlayer music_;
};

} // namespace game
