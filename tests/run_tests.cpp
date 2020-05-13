/*
 * File: run_tests.cpp
 * Author: <contact@simshadows.com>
 *
 * Testing for MHWI Build Search.
 */

#define CATCH_CONFIG_MAIN
#include "../dependencies/catch-2-12-1/catch.hpp"

#include <unordered_map>

#include "../src/core/core.h"
#include "../src/database/database.h"
#include "../src/support/support.h"
#include "../src/utils.h"

namespace TestMHWIBuildSearch
{

using namespace MHWIBuildSearch;


// Convenience function to get a variant, purely for testing purposes.
static const std::unordered_map<std::string, ArmourVariant> vari = {
    {"L",  ArmourVariant::low_rank              },
    {"HA", ArmourVariant::high_rank_alpha       },
    {"HB", ArmourVariant::high_rank_beta        },
    {"HG", ArmourVariant::high_rank_gamma       },
    {"MA", ArmourVariant::master_rank_alpha_plus},
    {"MB", ArmourVariant::master_rank_beta_plus },
    {"MG", ArmourVariant::master_rank_gamma_plus},
};


// Convenience function, purely for testing purposes.
static ArmourEquips get_armour(std::string head_set,
                               std::string head_var,
                               std::string chest_set,
                               std::string chest_var,
                               std::string arms_set,
                               std::string arms_var,
                               std::string waist_set,
                               std::string waist_var,
                               std::string legs_set,
                               std::string legs_var,
                               std::string charm_id,
                               const Database& db) {
    ArmourEquips armour;
    if (head_set != "") {
        armour.add(db.armour.at(head_set,
                                armour_variant_to_tier(vari.at(head_var)),
                                vari.at(head_var),
                                ArmourSlot::head));
    }
    if (chest_set != "") {
        armour.add(db.armour.at(chest_set,
                                armour_variant_to_tier(vari.at(chest_var)),
                                vari.at(chest_var),
                                ArmourSlot::chest));
    }
    if (arms_set != "") {
        armour.add(db.armour.at(arms_set,
                                armour_variant_to_tier(vari.at(arms_var)),
                                vari.at(arms_var),
                                ArmourSlot::arms));
    }
    if (waist_set != "") {
        armour.add(db.armour.at(waist_set,
                                armour_variant_to_tier(vari.at(waist_var)),
                                vari.at(waist_var),
                                ArmourSlot::waist));
    }
    if (legs_set != "") {
        armour.add(db.armour.at(legs_set,
                                armour_variant_to_tier(vari.at(legs_var)),
                                vari.at(legs_var),
                                ArmourSlot::legs));
    }
    if (charm_id != "") {
        armour.add(db.charms.at(charm_id));
    }

    return armour;
}


// We set up a single global database, out of convenience.
static const Database db = Database::get_db();


TEST_CASE("Greatsword build EFR calculations, basic testing.") {

    std::unordered_map<const Skill*, unsigned int> min_levels = {
        {db.agitator_ptr, 0},
        {db.critical_eye_ptr, 0},
        {db.weakness_exploit_ptr, 0},
    };
    std::unordered_map<const Skill*, unsigned int> forced_states = {
        {db.weakness_exploit_ptr, 1},
    };
    SkillSpec skill_spec(std::move(min_levels), std::move(forced_states));

    SECTION("Safi Shattersplitter + 1 Armour") {

        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        ArmourEquips armour = get_armour("Teostra", "MB",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 390.31);
    }

    SECTION("Safi Shattersplitter + 3 Armour") {

        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        ArmourEquips armour = get_armour("Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "Teostra",       "MB",
                                         "", "",
                                         "", "",
                                         "",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 415.77);
    }

    SECTION("Safi Shattersplitter + 5 Armour") {
        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        ArmourEquips armour = get_armour("Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "Teostra",       "MB",
                                         "Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 435.11);
    }

    SECTION("Safi Shattersplitter + 5 Armour + Charm") {
        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        ArmourEquips armour = get_armour("Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "Teostra",       "MB",
                                         "Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "CHALLENGER_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 480.30);
    }

    SECTION("Safi Shattersplitter + Augments + 5 Armour + Charm") {
        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        weapon.augments->set_augment(WeaponAugment::augment_lvl, 3);
        weapon.augments->set_augment(WeaponAugment::affinity_increase, 1);
        weapon.augments->set_augment(WeaponAugment::health_regen, 1);
        ArmourEquips armour = get_armour("Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "Teostra",       "MB",
                                         "Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "CHALLENGER_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 490.63);
    }

    SECTION("Safi Shattersplitter + Augments + Upgrades + 5 Armour + Charm") {
        WeaponInstance weapon(db.weapons.at("SAFI_SHATTERSPLITTER"));
        weapon.augments->set_augment(WeaponAugment::augment_lvl, 3);
        weapon.augments->set_augment(WeaponAugment::affinity_increase, 1);
        weapon.augments->set_augment(WeaponAugment::health_regen, 1);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_attack_6);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_attack_5);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_attack_5);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_attack_5);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_safi_sharpness_5);
        ArmourEquips armour = get_armour("Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "Teostra",       "MB",
                                         "Teostra",       "MB",
                                         "Raging Brachy", "MB",
                                         "CHALLENGER_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 554.90);
    }

    SECTION("Acid Shredder II + 1 Armour") {

        WeaponInstance weapon(db.weapons.at("ACID_SHREDDER_II"));
        ArmourEquips armour = get_armour("Teostra", "MB",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 375.15);
    }

    SECTION("Acid Shredder II + 1 Armour + Charm") {

        WeaponInstance weapon(db.weapons.at("ACID_SHREDDER_II"));
        ArmourEquips armour = get_armour("Teostra", "MB",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "", "",
                                         "ADAMANTINE_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 393.60);
    }

    SECTION("Acid Shredder II + Upgrades + 5 Armour + Charm") {

        WeaponInstance weapon(db.weapons.at("ACID_SHREDDER_II"));
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        ArmourEquips armour = get_armour("Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Yian Garuga",   "MB",
                                         "ADAMANTINE_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 469.64);
    }

    SECTION("Acid Shredder II + Augments + Upgrades5 Armour + Charm") {

        WeaponInstance weapon(db.weapons.at("ACID_SHREDDER_II"));
        weapon.augments->set_augment(WeaponAugment::augment_lvl, 3);
        weapon.augments->set_augment(WeaponAugment::affinity_increase, 2);
        weapon.augments->set_augment(WeaponAugment::attack_increase, 1);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_attack);
        weapon.upgrades->add_upgrade(WeaponUpgrade::ib_cust_affinity);
        ArmourEquips armour = get_armour("Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Raging Brachy", "MB",
                                         "Yian Garuga",   "MB",
                                         "ADAMANTINE_CHARM",
                                         db);

        double efr = calculate_efr_from_gear_lookup(db, weapon, armour, skill_spec);
        REQUIRE(Utils::round_2decpl(efr) == 492.35);
    }

}


} // namespace

