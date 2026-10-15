/// `terraingen-examples`: lists the examples linked into it, runs one, or opens them in the GUI's
/// Examples panel when the build has the GUI.

#include "common/example.h"

#ifdef TERRAINGEN_EXAMPLES_GUI
#include "common/browser.h"
#endif

#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
#ifdef TERRAINGEN_EXAMPLES_GUI
    return terraingen::examples::Main(args, std::cout, std::cerr, terraingen::examples::RunWindow);
#else
    return terraingen::examples::Main(args, std::cout, std::cerr);
#endif
}
