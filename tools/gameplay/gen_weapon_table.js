// Generates src/game/WeaponTable.inc from authored data (read-only):
//   AssetTools authored.db: weapon class CDO -> MultiplayerData (versus: TnMultiplayerGame.DesiredWeaponDataType = 3,
//     CONFIRMED RE TARGETED_PASS3) or PlayerData when MultiplayerData is none (TnWeapon falls back to type 1, CONFIRMED),
//     over HM_Engine.Default__HmWeaponData + TransGame.Default__TnWeaponData.
//   AssetTools mp_weapons.json: mesh / event anims / effects / damage types / kill-feed icons / display names.
//   data/gameplay/weapon_sockets.json: SkeletalMeshSocket exports (AssetTools vs_character.sockets, read-only).
const {DatabaseSync} = require('node:sqlite');
const fs = require('fs');
const AT = 'F:/Transformers Rebuild/AssetTools/manifests/';
const EXT = 'F:/Transformers Rebuild/ExtractedAssets/';
const db = new DatabaseSync(AT + 'authored.db', {readOnly: true});
const get = op => { const r = db.prepare('select props from objects where opath=?').get(op); return r ? JSON.parse(r.props) : null; };
const W = JSON.parse(fs.readFileSync(AT + 'mp_content/mp_weapons.json', 'utf8')).weapons;
const SOCK = JSON.parse(fs.readFileSync('data/gameplay/weapon_sockets.json', 'utf8')).weapons;
const defaults = Object.assign({}, get('HM_Engine.Default__HmWeaponData') || {}, get('TransGame.Default__TnWeaponData') || {});
const q = s => JSON.stringify(s == null ? '' : String(s));
const f = x => (x == null || isNaN(x) ? 0 : Number(x)).toFixed(4) + 'f';
let rows = [];
for (const w of Object.values(W)) {
  const cdo = get(w.class.replace('TransContent.', 'TransContent.Default__')) || {};
  let src = 'MP', dataObj = cdo.MultiplayerData, d = dataObj ? get(dataObj) : null;
  if (!d) { src = 'SP'; dataObj = cdo.PlayerData; d = dataObj ? get(dataObj) : null; }
  if (!d) continue;
  d = Object.assign({}, defaults, d);
  const types = d.WeaponFireTypes || [];
  const code = w.weapon_type_code == null ? -1 : Number(w.weapon_type_code);
  let fire = 'Other';
  if (code === 2 || (d.MaxAmmoClipCount === 9999 && !d.InstantHitDamage)) fire = 'Melee';
  else if (code === 4) fire = 'Grenade';
  else if (types[0] === 'EWFT_Projectile') fire = 'Projectile';
  else if (/RepairRay/.test(w.class)) fire = 'Other';   // repair beams heal (TnWeaponRepairRay): not a damage hitscan [PARTIAL]
  else if ((d.InstantHitDamage && d.InstantHitDamage[0] > 0) || cdo.bInstantHit) fire = 'InstantHit';
  const rm = d.RangeDamageModifiers || [];
  const near = rm[0] || {Range: d.WeaponRange || 30000, Modifier: 1}, far = rm[rm.length - 1] || near;
  const ps = d.PerShotSpreadModifier || {};
  const psm = ps.Modifier || {};
  const iv = (d.FireIntervalModifier && d.FireIntervalModifier.IntervalRange) || {Min: 1, Max: 1};
  const m = w.mesh || {};
  const anim = m.gltf ? m.gltf.replace(/_SKEL\.gltf$/, '_ANIM.anim.gltf') : '';
  const animOk = anim && fs.existsSync(EXT + anim);
  const ev = {}; for (const e of (m.event_anims || [])) ev[e.WeaponEventType] = e.AnimName;
  const sk = (SOCK[w.class] || {}).sockets || [];
  const mz = sk.find(s => s.socket === (m.muzzle_sockets || ['MuzzleFlash'])[0]) || sk.find(s => /Muzzle/i.test(s.socket));
  const dtName = (d.InstantHitDamageTypes || [])[0] || Object.keys(w.damage_types || {})[0] || '';
  const dtRec = (w.damage_types || {})[dtName] || {};
  const icon = Object.keys(w.kill_feed_icon || {})[0] || '';
  const fx = w.effects || {};
  const muzzleFx = ((fx.muzzle_flashes || [])[0] || {}).PSTemplate || '';
  // Projectile: WeaponProjectiles[0] class CDO -> MultiplayerData (else Data) TnProjectileData [CONF authored].
  let pr = {InitialSpeed: 0, Damage: 0, DamageRadius: 0, DamageType: '', homing: false};
  const pcls = (d.WeaponProjectiles || [])[0];
  if (fire === 'Projectile' && pcls) {
    const pc = get(pcls.replace('TransContent.', 'TransContent.Default__')) || get(pcls.replace('TransContent.', 'TransGame.Default__')) || {};
    const pd = get(pc.MultiplayerData || '') || get(pc.Data || '');
    if (pd) pr = {InitialSpeed: pd.InitialSpeed || 0, Damage: pd.Damage || 0, DamageRadius: pd.DamageRadius || 0, DamageType: pd.DamageType || '', homing: !!pd.HomingForce};
  }
  const tracerFx = ((fx.tracers || [])[0] || {}).TracerTemplate || '';
  rows.push(`    {${q(w.class.replace('TransContent.TnWeapon', ''))}, ${q((w.provider_ids || [])[0] || '')}, ${q(w.display_name && w.display_name.INT)}, ${code}, ` +
    `WeaponFire::${fire}, ${q(src)}, ${q(dataObj)},\n` +
    `     ${f((d.InstantHitDamage || [0])[0])}, ${d.NumShotsToFire || 1}, ${d.MaxAmmoClipCount || 0}, ${d.MaxAmmoCount || 0}, ${d.InitialReserveAmmoCount || 0}, ` +
    `${f(iv.Min)}, ${f((d.WeaponRange || 30000) * 0.01)}, ${f(near.Range * 0.01)}, ${f(far.Modifier)}, ` +
    `${f(psm.Min)}, ${f(psm.Max)}, ${f(ps.ModifierChangePerShot)}, ${f(ps.Cooldown || 2)}, ${f(d.FineAimSpreadModifier || 1)}, ` +
    `${f(d.WeaponReloadAnimTime)}, ${f(d.EquipTime)}, ${f(d.PutDownTime)}, ${d.bAutoFire ? 'true' : 'false'}, ${f((d.HeatProperties || {}).HeatMax)},\n` +
    `     ${q(m.gltf)}, ${q(animOk ? anim : '')}, ${q(mz ? mz.bone : '')}, {${f(mz ? mz.relative_location_ue[0] : 0)}, ${f(mz ? mz.relative_location_ue[1] : 0)}, ${f(mz ? mz.relative_location_ue[2] : 0)}}, ` +
    `{${mz ? mz.relative_rotation_ue.join(', ') : '0, 0, 0'}},\n` +
    `     ${q(ev.WP_Fire)}, ${q(ev.WP_Reload)}, ${q(ev.WP_Equip)}, ${q(ev.WP_PutDown)}, ${q(m.idle_animation)},\n` +
    `     ${q(dtName)}, ${q(dtRec.DeathString)}, ${q(dtRec.Suicide)}, ${q(icon)}, ${q(muzzleFx)}, ${q(tracerFx)},
` +
    `     ${f(pr.InitialSpeed * 0.01)}, ${f(pr.Damage)}, ${f(pr.DamageRadius * 0.01)}, ${q(pr.DamageType)}, ${pr.homing ? 'true' : 'false'}},`);
}
const out = `// GENERATED by tools/gameplay/gen_weapon_table.js - do not edit. Sources (read-only): AssetTools authored.db (weapon class
// CDO MultiplayerData, else PlayerData, over HmWeaponData / TnWeaponData defaults), mp_content/mp_weapons.json (mesh,
// event anims, effects, damage types, kill-feed icons), data/gameplay/weapon_sockets.json (cooked SkeletalMeshSockets).
// Fields: id, provider, display, WeaponType code, fire type, data source, data object,
//   damage, shots, clip, maxAmmo, initialReserve, interval, range m, falloff-near m, far modifier,
//   spread min/max/perShot/cooldown, fineAim spread, reload, equip, putDown, autoFire, heatMax,
//   mesh glTF, anim glTF, muzzle bone, muzzle loc UE, muzzle rot UE, anims fire/reload/equip/putdown/idle,
//   damage type, DeathString, Suicide, kill-feed icon, muzzle FX template, tracer FX template,
//   projectile speed m/s, projectile damage, damage radius m, projectile damage type, homing.
${rows.join('\n')}
`;
fs.writeFileSync('src/game/WeaponTable.inc', out);
console.log('rows', rows.length);
