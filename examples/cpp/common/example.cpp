#include "common/example.h"

#include <algorithm>
#include <array>
#include <exception>
#include <iomanip>
#include <map>
#include <string>
#include <utility>

namespace terraingen::examples {

namespace {

/// The categories in the order the examples teach them.
constexpr std::array<std::string_view, 11> kCategoryOrder = {
    "terrain", "craters", "distributions", "ages",    "boulders", "albedo",
    "layers",  "planets", "sources",       "queries", "config"};

std::size_t CategoryRank(std::string_view category) {
    const auto it = std::find(kCategoryOrder.begin(), kCategoryOrder.end(), category);
    return static_cast<std::size_t>(it - kCategoryOrder.begin());
}

std::map<std::string, ExampleInfo>& Registry() {
    static std::map<std::string, ExampleInfo> registry;
    return registry;
}

constexpr const char* kUsage =
    "Usage: terraingen-examples [NAME] [ARGS...] [OPTIONS]\n"
    "\n"
    "Runs a TerrainGen example, NAME such as craters_simple, or opens the examples in the GUI.\n"
    "\n"
    "Options:\n"
    "  --list             List the examples by category\n"
    "  --viewer KIND      'window' opens the GUI's Examples panel (the default when the build\n"
    "                     has the GUI); 'null' runs NAME without a viewer\n"
    "  --headless         Render the window hidden (no display needed)\n"
    "  --test             Run the example's checks\n"
    "  --output-dir DIR   Where outputs go (default ./output)\n"
    "  --save, --no-save  Write the outputs or not (saved without a viewer by default)\n"
    "  -h, --help         Show this help\n";

constexpr const char* kStandaloneUsage =
    " [ARGS...] [OPTIONS]\n"
    "\n"
    "Options:\n"
    "  --test             Run the example's checks\n"
    "  --output-dir DIR   Where outputs go (default ./output)\n"
    "  --no-save          Do not write the outputs\n"
    "  -h, --help         Show this help\n"
    "\n"
    "Open it in the GUI with: terraingen-examples ";

/// The parsed command line of `Main` and `StandaloneMain`.
struct CommandLine {
    ExampleArgs args;
    std::string viewer;
    bool headless = false;
    bool list = false;
    bool help = false;
    int save = -1;  ///< -1 for the default, else 0 or 1.
};

/// Parses `args`; `browser` accepts the options only `terraingen-examples` has.
CommandLine Parse(const std::vector<std::string>& args, bool browser) {
    CommandLine line;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        const auto value = [&]() -> const std::string& {
            if (i + 1 >= args.size()) {
                throw std::invalid_argument(arg + " needs a value");
            }
            return args[++i];
        };
        if (arg == "-h" || arg == "--help") {
            line.help = true;
        } else if (arg == "--test") {
            line.args.test = true;
        } else if (arg == "--output-dir") {
            line.args.outputDir = value();
        } else if (arg == "--no-save") {
            line.save = 0;
        } else if (browser && arg == "--save") {
            line.save = 1;
        } else if (browser && arg == "--list") {
            line.list = true;
        } else if (browser && arg == "--headless") {
            line.headless = true;
        } else if (browser && arg == "--viewer") {
            line.viewer = value();
            if (line.viewer != "window" && line.viewer != "null") {
                throw std::invalid_argument("--viewer must be window or null, not " + line.viewer);
            }
        } else if (arg.size() > 1 && arg[0] == '-') {
            throw std::invalid_argument("Unknown option " + arg);
        } else {
            line.args.positional.push_back(arg);
        }
    }
    return line;
}

void PrintList(std::ostream& out) {
    std::string category;
    for (const ExampleInfo* info : Examples()) {
        if (info->category != category) {
            out << (category.empty() ? "" : "\n") << info->category << ":\n";
            category = info->category;
        }
        out << "  " << std::left << std::setw(34) << info->name << info->description << "\n";
    }
}

}  // namespace

Example::Example(ExampleArgs args, std::ostream& out) : args_(std::move(args)), out_(&out) {}

Example::~Example() = default;

void Example::Save(const std::filesystem::path& /*dir*/) {}

const std::vector<ExampleView>& Example::Views() const {
    return views_;
}

const ExampleArgs& Example::Args() const {
    return args_;
}

std::ostream& Example::Out() {
    return *out_;
}

void Example::Show(std::string name, const Raster<float>& heights, const Raster<float>* albedo,
                   bool geographic) {
    views_.push_back(
        {std::move(name), heights, albedo != nullptr ? *albedo : Raster<float>(), geographic});
}

void Example::Check(bool condition, const std::string& what) {
    if (!condition) {
        throw CheckFailed("Check failed: " + what);
    }
}

std::string ExampleInfo::Program() const {
    return "example_" + name;
}

