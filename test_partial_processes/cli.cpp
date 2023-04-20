#include "CLI/App.hpp"
#include "CLI/Formatter.hpp"
#include "CLI/Config.hpp"
#include <iostream>

int main(int argc, char *argv[]) {
    int a_value = 0;
    int count_value = 0;

    CLI::App app{"MyProgram"};

    app.add_option("-a, --acount", a_value, "set a value");
    app.add_option("-c, --count", count_value, "set count value");

    CLI11_PARSE(app, argc, argv);

    std::cout << "a = " << a_value << ", count = " << count_value << std::endl;

    return 0;
}
