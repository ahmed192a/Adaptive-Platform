// uds_tester: runs the scripted UDS session against an in-process demo ECU, or lets you type raw requests.
//   uds_tester            scripted session, prints a readable trace (exit code 1 when a check fails)
//   uds_tester --repl     type hex requests ("22 F1 90"); "f " prefix = functional; "wait <ms>" advances the S3 clock
#include <iostream>
#include <string>

#include "uds_scenario.h"

int main(int argc, char **argv) {
  ap::demo::DemoEcu ecu;
  bool repl = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if (a == "--repl") repl = true;
    else if (a == "--help" || a == "-h") {
      std::cout << "usage: uds_tester [--repl]\n";
      return 0;
    } else {
      std::cerr << "unknown option " << a << "\n";
      return 2;
    }
  }
  if (!repl) return ap::demo::RunScenario(ecu, &std::cout).failed == 0 ? 0 : 1;

  std::cout << "demo ECU ready. Hex requests, e.g. '10 03', '22 F1 90'. 'f <hex>' = functional, 'wait <ms>', 'quit'.\n";
  std::string line;
  while (std::cout << "> " && std::getline(std::cin, line)) {
    if (line == "quit" || line == "exit") break;
    if (line.rfind("wait ", 0) == 0) {
      ecu.Advance(std::chrono::milliseconds(std::stoi(line.substr(5))));
      continue;
    }
    auto addr = ara::diag::Addressing::kPhysical;
    if (line.rfind("f ", 0) == 0) {
      addr = ara::diag::Addressing::kFunctional;
      line = line.substr(2);
    }
    auto req = ap::demo::ParseHex(line);
    if (!req) {
      std::cout << "  not a hex request\n";
      continue;
    }
    auto rsp = ecu.Server().Dispatch(*req, addr);
    std::cout << "  " << ap::demo::Describe(*req) << "\n";
    if (rsp) std::cout << "  RX " << ap::demo::HexString(*rsp) << "   " << ap::demo::Describe(*rsp) << "\n";
    else std::cout << "  (no response)\n";
  }
  return 0;
}