ExampleInfo
MakeInfo(std::string_view file, std::string description,
         std::function<std::unique_ptr<Example>(const ExampleArgs&, std::ostream&)> create) {
    const std::string stem = std::filesystem::path(file).stem().string();
    const std::string prefix = "example_";
    const std::size_t split = stem.find('_', prefix.size());
    if (stem.rfind(prefix, 0) != 0 || split == std::string::npos || split + 1 >= stem.size()) {
        throw std::invalid_argument("An example's file must be named "
                                    "example_<category>_<name>.cpp, not " +
                                    std::string(file));
    }
    ExampleInfo info;
    info.name = stem.substr(prefix.size());
    info.category = stem.substr(prefix.size(), split - prefix.size());
    info.description = std::move(description);
    info.create = std::move(create);
    return info;
}

bool RegisterExample(ExampleInfo info) {
    const std::string name = info.name;
    if (!Registry().emplace(name, std::move(info)).second) {
        throw std::invalid_argument("Two examples are named " + name);
    }
    return true;
}

std::vector<const ExampleInfo*> Examples() {
    std::vector<const ExampleInfo*> list;
    for (const auto& [name, info] : Registry()) {
        list.push_back(&info);
    }
    std::stable_sort(list.begin(), list.end(), [](const ExampleInfo* a, const ExampleInfo* b) {
        return CategoryRank(a->category) < CategoryRank(b->category) ||
               (CategoryRank(a->category) == CategoryRank(b->category) &&
                a->category < b->category);
    });
    return list;
}

const ExampleInfo* FindExample(std::string_view name) {
    if (name.rfind("example_", 0) == 0) {
        name.remove_prefix(8);
    }
    const auto it = Registry().find(std::string(name));
    return it != Registry().end() ? &it->second : nullptr;
}

std::filesystem::path ExamplesDir() {
    return TERRAINGEN_EXAMPLES_DIR;
}

int RunHeadless(const ExampleInfo& info, const ExampleArgs& args, bool save, std::ostream& out,
                std::ostream& err) {
    try {
        const std::unique_ptr<Example> example = info.create(args, out);
        example->Generate();
        if (save) {
            std::filesystem::create_directories(args.outputDir);
            example->Save(args.outputDir);
        }
        if (args.test) {
            example->Test();
            out << info.Program() << ": checks passed\n";
        }
    } catch (const std::exception& error) {
        err << info.Program() << ": " << error.what() << "\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

int Main(const std::vector<std::string>& args, std::ostream& out, std::ostream& err,
         const WindowRunner& window) {
    CommandLine line;
    try {
        line = Parse(args, true);
    } catch (const std::invalid_argument& error) {
        err << "terraingen-examples: " << error.what() << "\n\n" << kUsage;
        return kExitUsage;
    }
    if (line.help) {
        out << kUsage;
        return kExitSuccess;
    }
    if (line.list) {
        PrintList(out);
        return kExitSuccess;
    }
    const ExampleInfo* info = nullptr;
    if (!line.args.positional.empty()) {
        info = FindExample(line.args.positional.front());
        if (info == nullptr) {
            err << "terraingen-examples: no example named " << line.args.positional.front()
                << "; --list lists them\n";
            return kExitUsage;
        }
        line.args.positional.erase(line.args.positional.begin());
    }
    if (line.viewer.empty()) {
        line.viewer = window ? "window" : "null";
    }
    if (line.viewer == "window") {
        if (!window) {
            err << "terraingen-examples: this build has no GUI; configure with "
                   "-DTERRAINGEN_BUILD_GUI=ON and TERRAINGEN_ERNIE_DIR, or pass --viewer null\n";
            return kExitUsage;
        }
        return window(info, line.args, line.headless);
    }
    if (info == nullptr) {
        err << "terraingen-examples: --viewer null needs an example NAME\n\n" << kUsage;
        return kExitUsage;
    }
    return RunHeadless(*info, line.args, line.save != 0, out, err);
}

int StandaloneMain(const std::vector<std::string>& args, std::ostream& out, std::ostream& err) {
    const std::vector<const ExampleInfo*> examples = Examples();
    if (examples.size() != 1) {
        err << "An example's program must link exactly one example, not " << examples.size()
            << "\n";
        return kExitFailure;
    }
    const ExampleInfo& info = *examples.front();
    CommandLine line;
    try {
        line = Parse(args, false);
    } catch (const std::invalid_argument& error) {
        err << info.Program() << ": " << error.what() << "\n\nUsage: " << info.Program()
            << kStandaloneUsage << info.name << "\n";
        return kExitUsage;
    }
    if (line.help) {
        out << info.description << "\n\nUsage: " << info.Program() << kStandaloneUsage << info.name
            << "\n";
        return kExitSuccess;
    }
    return RunHeadless(info, line.args, line.save != 0, out, err);
}

}  // namespace terraingen::examples
