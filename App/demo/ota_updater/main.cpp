// ota_updater - run a complete software update through the UCM PackageManagement rules and State Management.
#include "ota_updater.h"

#include <cstdlib>
#include <iostream>
#include <string>

static void Usage() {
  std::cout << "usage: ota_updater [--size BYTES] [--package FILE] [--tamper] [--reject-session]\n"
               "                   [--fail-verify] [--state-file PATH] [--quiet] [--json]\n"
               "exit codes: 0 updated, 2 rolled back, 3 package rejected, 4 session rejected, 1 error\n";
}

int main(int argc, char **argv) {
  ap::demo::UpdateOptions opt;
  bool json = false;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&](std::string &dst) {
      if (i + 1 >= argc) { Usage(); std::exit(1); }
      dst = argv[++i];
    };
    if (a == "--size") { std::string v; next(v); opt.packageSize = std::strtoull(v.c_str(), nullptr, 10); }
    else if (a == "--package") next(opt.packageFile);
    else if (a == "--state-file") next(opt.stateFile);
    else if (a == "--tamper") opt.tamper = true;
    else if (a == "--reject-session") opt.rejectSession = true;
    else if (a == "--fail-verify") opt.failVerify = true;
    else if (a == "--quiet") opt.quiet = true;
    else if (a == "--json") { json = true; opt.quiet = true; }
    else if (a == "--help" || a == "-h") { Usage(); return 0; }
    else { std::cerr << "unknown option " << a << "\n"; Usage(); return 1; }
  }
  auto rep = ap::demo::RunUpdate(opt, std::cout);
  if (json) {
    std::cout << "{\"outcome\":\"" << ap::demo::ToString(rep.outcome) << "\",\"ucmStatus\":\"" << rep.ucmStatus
              << "\",\"machineState\":\"" << rep.machineState << "\",\"blocks\":" << rep.blocks
              << ",\"ucmError\":" << rep.ucmError << "}\n";
  } else {
    std::cout << "outcome: " << ap::demo::ToString(rep.outcome) << " (exit " << ap::demo::ExitCode(rep) << ")\n";
  }
  return ap::demo::ExitCode(rep);
}
