#include "app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <stdexcept>

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::fputs(terraingen::viewer::AppUsage().c_str(), stdout);
            return EXIT_SUCCESS;
        }
    }
    try {
        terraingen::viewer::App app(terraingen::viewer::ParseAppOptions(argc, argv));
        app.Run();
        return EXIT_SUCCESS;
    } catch (const std::invalid_argument& error) {
        std::fprintf(stderr, "terraingen-viewer: %s\n\n%s", error.what(),
                     terraingen::viewer::AppUsage().c_str());
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "terraingen-viewer: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
