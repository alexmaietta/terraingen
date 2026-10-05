#include "cli.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return terraingen::cli::Run(args, std::cout, std::cerr);
}
