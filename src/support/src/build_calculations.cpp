/*
 * File: build_calculations.cpp
 * Author: <contact@simshadows.com>
 */

#include <assert.h>
#include <stdexcept>
#include <cmath>

#include "../support.h"
#include "../../utils/utils.h"


namespace MHWIBuildSearch
{


// Weapon base raw multiplied by this number is the raw cap.
static constexpr unsigned int k_RAW_CAP = 2;


static constexpr double k_RAW_BLUNDER_MULTIPLIER = 0.75;


static EffectiveDamageValues calculate_edv(const unsigned int    weapon_raw, // True raw, not bloated raw.
                                           const int             weapon_aff,

                                           const EleStatVisibility weapon_elestat_visibility,
                                           const EleStatType       weapon_elestat_type,
                                           const unsigned int      weapon_elestat_value,

                                           const double          base_raw_multiplier,
                                           const double          frostcraft_raw_multiplier,
                                           const unsigned int    bludgeoner_added_raw,
                                           const unsigned int    added_raw,
                                           const int             added_aff,
                                           const double          raw_crit_dmg_multiplier,
                                           const SharpnessGauge& final_sharpness_gauge,

                                           const unsigned int    free_element_active_percentage ) {

    assert(weapon_raw > 0);
    assert(base_raw_multiplier > 0.0);
    assert(raw_crit_dmg_multiplier > 0.0);

    const int affinity = weapon_aff + added_aff;

    const double raw_crit_modifier = [&](){
        //double raw_crit_chance = std::clamp(((double) (weapon_aff + added_aff)) / 100, -1.0, 1.0); // Could be better
        double raw_crit_chance = ((double) affinity) / 100;
        if (raw_crit_chance < 0) {
            double raw_blunder_chance = (raw_crit_chance < -1.0) ? -1.0 : -raw_crit_chance;
            return (k_RAW_BLUNDER_MULTIPLIER * raw_blunder_chance) + (1 - raw_blunder_chance);
        } else {
            raw_crit_chance = (raw_crit_chance > 1.0) ? 1.0 : raw_crit_chance;
            return (raw_crit_dmg_multiplier * raw_crit_chance) + (1 - raw_crit_chance);
        }
    }();

    /*
     * Effective Raw
     */

    const double raw_sharpness_modifier = final_sharpness_gauge.get_raw_sharpness_modifier();

    const unsigned int raw_cap = weapon_raw * k_RAW_CAP;

    const double weapon_multiplied_raw = (weapon_raw + bludgeoner_added_raw) * base_raw_multiplier;
    const unsigned int precap_true_raw = std::round(weapon_multiplied_raw) + added_raw;
    const unsigned int postcap_true_raw = std::min(precap_true_raw, raw_cap);
    
    const double efr = postcap_true_raw * raw_crit_modifier * raw_sharpness_modifier * frostcraft_raw_multiplier;

    /*
     * Effective Element/Status
     */

    const double ele_sharpness_modifier = [&](){
        if ((weapon_elestat_visibility != EleStatVisibility::none)
                && elestattype_is_element(weapon_elestat_type)) {
            return final_sharpness_gauge.get_elemental_sharpness_modifier();
        } else {
            return 1.0; // We disable the modifier.
        }
    }();

    const unsigned int base_elestat_value = [&](){
        if (weapon_elestat_visibility == EleStatVisibility::hidden) {
            return (weapon_elestat_value * free_element_active_percentage) / 100;
        } else {
            assert((weapon_elestat_visibility == EleStatVisibility::open)
                   || ((weapon_elestat_visibility == EleStatVisibility::none) && (!weapon_elestat_value)));
            return weapon_elestat_value;
        }
    }();

    const double efes = base_elestat_value * ele_sharpness_modifier;
    const EleStatType elestat_type = weapon_elestat_type;

    return {affinity,
            final_sharpness_gauge,
            ((double) precap_true_raw / raw_cap),
            efr,

            efes,
            elestat_type };
}


EffectiveDamageValues calculate_edv_from_skills_lookup(const WeaponClass         weapon_class,
                                                       const WeaponContribution& wc,
                                                       const SkillMap&           full_skills,
                                                       const MiscBuffsEquips&    misc_buffs,
                                                       const SkillSpec&          skill_spec) {

    SkillContribution sc(full_skills, skill_spec, weapon_class, wc);
    return calculate_edv(wc.weapon_raw,
                         wc.weapon_aff,

                         wc.elestat_visibility,
                         wc.elestat_type,
                         wc.elestat_value,

                         sc.base_raw_multiplier * misc_buffs.get_base_raw_multiplier(),
                         sc.frostcraft_raw_multiplier,
                         sc.bludgeoner_added_raw,
                         sc.added_raw + misc_buffs.get_added_raw(),
                         sc.added_aff,
                         sc.raw_crit_dmg_multiplier,
                         sc.final_sharpness_gauge,

                         sc.free_element_active_percentage );
}


EffectiveDamageValues calculate_edv_from_gear_lookup(const WeaponInstance&  weapon,
                                                     const ArmourEquips&    armour,
                                                     const DecoEquips&      decos,
                                                     const MiscBuffsEquips& misc_buffs,
                                                     const SkillSpec&       skill_spec) {
    WeaponContribution wc = weapon.calculate_contribution();

    assert(decos.fits_in(armour, wc));

    SkillMap skills = armour.get_skills_without_set_bonuses();
    skills.merge_in(decos);
    if (wc.skill) skills.increment(wc.skill, 1);
    SetBonusMap set_bonuses = armour.get_set_bonuses();
    if (wc.set_bonus) set_bonuses.increment(wc.set_bonus, 1);
    skills.add_set_bonuses(set_bonuses);

    return calculate_edv_from_skills_lookup(weapon.weapon->weapon_class, wc, skills, misc_buffs, skill_spec);
}


std::string EffectiveDamageValues::get_humanreadable() const {
    return "EFR: " + std::to_string(this->efr)
           + "\nEFE/EFS: " + std::to_string(this->efes) + " " + elestattype_to_str(this->elestat_type)
           + "\nAffinity: " + std::to_string(this->affinity)
           + "\nSharpness Gauge: " + this->final_sharpness_gauge.get_humanreadable()
           + "\nPre-Raw Cap Ratio: " + std::to_string(this->pre_raw_cap_ratio * 100) + "%";
}


// TODO: Implement the special rounding function that implements special handling of values between
//       -1.0 and 1.0 to always round away from zero.
ModelCalculatedValues calculate_damage(const DamageModel& model,
                                       const EffectiveDamageValues& edv) {

    const double unrounded_raw_damage = (edv.efr / 100) * (double) model.raw_motion_value * ((double) model.hzv_raw / 100);

    const double unrounded_elestat_damage = model.elemental_modifier * [&](){
        if (edv.efes == 0) {
            return 0.0; // Zero EFE/EFS always means zero damage.
        } else {
            const double elemod_hzvperc_product = [&](){
                switch (edv.elestat_type) {
                    case EleStatType::fire:    return (double) model.hzv_fire    / 100;
                    case EleStatType::water:   return (double) model.hzv_water   / 100;
                    case EleStatType::thunder: return (double) model.hzv_thunder / 100;
                    case EleStatType::ice:     return (double) model.hzv_ice     / 100;
                    case EleStatType::dragon:  return (double) model.hzv_dragon  / 100;
                    default:
                        throw std::logic_error("Unexpected EleStatType value.");
                }
            }();
            return edv.efes * elemod_hzvperc_product;
        }
    }();

    assert(unrounded_raw_damage >= 0.0);
    assert(unrounded_elestat_damage >= 0.0);

    const double unrounded_total_damage = unrounded_raw_damage + unrounded_elestat_damage;
    const unsigned int actual_total_damage = std::round(unrounded_raw_damage) + std::round(unrounded_elestat_damage);

    return {unrounded_raw_damage,
            unrounded_elestat_damage,
            unrounded_total_damage,
            actual_total_damage };
}


std::string DamageModel::get_humanreadable() const {
    return "Raw Motion Value:   " + std::to_string(this->raw_motion_value)
           + "\nElemental Modifier: " + std::to_string(this->elemental_modifier)
           + "\n"
           + "\nRaw HZV:     " + std::to_string(this->hzv_raw)
           + "\nFire HZV:    " + std::to_string(this->hzv_fire)
           + "\nWater HZV:   " + std::to_string(this->hzv_water)
           + "\nThunder HZV: " + std::to_string(this->hzv_thunder)
           + "\nIce HZV:     " + std::to_string(this->hzv_ice)
           + "\nDragon HZV:  " + std::to_string(this->hzv_dragon);
}


std::string ModelCalculatedValues::get_humanreadable() const {
    return "Unrounded Raw Damage: " + std::to_string(this->unrounded_raw_damage)
           + "\nUnrounded Elemental/Status Damage: " + std::to_string(this->unrounded_elestat_damage)
           + "\nUnrounded Total Damage: " + std::to_string(this->unrounded_total_damage)
           + "\nActual Total Damage: " + std::to_string(this->actual_total_damage);
}


} // namespace

