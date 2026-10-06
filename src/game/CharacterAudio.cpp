#include "game/CharacterAudio.h"
#include <set>
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
    std::map<std::string, WeaponFxTemplates> weaponFx;
    std::map<std::string, WeaponProjectile> weaponProj;
    std::map<std::string, bool> weaponBeam;
    std::map<std::string, std::map<std::string, std::pair<float, float>>> weaponFades;
    std::map<std::string, std::string> abilityTrigger;
    std::map<std::string, BuffSounds> buffs;
    std::map<std::string, std::map<std::string, std::string>> classSounds;
    std::set<std::string> meleeDamageTypes;
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
        p.vehicleForm = j["vehicle_form"].asString();
        const assets::Json& vf = j["vehicle_fx"];
        if (vf.isObject()) {
            VehicleFxData& x = p.vehicleFx;
            const std::pair<const char*, std::vector<VehicleFxData::Entry>*> sets[] = {
                {"HoverFX", &x.hover}, {"BoostFx", &x.boost}, {"JumpFX", &x.jump}, {"RamFX", &x.ram}};
            for (const auto& st : sets)
                for (size_t i = 0; i < vf["sets"][st.first].size(); ++i)
                    st.second->push_back({vf["sets"][st.first][i]["template"].asString(), vf["sets"][st.first][i]["socket"].asString()});
            for (const auto& so : vf["sockets"].obj) {
                VehicleFxData::Socket s{};
                s.bone = so.second["bone"].asString();
                for (int k = 0; k < 16; ++k) s.rel[k] = so.second["rel"][(size_t)k].asFloat();
                for (int k = 0; k < 3; ++k) s.scale[k] = so.second["scale"][(size_t)k].asFloat(1.0f);
                x.sockets[so.first] = s;
            }
            const assets::Json& tv = vf["toggle"]["Transform_ToVehicle_VEH"];
            for (size_t i = 0; i < tv.size(); ++i)
                if (tv[i]["option"].asString() != "Toggle_DisableFx" && tv[i]["duration"].asFloat() > 0.0f)
                    x.enableFraction = tv[i]["t"].asFloat() / tv[i]["duration"].asFloat();
            x.valid = !x.hover.empty() || !x.boost.empty() || !x.jump.empty() || !x.ram.empty();
        }
        const assets::Json& vc = j["vehicle_component"];
        if (vc.isObject()) {
            VehicleAudioComponentData& v = p.vehicleComponent;
            auto list = [](const assets::Json& a, std::vector<std::string>& out) {
                for (size_t i = 0; i < a.size(); ++i) out.push_back(a[i].asString());
            };
            auto gear = [&](const assets::Json& g, VehicleAudioComponentData::Gear& out) {
                out.maxSpeed = g["max_speed"].asFloat();
                list(g["on_loops"], out.onLoops); list(g["on_oneshots"], out.onOneshots);
                list(g["off_loops"], out.offLoops); list(g["off_oneshots"], out.offOneshots);
            };
            for (size_t i = 0; i < vc["drive"].size(); ++i) { v.drive.emplace_back(); gear(vc["drive"][i], v.drive.back()); }
            gear(vc["reverse"], v.reverse);
            list(vc["boost_loops"], v.boostLoops); list(vc["boost_oneshots"], v.boostOneshots);
            list(vc["jump_rev"]["loops"], v.jumpLoops); list(vc["jump_rev"]["oneshots"], v.jumpOneshots);
            v.useJumpRev = vc["jump_rev"]["use"].asBool();
            for (const char* k : {"hover_land", "boost_land"})
                for (size_t i = 0; i < vc[k].size(); ++i)
                    (k[0] == 'h' ? v.hoverLand : v.boostLand).push_back({vc[k][i]["t"].asFloat(), vc[k][i]["event"].asString()});
            const assets::Json& s = vc["slots"];
            v.boost = s["BoostSound"].asString(); v.boostWheels = s["BoostWheelsSound"].asString();
            v.boostStop = s["BoostStopSound"].asString(); v.ascend = s["AscendSound"].asString(); v.ram = s["RamSound"].asString();
            v.booster = s["BoosterSound"].asString(); v.nitro = s["NitroSound"].asString(); v.squeal = s["DefaultTireSquealSound"].asString();
            v.speed = s["SpeedSound"].asString(); v.ascendStop = s["AscendStopSound"].asString(); v.descend = s["DescendSound"].asString();
            v.descendStop = s["DescendStopSound"].asString(); v.roll = s["RollSound"].asString(); v.oneEighty = s["OneEightySound"].asString();
            v.enter = s["EnterSound"].asString(); v.exit = s["ExitSound"].asString(); v.tread = s["DefaultTireTreadSound"].asString();
            const assets::Json& t = vc["tunables"];
            v.boostFadeIn = t["boost_fade_in"].asFloat(v.boostFadeIn); v.boostFadeOut = t["boost_fade_out"].asFloat(v.boostFadeOut);
            v.boostWheelsDelay = t["boost_wheels_delay"].asFloat(v.boostWheelsDelay); v.squealMinMph = t["squeal_min_mph"].asFloat(v.squealMinMph);
            v.squealFade = t["squeal_fade"].asFloat(v.squealFade); v.engineFadeIn = t["engine_fade_in"].asFloat(v.engineFadeIn);
            v.engineFadeOut = t["engine_fade_out"].asFloat(v.engineFadeOut); v.jumpRevTime = t["jump_rev_time"].asFloat(v.jumpRevTime);
            v.oneshotSpazTime = t["oneshot_spaz_time"].asFloat(v.oneshotSpazTime); v.speedHistory = t["speed_history"].asInt(15);
            v.treadFade = t["tread_fade"].asFloat(v.treadFade);
            v.valid = true;
        }
        d.profiles[p.key] = p;
    }
    for (const auto& kv : d.doc["weapons"].obj) {
        for (const auto& e : kv.second["events"].obj) d.weaponEvents[kv.first][e.first] = e.second.asString();
        d.weaponPickup[kv.first] = kv.second["pickup_sound"].asString();
        const assets::Json& pj = kv.second["projectile"];
        if (pj.isObject())
            d.weaponProj[kv.first] = {pj["class"].asString(), pj["flight_sound"].asString(), pj["secondary_flight_sound"].asString(),
                                      pj["explosion_sound"].asString(), pj["flight_effect"].asString(), pj["explosion_effect"].asString(),
                                      pj["bounce_sound"].asString(), pj["fuse_sound"].asString()};
        d.weaponBeam[kv.first] = kv.second["beam"].asBool();
        for (const auto& f : kv.second["fades"].obj) d.weaponFades[kv.first][f.first] = {f.second[0].asFloat(), f.second[1].asFloat()};
        const assets::Json& fx = kv.second["fx"];
        if (fx.isObject()) d.weaponFx[kv.first] = {fx["muzzle"].asString(), fx["tracer"].asString(), fx["squib"].asString()};
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
    for (const auto& kv : d.doc["abilities"].obj) d.abilityTrigger[kv.first] = kv.second["trigger"].asString();
    for (size_t i = 0; i < d.doc["melee_damage_types"].size(); ++i) d.meleeDamageTypes.insert(d.doc["melee_damage_types"][i].asString());
    for (const auto& kv : d.doc["class_sounds"].obj)
        for (const auto& f : kv.second.obj) d.classSounds[kv.first][f.first] = f.second.asString();
    for (const auto& kv : d.doc["buffs"].obj) {
        const assets::Json& j = kv.second;
        BuffSounds b;
        b.apply = j["ApplySound"].asString(); b.unapply = j["UnapplySound"].asString();
        b.autobotApply = j["AutobotApplySound"].asString(); b.autobotUnapply = j["AutobotUnapplySound"].asString();
        b.decepticonApply = j["DecepticonApplySound"].asString(); b.decepticonUnapply = j["DecepticonUnapplySound"].asString();
        b.heal = j["HealSound"].asString(); b.damage = j["DamageSound"].asString(); b.activation = j["ActivationSound"].asString();
        b.onlyLocal = j["only_local"].asBool(true);
        d.buffs[kv.first] = b;
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
    auto pj = d.weaponProj.find(cls);
    if (pj != d.weaponProj.end()) {
        want(pj->second.flightSound); want(pj->second.secondaryFlightSound); want(pj->second.explosionSound);
        want(pj->second.bounceSound); want(pj->second.fuseSound);
    }
    auto an = d.weaponAnims.find(cls);
    if (an != d.weaponAnims.end())
        for (const WeaponAnimSounds::Clip* c : {&an->second.idle, &an->second.fire, &an->second.reload, &an->second.equip, &an->second.putDown})
            for (const auto& n : c->sounds) want(n.second);
    if (sub.obj.empty()) return 0;
    const char* root = std::getenv("WFC_ASSETS");
    return cues.addCues(sub, std::string(root ? root : core::config::kAssetRootDefault) + "/../content/");
}

const std::string& CharacterAudio::abilityTriggerSound(const std::string& cls) {
    const Db& d = db();
    auto it = d.abilityTrigger.find(cls);
    return it == d.abilityTrigger.end() ? empty() : it->second;
}

const std::string& CharacterAudio::classSound(const std::string& cls, const std::string& field) {
    const Db& d = db();
    auto it = d.classSounds.find(cls);
    if (it == d.classSounds.end()) return empty();
    auto f = it->second.find(field);
    return f == it->second.end() ? empty() : f->second;
}

bool CharacterAudio::isMeleeDamageType(const std::string& dt) { return db().meleeDamageTypes.count(dt) != 0; }

const BuffSounds* CharacterAudio::buffSounds(const std::string& cls) {
    const Db& d = db();
    auto it = d.buffs.find(cls);
    return it == d.buffs.end() ? nullptr : &it->second;
}

int CharacterAudio::loadAbilityCues(SoundCues& cues) {
    const Db& d = db();
    assets::Json sub;
    sub.type = assets::Json::Type::Object;
    auto want = [&](const std::string& q) {
        if (!q.empty() && !cues.hasCue(q.c_str()) && d.doc["cues"][q].isObject()) sub.obj[q] = d.doc["cues"][q];
    };
    for (const auto& kv : d.abilityTrigger) want(kv.second);
    for (const auto& kv : d.buffs)
        for (const std::string* q : {&kv.second.apply, &kv.second.unapply, &kv.second.autobotApply, &kv.second.autobotUnapply,
                                     &kv.second.decepticonApply, &kv.second.decepticonUnapply, &kv.second.heal, &kv.second.damage,
                                     &kv.second.activation})
            want(*q);
    for (const auto& kv : d.classSounds) for (const auto& f : kv.second) want(f.second);
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

const WeaponProjectile* CharacterAudio::weaponProjectile(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponProj.find(cls);
    return it == d.weaponProj.end() ? nullptr : &it->second;
}

bool CharacterAudio::weaponIsBeam(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponBeam.find(cls);
    return it != d.weaponBeam.end() && it->second;
}

void CharacterAudio::weaponEventFades(const std::string& cls, const std::string& ev, float& fi, float& fo) {
    fi = fo = 0.0f;
    const Db& d = db();
    auto it = d.weaponFades.find(cls);
    if (it == d.weaponFades.end()) return;
    auto e = it->second.find(ev);
    if (e != it->second.end()) { fi = e->second.first; fo = e->second.second; }
}

const WeaponFxTemplates* CharacterAudio::weaponFx(const std::string& cls) {
    const Db& d = db();
    auto it = d.weaponFx.find(cls);
    return it == d.weaponFx.end() ? nullptr : &it->second;
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
