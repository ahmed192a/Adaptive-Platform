/**
 * @file update_request_return_types.hpp
 * @author Flashing Adapter Graduation Project Team
 * @version 0.1
 * @date 2022-03-14
 *
 * @copyright Copyright (c) 2022
 *
 */

#ifndef RETURN_TYPES_H_
#define RETURN_TYPES_H_
#include "ara/sm/sm_error_domain.h"
#include <variant>
#include <vector>
namespace ara::sm::update_request {

// [SWS_SM_91019]
/*
 * Name        : FunctionGroupNameType
 * Kind        : STRING
 * Description : full qualified FunctionGroup shortName.
 */
using FunctionGroupNameType = std::string;

// [SWS_SM_91018]
/*
 * Name        : FunctionGroupListType
 * Kind        : VECTOR
 * Subelements : FunctionGroupNameType
 * Description : A list of FunctionGroups.
 */
using FunctionGroupListType = std::vector<FunctionGroupNameType>;

using ResetMachineOutput = std::variant<ara::sm::errorDomains, std::monostate>;
using StopUpdateSessionOutput =
    std::variant<ara::sm::errorDomains, std::monostate>;
using StartUpdateSessionOutput =
    std::variant<ara::sm::errorDomains, std::monostate>;
using PrepareUpdateOutput = std::variant<ara::sm::errorDomains, std::monostate>;
using VerifyUpdateOutput = std::variant<ara::sm::errorDomains, std::monostate>;
using PrepareRollbackOutput =
    std::variant<ara::sm::errorDomains, std::monostate>;
using RequestUpdateSessionOutput =
    std::variant<ara::sm::errorDomains, std::monostate>;

} // namespace ara::sm::update_request

#endif