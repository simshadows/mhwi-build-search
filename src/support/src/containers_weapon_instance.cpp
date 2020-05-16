/*
 * File: containers_weapon_instance.cpp
 * Author: <contact@simshadows.com>
 */

#include <assert.h>
#include <algorithm>

#include "../support.h"
#include "../../utils/utils.h"


namespace MHWIBuildSearch
{


WeaponInstance::WeaponInstance(const Weapon * const new_weapon) noexcept
    : weapon   (new_weapon)
    , augments (WeaponAugmentsInstance::get_instance(weapon))
    , upgrades (WeaponUpgradesInstance::get_instance(weapon))
{
}


WeaponContribution WeaponInstance::calculate_contribution(const Database& db) const {
    WeaponAugmentsContribution ac = this->augments->calculate_contribution();
    WeaponUpgradesContribution uc = this->upgrades->calculate_contribution();

    // OH MY GOD THIS IS SO UNSAFE.
    // TODO: FIX THIS OH GOD
    const SetBonus * const set_bonus = (uc.set_bonus_id == "") ? nullptr : db.skills.set_bonus_at(uc.set_bonus_id);

    WeaponContribution ret = {
        this->weapon->true_raw + ac.added_raw + uc.added_raw,
        this->weapon->affinity + ac.added_aff + uc.added_aff,

        this->weapon->is_raw,

        this->weapon->deco_slots,
        this->weapon->skill,
        set_bonus,

        uc.sharpness_gauge_override,
        this->weapon->is_constant_sharpness,

        ac.health_regen_active,
    };

    // Now, we finish off any bits we have remaining.
    if (ac.extra_deco_slot_size) {
        ret.deco_slots.emplace_back(ac.extra_deco_slot_size);
    }
    if (uc.extra_deco_slot_size) {
        ret.deco_slots.emplace_back(uc.extra_deco_slot_size);
    }

    return ret;
}


std::string WeaponInstance::get_humanreadable() const {
    return this->weapon->name
           + "\n\n" + this->upgrades->get_humanreadable()
           + "\n\n" + this->augments->get_humanreadable();
}


} // namespace

