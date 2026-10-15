/// The `main` of each example's own program, `example_<category>_<name>`: runs the one example
/// linked in.

#include "common/example.h"

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    return terraingen::examples::StandaloneMain(args, std::cout, std::cerr);
}
