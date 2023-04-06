/**
 * @file validator.h
 * @author your name (you@domain.com)
 * @brief 
 * @version 0.1
 * @date 2023-04-06
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#ifndef ARA_EXEC_PARSER_VALIDATOR_VALIDATOR_H_
#define ARA_EXEC_PARSER_VALIDATOR_VALIDATOR_H_

#include "nlohmann/json.hpp"

class IValidatable {
 public:
   virtual bool Validate() = 0;
    virtual ~IValidatable() = default;
};


class ExecutionValidator : public IValidatable {
 public:
    explicit ExecutionValidator(const nlohmann::json& input_json_data) : input_json_data_(input_json_data) {}

    bool Validate() override {
        // check if "Execution_manifest" is present in the json
        if (input_json_data_.find("Execution_manifest") == input_json_data_.end()) {
            return false;
        }
        return ValidateExecutionManifestId() && ValidateProcesses();
    }
    
 private:
   bool ValidateExecutionManifestId() const{
        // check if "Execution_manifest_id" is present in the json under "Execution_manifest"
        if (input_json_data_["Execution_manifest"].find("Execution_manifest_id") == input_json_data_["Execution_manifest"].end()) {
            return false;
        }
        return true;
   }
   bool ValidateProcesses(){
        // Validate that at least one process is present in the json
        if (input_json_data_["Execution_manifest"].find("Process") == input_json_data_["Execution_manifest"].end()) {
            return false;
        }
        nlohmann::json processes = input_json_data_["Execution_manifest"]["Process"];
        if (processes.empty()) {
            return false;
        }

        // Validate that each process has a name
        for (auto& process : processes) {
            if (process.find("Process_name") == process.end()) {
                return false;
            }
            // Validate that each process has at least one mode dependent startup config
            if (process.find("Mode_dependent_startup_configs") == process.end()) {
                return false;
            }
            nlohmann::json mode_dependent_startup_configs = process["Mode_dependent_startup_configs"];
            if (mode_dependent_startup_configs.empty()) {
                return false;
            }
            // Validate that each mode dependent startup config has at least one function group dependency
            for (auto& mode_dependent_startup_config : mode_dependent_startup_configs) {
                if (mode_dependent_startup_config.find("FunctionGroupDependencies") == mode_dependent_startup_config.end()) {
                    return false;
                }
                nlohmann::json function_group_dependencies = mode_dependent_startup_config["FunctionGroupDependencies"];
                if (function_group_dependencies.empty()) {
                    return false;
                }
                // Validate that each function group dependency has a function group name
                for (auto& function_group_dependency : function_group_dependencies) {
                    if (function_group_dependency.find("Function_group") == function_group_dependency.end()) {
                        return false;
                    }
                }
            }
        }
        return true;

   }
    
   nlohmann::json input_json_data_;
};


class MachineValidator : public IValidatable {
 public:
   explicit MachineValidator(const nlohmann::json& input_json_data) : input_json_data_(input_json_data) {}
    
   bool Validate() override {
       // check if "Machine_manifest" is present in the json
       if (input_json_data_.find("Machine_manifest") == input_json_data_.end()) {
           return false;
       }
       return ValidateMachineManifestId() && ValidateSystemFunctionGroups();
   }
    
 private:
   bool ValidateMachineManifestId(){
        // check if "Machine_manifest_id" is present in the json under "Machine_manifest"
        if (input_json_data_["Machine_manifest"].find("Machine_manifest_id") == input_json_data_["Machine_manifest"].end()) {
            return false;
        }
        return true;
   }
   bool ValidateSystemFunctionGroups(){
        // Validate that at least one system function group is present in the json
        if (input_json_data_["Machine_manifest"].find("Mode_declaration_group") == input_json_data_["Machine_manifest"].end()) {
            return false;
        }
        nlohmann::json system_function_groups = input_json_data_["Machine_manifest"]["Mode_declaration_group"];
        if (system_function_groups.empty()) {
            return false;
        }
        // Validate that each system function group has a name
        for (auto& system_function_group : system_function_groups) {
            if (system_function_group.find("Function_group_name") == system_function_group.end()) {
                return false;
            }
            // Validate that each system function group has at least one mode declaration
            if (system_function_group.find("Mode_declarations") == system_function_group.end()) {
                return false;
            }
            nlohmann::json mode_declarations = system_function_group["Mode_declarations"];
            if (mode_declarations.empty()) {
                return false;
            }
        }
        return true;
   }
    
   nlohmann::json input_json_data_;
};



#endif

