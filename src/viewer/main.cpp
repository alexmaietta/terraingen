#include "viewer_app.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

int main(int argc, char** argv) {
    terraingen::viewer::ViewerOptions options;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            options.maxFrames = std::strtol(argv[++i], nullptr, 10);
        }
    }
    try {
        terraingen::viewer::ViewerApp app(options);
        app.Run();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "terraingen-viewer: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
