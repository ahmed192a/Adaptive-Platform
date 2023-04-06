/**
 * @file Parser.h
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2023-04-06
 *
 * @copyright Copyright (c) 2023
 *
 */
#ifndef ARA_EXEC_PARSER_PARSER_H_
#define ARA_EXEC_PARSER_PARSER_H_

#include "ara/exec/parser/manifest_parser_struct.h"
#include "ara/exec/parser/validator/validator.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

namespace ara::exec::parser {

class Parser {
private:
  std::string path_;
  nlohmann::json input_json_data;

public:
  explicit Parser(std::string const &path) : path_(path) {
    // check if path exists
    std::ifstream ifs(path);
    if (!ifs.good()) {
      std::cerr << "File does not exist." << std::endl;
      exit(1);
    }
    ifs >> this->input_json_data;
  }

  std::unique_ptr<ExecutionManifest> ParseExecutionManifest() {
    // - Validate the execution manifest file using the ExecutionValidator
    // class.
    // - Create an ExecutionManifest object and populate it with the parsed
    // data.
    // - Parse the execution manifest file using the JSON parser library.
    // - Handle any errors that may occur

    ExecutionValidator validator(input_json_data);
    if (!validator.Validate()) {
      return nullptr;
    }

    auto execution_manifest = std::make_unique<ExecutionManifest>();
    execution_manifest->manifest_id =
        input_json_data["Execution_manifest"]["Execution_manifest_id"];
    nlohmann::json processes = input_json_data["Execution_manifest"]["Process"];

    for (auto &process : processes) {
      Process proc{process};
      // std::cout << proc.name << std::endl;
      execution_manifest->processes.push_back(proc);
    }

    return execution_manifest;
  }

  std::unique_ptr<MachineManifest>
  ParseMachineManifest(std::map<std::string, ara::exec::parser::GLOB> &sys_FG) {
    // - Validate the machine manifest file using the MachineValidator class.
    // - Create a MachineManifest object and populate it with the parsed data.
    // - Parse the machine manifest file using the JSON parser library.
    // - Handle any errors that may occur

    MachineValidator validator(input_json_data);
    if (!validator.Validate()) {
      return nullptr;
    }

    auto machine_manifest = std::make_unique<MachineManifest>();
    machine_manifest->manifest_id =
        input_json_data["Machine_manifest"]["Machine_manifest_id"];

    nlohmann::json Mode_declaration_groups =
        input_json_data["Machine_manifest"]["Mode_declaration_group"];
    for (auto &mode_declaration_group : Mode_declaration_groups) {
      std::string FGName = mode_declaration_group["Function_group_name"];
      std::variant<ara::exec::ExecErrc, FunctionGroup::CtorToken>
          _functionGroup = FunctionGroup::Preconstruct(path_ + "/" + FGName);
      if (_functionGroup.index() == 0) {
        continue;
      }
      auto fg = std::make_shared<FunctionGroup>(
          std::move(std::get<1>(_functionGroup)));
      sys_FG[FGName].c_FG = fg;
      FunctionGroupState::CtorToken token = {fg->get_FGname(),
                                             fg->get_states()[0]};
      sys_FG[fg->get_FGname()].current_FGS =
          std::make_shared<FunctionGroupState>(std::move(token));
    }

    return machine_manifest;
  }
};

} // namespace ara::exec::parser

#endif // ARA_EXEC_PARSER_PARSER_H_
