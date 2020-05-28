#!/usr/bin/env python3
# -*- coding: ascii -*-

"""
Filename: regenerate_database_files.py
Author:   <contact@simshadows.com>

This codebase is dependent on the skills data defined in 'database_skills.json'.
"""

import os
import json

ENCODING = "utf-8"

SKILLS_JSON_PATH  = "./data/database_skills.json"
SKILLS_H_PATH = "./src/database/database_skills.h"
SKILLS_CPP_PATH = "./src/database/autogenerated/database_skills.cpp"

#########################################################################################

SKILLS_H_BASE = """
/*
 * This file is auto-generated.
 * Do not edit directly!
 */

#ifndef SKILLS_DATABASE_H
#define SKILLS_DATABASE_H

#include <string>
#include <vector>

#include "../core/core.h"

namespace SkillsDatabase
{{

using MHWIBuildSearch::Skill;
using MHWIBuildSearch::SetBonus;


{skill_declarations}

{setbonus_declarations}

const Skill* get_skill(const std::string& skill_id) noexcept;

const SetBonus* get_setbonus(const std::string& setbonus_id) noexcept;
std::vector<const SetBonus*> get_all_set_bonuses() noexcept;


}} // namespace

#endif // SKILLS_DATABASE_H

"""

#########################################################################################

SKILLS_CPP_BASE = """
/*
 * This file is auto-generated.
 * Do not edit directly!
 */

#include <unordered_map>

#include "../database_skills.h"

namespace SkillsDatabase
{{


{skill_definitions}


{setbonus_definitions}


static const std::unordered_map<std::string, const Skill*> g_skills_map = {{
{skill_map_elements}
}};


static const std::unordered_map<std::string, const SetBonus*> g_setbonus_map = {{
{setbonus_map_elements}
}};


const Skill* get_skill(const std::string& skill_id) noexcept {{
    return g_skills_map.at(skill_id);
}}


const SetBonus* get_setbonus(const std::string& setbonus_id) noexcept {{
    return g_setbonus_map.at(setbonus_id);
}}


std::vector<const SetBonus*> get_all_set_bonuses() noexcept {{
    std::vector<const SetBonus*> ret;
    for (const auto& e : g_setbonus_map) {{
        ret.emplace_back(e.second);
    }}
    return ret;
}}


}} // namespace

"""

#########################################################################################

def json_read(relfilepath):
    with open(relfilepath, encoding=ENCODING, mode="r") as f:
        return json.loads(f.read())

def file_write(relfilepath, *, data):
    with open(relfilepath, encoding=ENCODING, mode="w") as f:
        f.write(data)
    return

#########################################################################################

MINIMUM_SKILL_STATES = 2

def parse_skills(j):
    skills = {}
    for (skill_id, skill_json) in j.items():
        t = {
                # Name to bind globally in the C++ source code
                "identifier": "g_skill_" + skill_id.lower(),

                # Struct data
                "skill_id":     skill_id,
                "skill_name":   skill_json["name"],
                "normal_limit": skill_json["limit"],
                "secret_limit": skill_json.get("secret_limit", skill_json["limit"]),
                "states":       skill_json.get("states", MINIMUM_SKILL_STATES),
            }
        skills[t["skill_id"]] = t
    return skills

def parse_setbonuses(j):
    setbonuses = {}
    for (sb_id, sb_json) in j.items():
        t = {
                # Name to bind globally in the C++ source code
                "identifier": "g_setbonus_" + sb_id.lower(),

                # Struct data
                "sb_id": sb_id,
                "sb_name": sb_json["name"],
                "stages": [(x["parts"], x["skill"]) for x in sb_json["stages"]]
            }
        setbonuses[t["sb_id"]] = t
    return setbonuses

def generate_skills_source():
    j = json_read(SKILLS_JSON_PATH)

    skills = parse_skills(j["skills"])
    setbonuses = parse_setbonuses(j["set_bonuses"])

    skill_declarations = []
    skill_definitions  = []
    skill_map_elements = []

    setbonus_declarations = []
    setbonus_definitions  = []
    setbonus_map_elements = []

    for (_, skill) in skills.items():
        skill_declarations.append(
                    f"extern const Skill {skill['identifier']};"
                )
        skill_definitions.append(
                    f"const Skill {skill['identifier']} = {{\n"
                    f"    \"{skill['skill_id']}\", // id\n"
                    f"    \"{skill['skill_name']}\", // name\n"
                    f"    {skill['normal_limit']}, // normal_limit\n"
                    f"    {skill['secret_limit']}, // secret_limit\n"
                    f"    {skill['states']} // states\n"
                    f"}};"
                )
        skill_map_elements.append(
                    f"    {{ \"{skill['skill_id']}\", &{skill['identifier']} }},"
                )

    for (_, setbonus) in setbonuses.items():
        stages = []
        for (parts, skill_id) in setbonus["stages"]:
            stages.append(f"        {{ {parts}, &{skills[skill_id]['identifier']} }},")
        stages_str = "\n".join(stages)

        setbonus_declarations.append(
                    f"extern const SetBonus {setbonus['identifier']};"
                )
        setbonus_definitions.append(
                    f"const SetBonus {setbonus['identifier']} = {{\n"
                    f"    \"{setbonus['sb_id']}\", // id\n"
                    f"    \"{setbonus['sb_name']}\", // name\n"
                    f"    {{ // stages\n"
                    f"{stages_str}\n"
                    f"    }}\n"
                    f"}};"
                )
        setbonus_map_elements.append(
                    f"    {{ \"{setbonus['sb_id']}\", &{setbonus['identifier']} }},"
                )

    h_file_data = SKILLS_H_BASE.format(
            skill_declarations="\n".join(skill_declarations),
            setbonus_declarations="\n".join(setbonus_declarations),
        )
    file_write(SKILLS_H_PATH, data=h_file_data)

    cpp_file_data = SKILLS_CPP_BASE.format(
            skill_definitions="\n\n".join(skill_definitions),
            setbonus_definitions="\n\n".join(setbonus_definitions),
            skill_map_elements="\n".join(skill_map_elements),
            setbonus_map_elements="\n".join(setbonus_map_elements),
        )
    file_write(SKILLS_CPP_PATH, data=cpp_file_data)


generate_skills_source()

