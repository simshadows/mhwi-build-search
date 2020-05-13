/*
 * File: mhwi_build_search.cpp
 * Author: <contact@simshadows.com>
 */

#include <assert.h>

#include <iostream>
#include <cmath>
#include <vector>

#include "core/core.h"
#include "database/database.h"
#include "support/support.h"
#include "utils.h"


namespace MHWIBuildSearch
{


void run() {
    const Database db = Database::get_db();

    std::unordered_map<const Skill*, unsigned int> min_levels = {
        {db.weakness_exploit_ptr, 0},
        {db.agitator_ptr, 0},
    };
    std::unordered_map<const Skill*, unsigned int> forced_states;
    SkillSpec skill_spec(std::move(min_levels), std::move(forced_states));
    std::clog << std::endl << skill_spec.get_humanreadable() << std::endl << std::endl;

    /*
     * Using values for Royal Venus Blade with only one affinity augment and Elementless Jewel 2.
     */

    WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
    weapon.augments->set_augment(WeaponAugment::augment_lvl, 3);
    weapon.augments->set_augment(WeaponAugment::attack_increase, 1);
    weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_sharpness_6);
    weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_sharpness_5);
    weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_sharpness_5);

    ArmourEquips armour;
    armour.add(db.armour.at("Raging Brachy",
                            Tier::master_rank,
                            ArmourVariant::master_rank_beta_plus,
                            ArmourSlot::head));
    armour.add(db.armour.at("Teostra",
                            Tier::master_rank,
                            ArmourVariant::master_rank_beta_plus,
                            ArmourSlot::arms));
    armour.add(db.charms.at("CHALLENGER_CHARM"));
    
    std::clog << weapon.get_humanreadable() << std::endl << std::endl;
    std::clog << armour.get_humanreadable() << std::endl << std::endl;
    std::clog << armour.get_skills_without_set_bonuses().get_humanreadable() << std::endl << std::endl;

    double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
    std::clog << efr << std::endl;

    /*
     * For testing purposes, we'll also do a by-skill lookup.
     */

    weapon = WeaponInstance(db.weapons.at("ROYAL_VENUS_BLADE"));

    SkillMap skills;
    skills.set_lvl(db.critical_boost_ptr, 3);
    skills.set_lvl(db.non_elemental_boost_ptr, 1);
    //skills.set_lvl(db.true_element_acceleration_ptr, 1);

    WeaponContribution wc = weapon.calculate_contribution(db);
    efr = calculate_efr_from_skills_lookup(db, wc, skills, skill_spec);
    std::clog << efr << std::endl;
    //assert(Utils::round_2decpl(efr) == 437.85); // Quick test!
}


} // namespace


int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    MHWIBuildSearch::run();

    return 0;
}

