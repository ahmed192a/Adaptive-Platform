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
    // metaModelIdentifier is the path to the manifest file and the last part of the path is the name of the function group
    // we need to get the function group name from the path and then search for it in the manifest file
    // if the function group is found, we need to return the function group as a CtorToken
    // if the function group is not found, we need to return ExecErrc::kMetaModelError
    std::string fg_name = metaModelIdentifier.substr(metaModelIdentifier.find_last_of("/") + 1);
    std::string path = metaModelIdentifier.substr(0, metaModelIdentifier.find_last_of("/"));
    std::variant<ara::exec::ExecErrc, FunctionGroup::CtorToken> token;
    std::ifstream ifs(path);
    json manifest_json_full;
    if (!ifs.good()) {
      std::cerr << "File does not exist." << std::endl;
      exit(1);
    }
    ifs >> manifest_json_full;

    json mode_declaration_groups = manifest_json_full["Machine_manifest"]["Mode_declaration_group"];


        // find fg_name in mode_declaration_groups
        for (auto &mode_declaration_group : mode_declaration_groups)
        {
            if (mode_declaration_group["Function_group_name"] == fg_name)
            {
                token.emplace<1>(mode_declaration_group);
                return token;
            }
            
        }
    
    token.emplace<0>(ara::exec::ExecErrc::kMetaModelError);
    return token;
}

FunctionGroup::FunctionGroup(FunctionGroup::CtorToken &&token) noexcept
{
    FunctionGroup::CtorToken _token = std::move(token);
    mFunction_group_name = _token["Function_group_name"];
    auto mode_declarations = _token["Mode_declarations"];
    for (auto const &mode_declaration : mode_declarations)
    {
        std::string mode = mode_declaration["Mode"];
        mstates.push_back(mode);
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
