#include "game/CharacterAudio.h"
#include "game/SoundCues.h"
#include "assets/Json.h"
#include "core/Config.h"
#include "core/Log.h"

#include <cstdlib>

namespace game {
namespace {

#include "game/CharacterAudio.inc"

struct Db {
    assets::Json doc;
    std::map<std::string, CharacterAudioProfile> profiles;
    std::map<std::string, std::map<std::string, std::string>> weaponEvents;
    std::map<std::string, std::string> weaponPickup;
    std::map<std::string, WeaponHitEffect> weaponHit;
    std::map<std::string, WeaponAnimSounds> weaponAnims;
};

const Db& db() {
    static Db d;
    static bool init = false;
    if (init) return d;
    init = true;
    if (!assets::Json::parse(std::string(kCharacterAudioJson), d.doc)) { LOG_WARN("character audio: bad data"); return d; }
    for (const auto& kv : d.doc["profiles"].obj) {
        const assets::Json& j = kv.second;
        CharacterAudioProfile p;
        p.key = kv.first;
        p.name = j["name"].asString();
        p.faction = j["faction"].asString();
        p.voiceSet = j["voice_set"].asString();
        p.vehicleSet = j["vehicle_set"].asString();
        p.vehicleDeath = j["vehicle_death_sound"].asString();
        for (const auto& e : j["voice"].obj) p.voice[e.first] = e.second.asString();
        for (const auto& e : j["vehicle"].obj) p.vehicle[e.first] = e.second.asString();
        for (const auto& c : j["clips"].obj) {
            CharacterAudioProfile::Clip clip;
            clip.length = c.second["length"].asFloat(0.0f);
            const assets::Json& ns = c.second["notifies"];
            for (size_t i = 0; i < ns.size(); ++i)
                clip.notifies.push_back({ns[i]["t"].asFloat(), ns[i]["event"].asString(), ns[i]["cue"].asString(),
                                         ns[i]["min_weight"].asFloat(0.25f)});
            p.clips[c.first] = clip;
        }
        for (size_t i = 0; i < j["weapons"].size(); ++i) p.weapons.push_back(j["weapons"][i].asString());
        d.profiles[p.key] = p;
    }
    for (const auto& kv : d.doc["weapons"].obj) {
        for (const auto& e : kv.second["events"].obj) d.weaponEvents[kv.first][e.first] = e.second.asString();
        d.weaponPickup[kv.first] = kv.second["pickup_sound"].asString();
        const assets::Json& an = kv.second["anims"];
        if (an.isObject() && !an.obj.empty()) {
            WeaponAnimSounds& ws = d.weaponAnims[kv.first];
            const std::pair<const char*, WeaponAnimSounds::Clip*> slots[] = {
                {"Idle", &ws.idle}, {"WP_Fire", &ws.fire}, {"WP_Reload", &ws.reload}, {"WP_Equip", &ws.equip}, {"WP_PutDown", &ws.putDown}};
            for (const auto& sl : slots) {
                const assets::Json& j = an[sl.first];
                if (!j.isObject()) continue;
                sl.second->name = j["clip"].asString();
                sl.second->length = j["length"].asFloat();
                for (size_t i = 0; i < j["sounds"].size(); ++i)
                    sl.second->sounds.push_back({j["sounds"][i][0].asFloat(), j["sounds"][i][1].asString()});
            }
        }
        const assets::Json& h = kv.second["hit_effect"];
        if (h.isObject()) {
            WeaponHitEffect e;
            e.damageType = h["damage_type"].asString();
            e.hitEvent = h["hit_event"].asString();
            e.blockEvent = h["block_event"].asString();
            e.index = h["index"].asInt(-1);
            e.retrigger = h["retrigger"].asFloat();
            e.causesBlood = h["causes_blood"].asBool();
            d.weaponHit[kv.first] = e;
        }
    }
    return d;
}

const std::string& empty() { static const std::string e; return e; }

} // namespace

const std::string& CharacterAudioProfile::voiceCue(const std::string& event) const {
    auto it = voice.find(event);
    return it == voice.end() ? empty() : it->second;
}

const std::string& CharacterAudioProfile::vehicleCue(const std::string& event) const {
    auto it = vehicle.find(event);
    return it == vehicle.end() ? empty() : it->second;
}

const std::string& CharacterAudioProfile::notifyCue(const Notify& n) const {
    return n.cue.empty() ? voiceCue(n.event) : n.cue;
}

const CharacterAudioProfile::Clip* CharacterAudioProfile::clip(const std::string& name) const {
    auto it = clips.find(name);
    return it == clips.end() ? nullptr : &it->second;
}

const CharacterAudioProfile* CharacterAudio::find(const std::string& k) {
    const Db& d = db();
    auto it = d.profiles.find(k);
    if (it != d.profiles.end()) return &it->second;
    for (const auto& kv : d.profiles) if (!kv.second.name.empty() && kv.second.name == k) return &kv.second;
    return nullptr;
}

const CharacterAudioProfile& CharacterAudio::defaultProfile() {
    static CharacterAudioProfile none;
    const CharacterAudioProfile* p = find("Truck");
    return p ? *p : none;
}

int CharacterAudio::profileCount() { return (int)db().profiles.size(); }

std::vector<std::string> CharacterAudio::keys() {
    std::vector<std::string> k;
    for (const auto& kv : db().profiles) k.push_back(kv.first);
    return k;
}

int CharacterAudio::loadCues(SoundCues& cues, const CharacterAudioProfile& p) {
    const Db& d = db();
    assets::Json sub;
    sub.type = assets::Json::Type::Object;
    auto want = [&](const std::string& q) {
        if (q.empty() || cues.hasCue(q.c_str()) || sub.has(q)) return;
        const assets::Json& t = d.doc["cues"][q];
        if (t.isObject()) sub.obj[q] = t;
    };
    for (const auto& e : p.voice) want(e.second);
    for (const auto& e : p.vehicle) want(e.second);
    want(p.vehicleDeath);
    for (const auto& c : p.clips) for (const auto& n : c.second.notifies) want(n.cue);
    for (const std::string& w : p.weapons) {
        auto it = d.weaponEvents.find(w);
        if (it != d.weaponEvents.end()) for (const auto& e : it->second) want(e.second);
    }
    if (sub.obj.empty()) return 0;
    const char* root = std::getenv("WFC_ASSETS");
    const std::string content = std::string(root ? root : core::config::kAssetRootDefault) + "/../content/";
    return cues.addCues(sub, content);
}

int CharacterAudio::loadWeaponCues(SoundCues& cues, const std::string& cls) {
    const Db& d = db();
    assets::Json sub;
    sub.type = assets::Json::Type::Object;
    auto want = [&](const std::string& q) {
        if (!q.empty() && !cues.hasCue(q.c_str()) && d.doc["cues"][q].isObject()) sub.obj[q] = d.doc["cues"][q];
    };
    auto it = d.weaponEvents.find(cls);
    if (it != d.weaponEvents.end()) for (const auto& e : it->second) want(e.second);
    auto an = d.weaponAnims.find(cls);
    if (an != d.weaponAnims.end())
        for (const WeaponAnimSounds::Clip* c : {&an->second.idle, &an->second.fire, &an->second.reload, &an->second.equip, &an->second.putDown})
            for (const auto& n : c->sounds) want(n.second);
    if (sub.obj.empty()) return 0;
    const char* root = std::getenv("WFC_ASSETS");
    return cues.addCues(sub, std::string(root ? root : core::config::kAssetRootDefault) + "/../content/");
}

const std::string& CharacterAudio::weaponCue(const std::string& cls, const std::string& event) {
    const Db& d = db();
    auto it = d.weaponEvents.find(cls);
    if (it == d.weaponEvents.end()) return empty();
    auto e = it->second.find(event);
    return e == it->second.end() ? empty() : e->second;
}

const WeaponAnimSounds* CharacterAudio::weaponAnimSounds(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponAnims.find(cls);
    return it == d.weaponAnims.end() ? nullptr : &it->second;
}

const WeaponHitEffect* CharacterAudio::weaponHitEffect(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponHit.find(cls);
    return it == d.weaponHit.end() ? nullptr : &it->second;
}

int CharacterAudio::loadHitCues(SoundCues& cues, const CharacterAudioProfile& victim, const std::string& cls) {
    const WeaponHitEffect* h = weaponHitEffect(cls);
    if (!h) return 0;
    const Db& d = db();
    assets::Json sub;
    sub.type = assets::Json::Type::Object;
    for (const std::string* ev : {&h->hitEvent, &h->blockEvent}) {
        const std::string q = victim.voiceCue(*ev);
        if (!q.empty() && !cues.hasCue(q.c_str()) && d.doc["cues"][q].isObject()) sub.obj[q] = d.doc["cues"][q];
    }
    if (sub.obj.empty()) return 0;
    const char* root = std::getenv("WFC_ASSETS");
    return cues.addCues(sub, std::string(root ? root : core::config::kAssetRootDefault) + "/../content/");
}

const std::string& CharacterAudio::weaponPickupSound(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponPickup.find(cls);
    return it == d.weaponPickup.end() ? empty() : it->second;
}

} // namespace game
