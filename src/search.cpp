/*
 * File: search.cpp
 * Author: <contact@simshadows.com>
 */

//#include <cstdlib>
#include <assert.h>
#include <chrono>
#include <algorithm>
#include <iterator>
#include <iostream>
#include <tuple>

#include "mhwi_build_search.h"
#include "core/core.h"
#include "database/database.h"
#include "database/database_skills.h"
#include "support/support.h"
#include "utils/utils.h"
#include "utils/utils_strings.h"
#include "utils/logging.h"
#include "utils/pruning_vector.h"
#include "utils/counter.h"
#include "utils/counter_subset_seen_map.h"


namespace MHWIBuildSearch
{


struct SSBLimits {
    unsigned int operator()(const Skill * const k) const noexcept {
        return k->secret_limit;
    }
    unsigned int operator()(const SetBonus * const k) const noexcept {
        (void)k;
        return 5; // 1 for each armour piece. This is true since we don't consider the weapon.
    }
};


using SSBTuple = std::tuple<SkillMap, SetBonusMap>;


template<class StoredData>
using SSBSeenMapSmall = Utils::NaiveCounterSubsetSeenMap<StoredData, SkillMap, SetBonusMap>;
template<class StoredData>
using SSBSeenMap = Utils::BitTreeCounterSubsetSeenMap<StoredData, SSBLimits, SkillMap, SetBonusMap>;


struct ArmourPieceCombo {
    const ArmourPiece* armour_piece;
    DecoEquips decos;
};


struct ArmourSetCombo {
    ArmourEquips armour;
    DecoEquips decos;
};


struct WeaponInstanceExtended {
    WeaponInstance     instance;
    WeaponContribution contributions;
    double             ceiling_efr;
};



struct WeaponInstancePruneFn {
    // Return true if left can prune away right.
    // Left equalling right can also prune away right.
    bool operator()(const std::pair<WeaponInstance, WeaponContribution>& left,
                    const std::pair<WeaponInstance, WeaponContribution>& right ) const noexcept {
        const WeaponContribution& lc = left.second;
        const WeaponContribution& rc = right.second;

        if ((lc.weapon_raw < rc.weapon_raw) || (lc.weapon_aff < rc.weapon_aff)) {
            return false;
        }

        {
            assert(std::is_sorted(lc.deco_slots.begin(), lc.deco_slots.end(), std::greater<unsigned int>()));
            assert(std::is_sorted(rc.deco_slots.begin(), rc.deco_slots.end(), std::greater<unsigned int>()));

            // Left cannot replace right if left has less deco slots.
            if (lc.deco_slots.size() < rc.deco_slots.size()){
                return false;
            }

            // We now know left has at least the same number of deco slots as right.
            // Now, left cannot replace right if some some set of decos that fit in right
            // cannot fit in left.
            auto l_head = lc.deco_slots.begin();
            for (const unsigned int rv : rc.deco_slots) {
                const unsigned int lv = *l_head;
                if (lv < rv) {
                    return false;
                }
                ++l_head; // TODO: Increase safety?
            }
        }

        /*
         * We prune skills and set bonuses by this truth table:
         *
         *                | rc=None  | rc=setbonusA | rc=setbonusB
         *   -------------|----------|--------------|--------------
         *   lc=None      | continue | return False | return False
         *   lc=setbonusA | continue | continue     | return False
         *   lc=setbonusB | continue | return False | continue
         *   -------------|----------|--------------|--------------
         */
        if ((rc.set_bonus && (lc.set_bonus != rc.set_bonus)) || (rc.skill && (lc.skill != rc.skill))) {
            return false;
        }

        if (rc.health_regen_active && !lc.health_regen_active) {
            return false;
        }

        if ((!lc.is_constant_sharpness) && rc.is_constant_sharpness) {
            // TODO: Returning constant false is conservative. Try apply 0 handicraft?
            return false;
        } else {
            return SharpnessGauge::left_has_eq_or_more_hits(lc.maximum_sharpness, rc.maximum_sharpness);
        }
    }
};


static std::vector<WeaponInstanceExtended> prepare_weapons(const Database& db,
                                                           const SearchParameters& params,
                                                           const std::unordered_set<const SetBonus*>& set_bonus_subset) {

    auto start_t = std::chrono::steady_clock::now();

    std::vector<const Weapon*> weapons = db.weapons.get_all_of_weaponclass(params.weapon_class);
    Utils::log_stat("Weapons: ", weapons.size());
    assert(weapons.size());

    std::vector<std::pair<WeaponInstance, WeaponContribution>> unpruned;

    for (const Weapon * const weapon : weapons) {
        const auto augment_instances = WeaponAugmentsInstance::generate_maximized_instances(weapon);
        const auto upgrade_instances = WeaponUpgradesInstance::generate_maximized_instances(weapon);
        for (const std::shared_ptr<WeaponAugmentsInstance>& a : augment_instances) {
            for (const std::shared_ptr<WeaponUpgradesInstance>& u : upgrade_instances) {
                WeaponInstance new_inst = {weapon, a, u};
                WeaponContribution new_cont = new_inst.calculate_contribution();
                if (params.skill_spec.skill_must_be_removed(new_cont.skill)) {
                    continue;
                }
                if (params.health_regen_required && !new_cont.health_regen_active) {
                    continue;
                }
                if (!Utils::set_has_key(set_bonus_subset, new_cont.set_bonus)) {
                    new_cont.set_bonus = nullptr;
                }
                if (!params.skill_spec.is_in_subset(new_cont.skill)) {
                    new_cont.skill = nullptr;
                }
                unpruned.emplace_back(std::move(new_inst), std::move(new_cont));
            }
        }
    }
    const std::size_t stat_pre = unpruned.size();

    Utils::PruningVector<std::pair<WeaponInstance, WeaponContribution>, WeaponInstancePruneFn> pruned;

    //std::size_t i = 0;
    for (auto& e : unpruned) {
        //std::clog << i++ << "/" << std::to_string(stat_pre) << "\n";
        pruned.try_push_back(std::move(e));
    }

    SkillMap maximized_skills;
    for (const auto& e : params.skill_spec) {
        maximized_skills.set(e.first, e.first->secret_limit);
    }

    std::vector<WeaponInstanceExtended> ret;
    for (const auto& original : pruned.underlying()) {
        const EffectiveDamageValues edv = calculate_edv_from_skills_lookup(original.first.weapon->weapon_class,
                                                                           original.second,
                                                                           maximized_skills,
                                                                           params.misc_buffs,
                                                                           params.skill_spec);
        const double ceiling_efr = edv.efr;
        ret.push_back({std::move(original.first), std::move(original.second), ceiling_efr});
    }

    Utils::log_stat_reduction("Generated weapon augment+upgrade instances: ", stat_pre, ret.size());
    Utils::log_stat_duration("  >>> weapon augment+upgrade instances: ", start_t);
    Utils::log_stat();

    return ret;
}


static std::array<std::vector<const Decoration*>, k_MAX_DECO_SIZE> prepare_decos(const Database& db,
                                                                                 const SkillSpec& skill_spec) {
    std::vector<const Decoration*> all_decos = db.decos.get_all();

    std::unordered_map<const Skill*, const Decoration*> simplest_decos;
    std::vector<const Decoration*> decos_to_keep;

    // We first put decos that only contribute one level (within the skill spec) to the build in simplest_decos,
    // and any deco that contributes multiple levels to decos_to_keep.
    // (This isn't perfect since this allows all size-4 single-skill decos with skill levels 2 or greater to all be included,
    // regardless of the fact that we ideally want just the bigger deco.)
    // TODO: Make a better algorithm!
    for (const Decoration * const deco : all_decos) {
        const Skill* found_skill;
        std::size_t num_contribution = 0;

        bool dont_add = false;
        for (const auto& e : deco->skills) {
            if (skill_spec.skill_must_be_removed(e.first)) {
                dont_add = true;
                break;
            }
            if (skill_spec.is_in_subset(e.first)) {
                found_skill = e.first;
                num_contribution += e.second;
            }
        }
        if (dont_add) {
            continue;
        }

        if (num_contribution == 1) {
            if (Utils::map_has_key(simplest_decos, found_skill)
                    && (simplest_decos.at(found_skill)->slot_size < deco->slot_size)) {
                continue;
            }
            simplest_decos[found_skill] = deco;
        } else if (num_contribution > 1) {
            decos_to_keep.push_back(deco);
        }
    }
    // Now, we merge simplest_decos into decos_to_keep.
    for (const auto& e : simplest_decos) {
        decos_to_keep.push_back(e.second);
    }

    // Sort
    const auto cmp = [](const Decoration * const a, const Decoration * const b){
        return a->slot_size > b->slot_size;
    };
    std::sort(decos_to_keep.begin(), decos_to_keep.end(), cmp);

    Utils::log_stat_reduction("Decorations pruning: ", all_decos.size(), decos_to_keep.size());

    std::array<std::vector<const Decoration*>, k_MAX_DECO_SIZE> ret;
    for (const Decoration* deco : decos_to_keep) {
        assert(deco->slot_size >= 1);
        static_assert(k_MIN_DECO_SIZE == 1, "Assumption violation.");
        for (std::size_t i = deco->slot_size - 1; i < ret.size(); ++i) {
            ret[i].emplace_back(deco);
        }
    }
    return ret;
}


static std::vector<const Charm*> prepare_charms(const Database& db, const SkillSpec& skill_spec) {
    std::vector<const Charm*> ret = db.charms.get_all();

    const std::size_t stat_pre = ret.size();

    const auto pred = [&](const Charm* x){
        for (const Skill* s : x->skills) {
            if (skill_spec.skill_must_be_removed(s)) return true;
            if (skill_spec.is_in_subset(s)) return false;
        }
        return true;
    };
    ret.erase(std::remove_if(ret.begin(), ret.end(), pred), ret.end());

    const std::size_t stat_post = ret.size();

    Utils::log_stat_reduction("Charms filtering:    ", stat_pre, stat_post);

    return ret;
}


static std::map<ArmourSlot, std::vector<const ArmourPiece*>> prepare_armour(const Database& db,
                                                                            const SearchParameters& params) {
    std::map<ArmourSlot, std::vector<const ArmourPiece*>> ret = db.armour.get_all_pieces_by_slot();

    std::size_t head_pre  = ret.at(ArmourSlot::head).size();
    std::size_t chest_pre = ret.at(ArmourSlot::chest).size();
    std::size_t arms_pre  = ret.at(ArmourSlot::arms).size();
    std::size_t waist_pre = ret.at(ArmourSlot::waist).size();
    std::size_t legs_pre  = ret.at(ArmourSlot::legs).size();

    assert(ret.size() == 5);
    assert(head_pre);
    assert(chest_pre);
    assert(arms_pre);
    assert(arms_pre);
    assert(legs_pre);

    const auto pred = [&](const ArmourPiece* x){
        for (const auto& e : x->skills) {
            assert(e.second); // Non-zero level
            if (params.skill_spec.skill_must_be_removed(e.first)) return true;
        }

        switch (armour_variant_to_tier(x->variant)) {
            case Tier::low_rank:    return !params.allow_low_rank;
            case Tier::high_rank:   return !params.allow_high_rank;
            case Tier::master_rank: return !params.allow_master_rank;
            default:
                throw std::runtime_error("Invalid tier.");
        }
    };

    for (auto& e : ret) {
        auto& a = e.second;
        a.erase(std::remove_if(a.begin(), a.end(), pred), a.end());
    }

    Utils::log_stat_reduction("Head piece  - filtering by tier and removed skills: ",
                              head_pre, ret.at(ArmourSlot::head).size());
    Utils::log_stat_reduction("Chest piece - filtering by tier and removed skills: ",
                              chest_pre, ret.at(ArmourSlot::chest).size());
    Utils::log_stat_reduction("Arm piece   - filtering by tier and removed skills: ",
                              arms_pre, ret.at(ArmourSlot::arms).size());
    Utils::log_stat_reduction("Waist piece - filtering by tier and removed skills: ",
                              waist_pre, ret.at(ArmourSlot::waist).size());
    Utils::log_stat_reduction("Leg piece   - filtering by tier and removed skills: ",
                              legs_pre, ret.at(ArmourSlot::legs).size());

    return ret;
}


static std::vector<std::vector<const Decoration*>> generate_deco_combos(const std::vector<unsigned int>& deco_slots,
                                                                        const std::array<std::vector<const Decoration*>,
                                                                                         k_MAX_DECO_SIZE>& sorted_decos,
                                                                        const SkillSpec& skill_spec,
                                                                        const SkillMap& existing_skills) {
    assert(std::is_sorted(deco_slots.begin(), deco_slots.end(), std::greater<unsigned int>()));

    if (!deco_slots.size()) return {};
    // The rest of the algorithm will assume the presence of decoration slots.

    // We will "consume" deco slots from deco_slots by tracking the first "unconsumed" slot.
    using DecoSlotsHead = std::vector<unsigned int>::const_iterator;

    using WorkingCombo  = std::pair<std::vector<const Decoration*>, DecoSlotsHead>;
    using WorkingList   = std::vector<WorkingCombo>;
    using CompleteCombo = std::vector<const Decoration*>;
    using CompleteList  = std::vector<CompleteCombo>;

    WorkingList incomplete_combos = {{{}, deco_slots.begin()}}; // Start with a seed combo
    CompleteList complete_combos;

    const std::size_t sublist_index = deco_slots.front() - 1;
    assert(sublist_index < sorted_decos.size());
    const std::vector<const Decoration*>& deco_sublist = sorted_decos[sublist_index];

    for (const Decoration* deco : deco_sublist) {

        // We copy-initialize since we want to add zero of this deco to them anyway.
        WorkingList new_incomplete_combos = incomplete_combos;

        unsigned int max_to_add = 0;
        for (const auto& e : deco->skills) {
            if (skill_spec.is_in_subset(e.first)) {
                assert(e.first->secret_limit >= existing_skills.get(e.first));
                const unsigned int v = Utils::ceil_div(e.first->secret_limit - existing_skills.get(e.first), e.second);
                if (v > max_to_add) max_to_add = v;
            }
        }

        for (WorkingCombo combo : incomplete_combos) {
            assert(combo.second != deco_slots.end());

            // We skip 0 because we already copied all previous combos.
            std::vector<const Decoration*> curr_decos = combo.first;
            DecoSlotsHead curr_head = combo.second;
            for (unsigned int to_add = 1; to_add <= max_to_add; ++to_add) {
                if (*curr_head < deco->slot_size) break; // Stop adding. Deco can no longer fit.

                curr_decos.emplace_back(deco);
                ++curr_head;

                if (curr_head == deco_slots.end()) {
                    complete_combos.emplace_back(curr_decos);
                    break;
                } else {
                    new_incomplete_combos.emplace_back(curr_decos, curr_head);
                }
            }

        }

        incomplete_combos = std::move(new_incomplete_combos);
    }

    for (WorkingCombo& combo : incomplete_combos) {
        complete_combos.emplace_back(std::move(combo.first));
    }

    return complete_combos;
}


static SSBSeenMapSmall<ArmourPieceCombo> generate_slot_combos(const std::vector<const ArmourPiece*>& pieces,
                                                              const std::array<std::vector<const Decoration*>,
                                                                               k_MAX_DECO_SIZE>& sorted_decos,
                                                              const SkillSpec& skill_spec,
                                                              const std::unordered_set<const SetBonus*>& set_bonus_subset,
                                                              const std::string& debug_msg) {
    // We will assume decorations are sorted.
    // TODO: Add a runtime assert.

    SSBSeenMapSmall<ArmourPieceCombo> seen_set;

    unsigned long long stat_pre = 0;

    for (const ArmourPiece* piece : pieces) {

        SkillMap armour_skills;
        armour_skills.add_skills_filtered(*piece, skill_spec);

        std::vector<std::vector<const Decoration*>> deco_combos = generate_deco_combos(piece->deco_slots,
                                                                                       sorted_decos,
                                                                                       skill_spec,
                                                                                       armour_skills);

        stat_pre += deco_combos.size(); // TODO: How do I know this won't overflow?

        for (std::vector<const Decoration*>& decos : deco_combos) {

            SSBTuple ssb = {armour_skills, {}};

            std::get<0>(ssb).add_skills_filtered(decos, skill_spec);
            if (Utils::set_has_key(set_bonus_subset, piece->set_bonus)) {
                std::get<1>(ssb).set(piece->set_bonus, 1);
            }

            seen_set.add({piece, std::move(decos)}, std::move(ssb));
        }
    }

    Utils::log_stat_reduction(debug_msg, stat_pre, seen_set.size());

    return seen_set;
}


// TODO: I need a better name lmao
static std::vector<const Skill*> get_skills_in_subset_servable_without_sb_or_weapons(const Database& db,
                                                                                     const SkillSpec& skill_spec) {
    const std::unordered_set<const Skill*> possible_skills = [&db](){
        // Initial set is just armour skills
        std::unordered_set<const Skill*> x = db.armour.all_possible_skills_from_armour_without_set_bonuses();
        // Now we get the other skills
        std::unordered_set<const Skill*> deco_skills = db.decos.all_possible_skills_from_decos();
        std::unordered_set<const Skill*> charm_skills = db.charms.all_possible_skills_from_charms();
        // And now, we merge them in.
        x.insert(deco_skills.begin(), deco_skills.end());
        x.insert(charm_skills.begin(), charm_skills.end());
        return x;
    }();

    std::vector<const Skill*> ret = skill_spec.get_skill_subset_as_vector();

    const auto pred = [&possible_skills](const Skill* x){
        return !Utils::set_has_key(possible_skills, x);
    };
    ret.erase(std::remove_if(ret.begin(), ret.end(), pred), ret.end());

    return ret;
}


static void merge_in_armour_list(SSBSeenMap<ArmourSetCombo>& armour_combos,
                                 const SSBSeenMapSmall<ArmourPieceCombo>& piece_combos,
                                 const SkillSpec& skill_spec) {
    auto prev_armour_combos = armour_combos.get_data_as_vector();

    for (const auto& e1 : prev_armour_combos) {
        const SSBTuple&       set_combo_ssb = e1.first;
        const ArmourSetCombo& set_combo     = e1.second;

        assert(std::get<0>(set_combo_ssb).only_contains_skills_in_spec(skill_spec));(void)skill_spec;

        for (const auto& e2 : piece_combos) {
            const SSBTuple&         piece_combo_ssb = e2.first;
            const ArmourPieceCombo& piece_combo     = e2.second;

            ArmourSetCombo set_combo_to_add = [&](){
                ArmourSetCombo x = set_combo;
                x.armour.add(piece_combo.armour_piece);
                x.decos.merge_in(piece_combo.decos);
                return x;
            }();

            // We first need to check if the set bonuses exceed our limits.
            bool skip = false;
            const SetBonusMap tmp_setbonuses = set_combo_to_add.armour.get_set_bonuses();
            for (const auto& e : skill_spec.get_set_bonus_cutoffs()) {
                if (tmp_setbonuses.get(e.first) >= e.second) {
                    skip = true;
                    break;
                }
            }
            if (skip) {
                continue;
            }

            // Now, we may continue to add it!

            const auto op2 = [&](){
                SSBTuple x = set_combo_ssb;
                std::get<0>(x).merge_in(std::get<0>(piece_combo_ssb));
                std::get<1>(x).merge_in(std::get<1>(piece_combo_ssb));
                assert(std::get<0>(x).only_contains_skills_in_spec(skill_spec));(void)skill_spec;
                return x;
            };

            armour_combos.add(std::move(set_combo_to_add), op2());
        }
    }
}


static void refilter_weapons(std::vector<WeaponInstanceExtended>& weapons,
                             const double max_efr,
                             const std::size_t original_weapon_count) {
    const auto pred = [&](const WeaponInstanceExtended& x){
        return x.ceiling_efr <= max_efr;
    };
    weapons.erase(std::remove_if(weapons.begin(), weapons.end(), pred), weapons.end());

    Utils::log_stat_reduction("\n\nRepruned weapons with EFR " + std::to_string(max_efr) + ": ",
                              original_weapon_count,
                              weapons.size());
}


static void do_search(const Database& db, const SearchParameters& params) {

    auto total_start_t = std::chrono::steady_clock::now();

    std::clog << params.skill_spec.get_humanreadable() << "\n\n";

    const std::unordered_set<const SetBonus*> set_bonus_subset = [&](){
        std::unordered_set<const SetBonus*> x;

        for (const SetBonus * const set_bonus : SkillsDatabase::g_all_setbonuses) {
            for (const auto& e : set_bonus->stages) {
                if (params.skill_spec.is_in_subset(e.second)) {
                    x.emplace(set_bonus);
                    break;
                }
            }
        }

        if (x.size()) {
            std::clog << "Set bonuses to be considered:";
            for (const SetBonus * const set_bonus : x) {
                std::clog << std::endl << "  " << set_bonus->name;
            }
            std::clog << std::endl << std::endl;
        }

        return x;
    }();

    std::clog << "Buffs:\n" + Utils::indent(params.misc_buffs.get_humanreadable(), 2) + "\n\n";

    std::vector<WeaponInstanceExtended> weapons = prepare_weapons(db, params, set_bonus_subset);
    const std::size_t weapons_initial_size = weapons.size();
    assert(weapons_initial_size);

    auto start_t = std::chrono::steady_clock::now();

    std::array<std::vector<const Decoration*>, k_MAX_DECO_SIZE> grouped_sorted_decos = prepare_decos(db, params.skill_spec);
    assert(grouped_sorted_decos.size() == 4);
    assert(grouped_sorted_decos[0].size());
    assert(grouped_sorted_decos[1].size());
    assert(grouped_sorted_decos[2].size());
    assert(grouped_sorted_decos[3].size());

    std::vector<const Charm*> charms = prepare_charms(db, params.skill_spec);
    assert(charms.size());

    std::map<ArmourSlot, std::vector<const ArmourPiece*>> armour = prepare_armour(db, params);

    assert(armour.size() == 5);
    assert(armour.at(ArmourSlot::head).size());
    assert(armour.at(ArmourSlot::chest).size());
    assert(armour.at(ArmourSlot::arms).size());
    assert(armour.at(ArmourSlot::waist).size());
    assert(armour.at(ArmourSlot::legs).size());

    SSBSeenMapSmall<ArmourPieceCombo> head_combos = generate_slot_combos(armour.at(ArmourSlot::head),
                                                                         grouped_sorted_decos,
                                                                         params.skill_spec,
                                                                         set_bonus_subset,
                                                                         "Generated head+deco  combinations: ");
    SSBSeenMapSmall<ArmourPieceCombo> chest_combos = generate_slot_combos(armour.at(ArmourSlot::chest),
                                                                          grouped_sorted_decos,
                                                                          params.skill_spec,
                                                                          set_bonus_subset,
                                                                          "Generated chest+deco combinations: ");
    SSBSeenMapSmall<ArmourPieceCombo> arms_combos = generate_slot_combos(armour.at(ArmourSlot::arms),
                                                                         grouped_sorted_decos,
                                                                         params.skill_spec,
                                                                         set_bonus_subset,
                                                                         "Generated arms+deco  combinations: ");
    SSBSeenMapSmall<ArmourPieceCombo> waist_combos = generate_slot_combos(armour.at(ArmourSlot::waist),
                                                                          grouped_sorted_decos,
                                                                          params.skill_spec,
                                                                          set_bonus_subset,
                                                                          "Generated waist+deco combinations: ");
    SSBSeenMapSmall<ArmourPieceCombo> legs_combos = generate_slot_combos(armour.at(ArmourSlot::legs),
                                                                         grouped_sorted_decos,
                                                                         params.skill_spec,
                                                                         set_bonus_subset,
                                                                         "Generated legs+deco  combinations: ");
    Utils::log_stat_duration("  >>> decos, charms, and armour slot combos: ", start_t);
    Utils::log_stat();

    // We build the initial build list.
    
    start_t = std::chrono::steady_clock::now();
    SSBSeenMap<ArmourSetCombo> armour_combos = [&](){
        std::vector<const Skill*> sk_vec = get_skills_in_subset_servable_without_sb_or_weapons(db, params.skill_spec);
        Utils::log_stat("Skills to be considered by the combining seen set: ", sk_vec.size());
        std::unordered_set<const SetBonus*> sb_set;
        for (const auto& e : armour) {
            for (const ArmourPiece * const piece : e.second) {
                if (Utils::set_has_key(set_bonus_subset, piece->set_bonus)) {
                    sb_set.emplace(piece->set_bonus);
                }
            }
        }
        std::vector<const SetBonus*> sb_vec (sb_set.begin(), sb_set.end());
        Utils::log_stat("Set bonuses to be considered by the combining seen set: ", sb_vec.size());

        return SSBSeenMap<ArmourSetCombo>(std::move(sk_vec), std::move(sb_vec));
    }();
    Utils::log_stat_duration("  >>> Combining seen set initialization: ", start_t);
    std::clog << "\n";
    
    //SSBSeenMap<ArmourSetCombo> armour_combos;
    for (const Charm* charm : charms) {
        ArmourSetCombo combo;
        SSBTuple ssb;

        combo.armour.add(charm);
        std::get<0>(ssb).add_skills_filtered(*charm, charm->max_charm_lvl, params.skill_spec);
        assert(std::get<0>(ssb).only_contains_skills_in_spec(params.skill_spec));

        armour_combos.add(std::move(combo), std::move(ssb));
    }

    // And now, we merge in our slot combinations!

    start_t = std::chrono::steady_clock::now();
    unsigned long long stat_pre = armour_combos.size() * head_combos.size();
    //
    merge_in_armour_list(armour_combos, head_combos, params.skill_spec);
    //
    Utils::log_stat_reduction("Merged in head+deco  combinations: ", stat_pre, armour_combos.size());
    Utils::log_stat_duration("  >>> head combo merge: ", start_t);

    start_t = std::chrono::steady_clock::now();
    stat_pre = armour_combos.size() * chest_combos.size();
    //
    merge_in_armour_list(armour_combos, chest_combos, params.skill_spec);
    //
    Utils::log_stat_reduction("Merged in chest+deco combinations: ", stat_pre, armour_combos.size());
    Utils::log_stat_duration("  >>> chest combo merge: ", start_t);

    start_t = std::chrono::steady_clock::now();
    stat_pre = armour_combos.size() * arms_combos.size();
    //
    merge_in_armour_list(armour_combos, arms_combos, params.skill_spec);
    //
    Utils::log_stat_reduction("Merged in arms+deco  combinations: ", stat_pre, armour_combos.size());
    Utils::log_stat_duration("  >>> arms combo merge: ", start_t);

    start_t = std::chrono::steady_clock::now();
    stat_pre = armour_combos.size() * waist_combos.size();
    //
    merge_in_armour_list(armour_combos, waist_combos, params.skill_spec);
    //
    Utils::log_stat_reduction("Merged in waist+deco combinations: ", stat_pre, armour_combos.size());
    Utils::log_stat_duration("  >>> waist combo merge: ", start_t);

    start_t = std::chrono::steady_clock::now();
    stat_pre = armour_combos.size() * legs_combos.size();
    //
    merge_in_armour_list(armour_combos, legs_combos, params.skill_spec);
    //
    Utils::log_stat_reduction("Merged in legs+deco  combinations: ", stat_pre, armour_combos.size());
    Utils::log_stat_duration("  >>> legs combo merge: ", start_t);

    double best_efr = 0;

    std::size_t stat_wa_combos_explored = 0;
    std::size_t stat_wad_combos_explored = 0;
    start_t = std::chrono::steady_clock::now();

    for (const auto& e : armour_combos) {
        const SSBTuple&       ac_ssb = e.first;
        const ArmourSetCombo& ac     = e.second;

        bool reprune_weapons = false;
        for (const WeaponInstanceExtended& wc : weapons) {

            const SkillMap wac_skills = [&](){
                SkillMap x = std::get<0>(ac_ssb); // "Weapon-armour-combo"
                if (wc.contributions.skill) x.increment(wc.contributions.skill, 1);
                // TODO: Should we merge in set bonuses here?
                return x;
            }();

            const SetBonusMap wac_set_bonuses = [&](){
                SetBonusMap x = ac.armour.get_set_bonuses();
                if (wc.contributions.set_bonus) x.increment(wc.contributions.set_bonus, 1);
                return x;
            }();
            
            // Filter out anything that exceeds set bonus cutoffs.
            bool invalid_set_bonuses = false;
            for (const auto& e : params.skill_spec.get_set_bonus_cutoffs()) {
                assert(wac_set_bonuses.get(e.first) <= e.second);
                if (wac_set_bonuses.get(e.first) == e.second) {
                    invalid_set_bonuses = true;
                    break;
                }
            }
            if (invalid_set_bonuses) {
                continue;
            }

            std::vector<std::vector<const Decoration*>> w_decos = generate_deco_combos(wc.contributions.deco_slots,
                                                                                       grouped_sorted_decos,
                                                                                       params.skill_spec,
                                                                                       wac_skills);
            ++stat_wa_combos_explored;
            stat_wad_combos_explored += w_decos.size();

            for (std::vector<const Decoration*>& dc : w_decos) {

                const SkillMap skills = [&](){
                    SkillMap x = wac_skills;
                    x.add_set_bonuses(wac_set_bonuses);
                    x.merge_in(dc);
                    return x;
                }();

                // Filter out anything that doesn't meet minimum requirements
                if (!params.skill_spec.skills_meet_minimum_requirements(skills)) continue;
                assert((!params.health_regen_required) || wc.contributions.health_regen_active);

                const EffectiveDamageValues edv = calculate_edv_from_skills_lookup(wc.instance.weapon->weapon_class,
                                                                                   wc.contributions,
                                                                                   skills,
                                                                                   params.misc_buffs,
                                                                                   params.skill_spec);
                const double efr = edv.efr;

                if (efr > best_efr) {
                    best_efr = efr;

                    const DecoEquips curr_decos = [&](){
                        DecoEquips x = std::move(dc);
                        x.merge_in(ac.decos);
                        assert(x.fits_in(ac.armour, wc.contributions));
                        return x;
                    }();

                    const std::string build_info = wc.instance.weapon->name + "\n\n"
                                                   + wc.instance.upgrades->get_humanreadable() + "\n\n"
                                                   + wc.instance.augments->get_humanreadable() + "\n\n"
                                                   + "Armour:\n"
                                                   + Utils::indent(ac.armour.get_humanreadable(), 4) + "\n\n"
                                                   + "Decorations:\n"
                                                   + Utils::indent(curr_decos.get_humanreadable(), 4) + "\n\n"
                                                   + "Skills:\n"
                                                   + Utils::indent(skills.get_humanreadable(), 4) + "\n\n"
                                                   + "Buffs:\n"
                                                   + Utils::indent(params.misc_buffs.get_humanreadable(), 4) + "\n\n"
                                                   + "Effective Damage Values:\n"
                                                   + Utils::indent(edv.get_humanreadable(), 4);
                    std::clog << "\n\nFound EFR: " + std::to_string(best_efr) + "\n\n"
                              << Utils::indent(build_info, 4) + "\n";
                    reprune_weapons = true;
                }

            }

        }
        if (reprune_weapons) refilter_weapons(weapons, best_efr, weapons_initial_size);
    }

    Utils::log_stat_expansion("\nWeapon-armour --> +decos combinations explored: ",
                              stat_wa_combos_explored,
                              stat_wad_combos_explored);
    Utils::log_stat_duration("  >>> weapon combo merge: ", start_t);
    Utils::log_stat();

    std::clog << std::endl;
    Utils::log_stat_duration("Search execution time (before teardown): ", total_start_t);
    Utils::log_stat();
}


void search_cmd(const std::string& search_parameters_path) {

    const Database db = Database::get_db();
    const SearchParameters params = read_file(search_parameters_path);

    do_search(db, params);
}


} // namespace

