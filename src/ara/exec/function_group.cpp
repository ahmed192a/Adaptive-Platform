/**
 * @file function_group.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of FunctionGroup class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include <iostream>
#include <filesystem>
#include "ara/exec/function_group.h"
#include <fstream>
using namespace ara::exec;
using namespace nlohmann;


std::variant<ara::exec::ExecErrc, FunctionGroup::CtorToken> FunctionGroup::Preconstruct(std::string const& metaModelIdentifier) noexcept
{
    std::variant<ara::exec::ExecErrc, FunctionGroup::CtorToken> token;
    token.emplace<0>(ara::exec::ExecErrc::kMetaModelError);
    // identifier format: "<manifest path>/<function group name>"
    auto const slash = metaModelIdentifier.find_last_of('/');
    if (slash == std::string::npos || slash + 1 >= metaModelIdentifier.size()) {
        return token;
    }
    std::string const fg_name = metaModelIdentifier.substr(slash + 1);
    std::string const path = metaModelIdentifier.substr(0, slash);
    try {
        std::ifstream ifs(path);
        if (!ifs.good()) {
            return token; // manifest not readable
        }
        json const manifest = json::parse(ifs, nullptr, /*allow_exceptions=*/true);
        auto const m = manifest.find("Machine_manifest");
        if (m == manifest.end() || !m->is_object()) return token;
        auto const g = m->find("Mode_declaration_group");
        if (g == m->end() || !g->is_array()) return token;
        for (auto const &group : *g) {
            auto const n = group.find("Function_group_name");
            if (n != group.end() && n->is_string() && n->get<std::string>() == fg_name) {
                token.emplace<1>(group);
                return token;
            }
        }
    } catch (...) {
        // malformed manifest: report as meta model error, never terminate
    }
    token.emplace<0>(ara::exec::ExecErrc::kMetaModelError);
    return token;
}

FunctionGroup::FunctionGroup(FunctionGroup::CtorToken &&token) noexcept
{
    FunctionGroup::CtorToken _token = std::move(token);
    try {
        mFunction_group_name = _token.at("Function_group_name").get<std::string>();
        for (auto const &mode_declaration : _token.at("Mode_declarations"))
        {
            mstates.push_back(mode_declaration.at("Mode").get<std::string>());
        }
    } catch (...) {
        // leave the group with whatever was parsed; never terminate
    }
}


void FunctionGroup::set_FGname(std::string const& fg_name)
{
    mFunction_group_name = fg_name;
}
void FunctionGroup::set_states(std::vector<std::string> const& states)
{
    mstates = states;
}
std::string FunctionGroup::get_FGname() const noexcept
{
    return mFunction_group_name;
}
std::vector<std::string> FunctionGroup::get_states() const noexcept
{
    return mstates;
}

bool FunctionGroup::operator==(FunctionGroup const &other) const noexcept
{
    if (other.mFunction_group_name == this->mFunction_group_name)
    {
        return true;
    }
    return false;
}

bool FunctionGroup::operator!=(FunctionGroup const &other) const noexcept
{
    if (this->mFunction_group_name == other.mFunction_group_name)
    {
        return false;
    }
    return true;
}
