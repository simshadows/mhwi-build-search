/*
 * File: search_jsonparse.cpp
 * Author: <contact@simshadows.com>
 */

#include <assert.h>
#include <fstream>
#include <iostream>

#include "mhwi_build_search.h"
#include "database/database_miscbuffs.h"
#include "database/database_skills.h"
#include "utils/utils.h"

#include "../dependencies/json-3-7-3/json.hpp"


namespace MHWIBuildSearch
{


static SearchParameters read_json_obj(const nlohmann::json& j) {

    if (!j.is_object()) {
        throw std::runtime_error("Expected JSON object.");
    }

    const bool allow_low_rank        = j["allow_low_rank"];
    const bool allow_high_rank       = j["allow_high_rank"];
    const bool allow_master_rank     = j["allow_master_rank"];
    const bool health_regen_required = j["health_regen_required"];

    WeaponClass weapon_class = upper_snake_case_to_weaponclass(j["weapon_selection"]["class"]);
    std::unordered_set<EleStatType> allowed_weapon_elestat_types = [&](){
        const bool allow_fire    = j["weapon_selection"]["allow_fire"   ];
        const bool allow_water   = j["weapon_selection"]["allow_water"  ];
        const bool allow_thunder = j["weapon_selection"]["allow_thunder"];
        const bool allow_ice     = j["weapon_selection"]["allow_ice"    ];
        const bool allow_dragon  = j["weapon_selection"]["allow_dragon" ];

        std::unordered_set<EleStatType> x;
        if (allow_fire)    x.emplace(EleStatType::fire   );
        if (allow_water)   x.emplace(EleStatType::water  );
        if (allow_thunder) x.emplace(EleStatType::thunder);
        if (allow_ice)     x.emplace(EleStatType::ice    );
        if (allow_dragon)  x.emplace(EleStatType::dragon );
        return x;
    }();

    DamageModel damage_model = [&](){
        const unsigned int raw_mv         = j["damage_model"]["raw_mv"        ];
        const double       elemod_fire    = j["damage_model"]["elemod_fire"   ];
        const double       elemod_water   = j["damage_model"]["elemod_water"  ];
        const double       elemod_thunder = j["damage_model"]["elemod_thunder"];
        const double       elemod_ice     = j["damage_model"]["elemod_ice"    ];
        const double       elemod_dragon  = j["damage_model"]["elemod_dragon" ];

        const unsigned int hzv_raw     = j["damage_model"]["hzv_raw"    ];
        const unsigned int hzv_fire    = j["damage_model"]["hzv_fire"   ];
        const unsigned int hzv_water   = j["damage_model"]["hzv_water"  ];
        const unsigned int hzv_thunder = j["damage_model"]["hzv_thunder"];
        const unsigned int hzv_ice     = j["damage_model"]["hzv_ice"    ];
        const unsigned int hzv_dragon  = j["damage_model"]["hzv_dragon" ];

        DamageModel x = {raw_mv,
                         elemod_fire,
                         elemod_water,
                         elemod_thunder,
                         elemod_ice,
                         elemod_dragon,

                         hzv_raw,
                         hzv_fire,
                         hzv_water,
                         hzv_thunder,
                         hzv_ice,
                         hzv_dragon };
        return x;
    }();


    std::unordered_map<const Skill*, unsigned int> min_levels = [&](){
        std::unordered_map<const Skill*, unsigned int> ret;
        for (auto& e : j["selected_skills"].items()) {
            ret.insert({SkillsDatabase::get_skill(e.key()), e.value()});
        }
        return ret;
    }();

    std::unordered_map<const Skill*, unsigned int> states = [&](){
        std::unordered_map<const Skill*, unsigned int> ret;
        for (auto& e : j["forced_skill_states"].items()) {
            ret.insert({SkillsDatabase::get_skill(e.key()), e.value()});
        }
        return ret;
    }();

    std::unordered_set<const Skill*> force_remove_skill = [&](){
        std::unordered_set<const Skill*> ret;
        nlohmann::json j2 = j["force_remove_skills"];
        if (!j2.is_array()) {
            throw std::runtime_error("'force_remove_skills' must be an array of strings.");
        }
        std::vector<std::string> force_remove_skill_ids = j2;
        for (const std::string& skill_id : force_remove_skill_ids) {
            ret.emplace(SkillsDatabase::get_skill(skill_id));
        }
        return ret;
    }();

    SkillSpec skill_spec (std::move(min_levels),
                          std::move(states),
                          std::move(force_remove_skill) );
    // TODO: Just throw an exception from within the constructor instead.
    if (!skill_spec.data_is_valid()) {
        throw std::runtime_error("Invalid query.");
    }

    // Now, we determine miscellaneous buffs here.
    std::unordered_set<const MiscBuff*> miscbuffs;
    {
        nlohmann::json j2 = j["misc_buffs"];
        if (!j2.is_array()) {
            throw std::runtime_error("'misc_buffs' must be an array of strings.");
        }
        std::vector<std::string> miscbuff_ids = j2;
        for (const std::string& miscbuff_id : miscbuff_ids) {
            const MiscBuff& miscbuff = MiscBuffsDatabase::get_miscbuff(miscbuff_id);
            miscbuffs.emplace(&miscbuff);
        }
    }

    return {allow_low_rank,
            allow_high_rank,
            allow_master_rank,
            health_regen_required,
    
            weapon_class,
            std::move(allowed_weapon_elestat_types),

            std::move(damage_model),

            std::move(skill_spec),
            MiscBuffsEquips(std::move(miscbuffs)) };
}


SearchParameters read_file(const std::string& filepath) {
    nlohmann::json j;

    {
        std::ifstream f(filepath); // open file
        f >> j;
    } // close file

    return read_json_obj(j);
}


} // namespace

