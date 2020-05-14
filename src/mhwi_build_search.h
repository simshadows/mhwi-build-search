/*
 * File: mhwi_build_search.h
 * Author: <contact@simshadows.com>
 */

#include "core/core.h"
#include "database/database.h"
#include "support/support.h"

namespace MHWIBuildSearch
{


/****************************************************************************************
 * search_jsonparse: JSON Search Parameter Parsing
 ***************************************************************************************/


struct SearchParameters {
    bool allow_low_rank;
    bool allow_high_rank;
    bool allow_master_rank;
    bool health_regen_required;

    WeaponClass weapon_class;

    SkillSpec skill_spec;
};


SearchParameters read_file(const Database& db, const std::string& filepath);


/****************************************************************************************
 * search
 ***************************************************************************************/


void search_cmd(const std::string& search_parameters_path);


} // namespace
