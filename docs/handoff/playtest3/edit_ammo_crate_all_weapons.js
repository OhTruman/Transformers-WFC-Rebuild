// Playtest 3 (the "pickup cube" = the placed Energon / ammo / overshield pickups): TnAmmoCratePickup's ValidWeaponTypes are
// WT_Primary / WT_Secondary / WT_Vehicle [CONF], but the crate refused unless the ACTIVE weapon's reserve was short and refilled
// only that weapon (a leftover from the one-weapon slice). Now it gives when any primary / heavy (WeaponType 0 / 1) inventory
// weapon or the vehicle weapon is short, and refills each of them to MaxAmmoCount (amount native: [PROV] full refill, as before).
// Run from the tree root.
const fs = require('fs');
const NL = String.fromCharCode(10), CR = String.fromCharCode(13);
function ed(f, pairs) {
    let s = fs.readFileSync(f, 'utf8'); const crlf = s.includes(CR + NL); s = s.split(CR + NL).join(NL);
    for (const [a, b] of pairs) {
        if (s.split(a).length !== 2) throw new Error(f + ': anchor not unique / missing: ' + a.slice(0, 90));
        s = s.replace(a, b);
    }
    fs.writeFileSync(f, crlf ? s.split(NL).join(CR + NL) : s);
}
ed('src/game/PickupFactory.cpp', [
    ['            // ValidWeaponTypes WT_Primary/WT_Secondary/WT_Vehicle [CONF]; amount native -> [PROV] refill the' + NL +
     '            // reserve to MaxAmmoCount. The slice\'s only weapon is the Ion Blaster (primary).' + NL +
     '            Weapon& w = c.weapon();' + NL +
     '            if (w.reserve >= w.reserveMax) return false;                 // AmmoMaxed' + NL +
     '            w.reserve = w.reserveMax;' + NL +
     '            return true;' + NL,
     '            // ValidWeaponTypes WT_Primary/WT_Secondary/WT_Vehicle [CONF]: every such weapon the pawn carries; amount native ->' + NL +
     '            // [PROV] refill each reserve to MaxAmmoCount. AmmoMaxed (no take) only when all of them are full.' + NL +
     '            bool any = false;' + NL +
     '            for (Weapon& w : c.inventoryMutable())' + NL +
     '                if (w.def && (w.def->typeCode == 0 || w.def->typeCode == 1) && w.reserve < w.reserveMax) { w.reserve = w.reserveMax; any = true; }' + NL +
     '            if (Weapon* vw = c.vehicleWeapon()) if (vw->reserve < vw->reserveMax) { vw->reserve = vw->reserveMax; any = true; }' + NL +
     '            return any;' + NL],
]);
console.log('ok');
