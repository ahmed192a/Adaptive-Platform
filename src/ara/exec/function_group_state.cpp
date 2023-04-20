/**
 * @file function_group_state.cpp
 * @author Flashing Adapter Graduation Project Team
 * @brief Implementation of FunctionGroupState class
 * @version 0.1
 * @date 2022-03-07
 *
 * @copyright Copyright (c) 2022
 *
 */
#include "ara/exec/function_group_state.h"
#include <string>

using namespace ara::exec;
namespace ara
{
    namespace exec
    {

        std::variant<ara::exec::ExecErrc, FunctionGroupState::CtorToken> FunctionGroupState::Preconstruct(FunctionGroup const &functionGroup, std::string const& metaModelIdentifier) noexcept
        {
            std::variant<ara::exec::ExecErrc, FunctionGroupState::CtorToken> token;
            unsigned long sl = metaModelIdentifier.find('/');
            if (functionGroup.get_FGname() != metaModelIdentifier.substr(0, sl))
            {
                token.emplace<0>(ara::exec::ExecErrc::kMetaModelError);
                return token;
            }
            FunctionGroupState::CtorToken ctoken;
            ctoken.fg_name = functionGroup.get_FGname();
            ctoken.c_state = metaModelIdentifier.substr(sl + 1, metaModelIdentifier.size() - sl - 1);
            token.emplace<1>(ctoken);
            return token;
        }


        FunctionGroupState::FunctionGroupState(FunctionGroupState::CtorToken &&token) noexcept : mCtorToken(std::move(token))
        {
        }





        void FunctionGroupState::set_FGname(std::string const& fg_name)
        {
            mCtorToken.fg_name = fg_name;
        }


        void FunctionGroupState::set_states(std::string const& state)
        {
            mCtorToken.c_state = state;
        }


        std::string FunctionGroupState::get_FGname() const noexcept
        {
            return mCtorToken.fg_name;
        }

        std::string FunctionGroupState::get_states() const noexcept
        {
            return mCtorToken.c_state;
        }


        bool FunctionGroupState::operator==(FunctionGroupState const &other) const noexcept
        {
            if (this->mCtorToken.fg_name == other.mCtorToken.fg_name)
            {
                return true;
            }
            return false;
        }


        bool FunctionGroupState::operator!=(FunctionGroupState const &other) const noexcept
        {
            if (this->mCtorToken.fg_name == other.mCtorToken.fg_name)
            {
                return false;
            }
            return true;
        }
    } // namespace exec

} // namespace ara
