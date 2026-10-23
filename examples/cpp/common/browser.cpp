// SPDX-License-Identifier: BSD-3-Clause

#include "common/browser.h"

#include <ernie/app/icons.h>
#include <imgui.h>

#include <chrono>
#include <exception>
#include <filesystem>
#include <future>
#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::examples {

namespace {

constexpr const char* kExamplesPanel = "Examples";

/// GuiApp's configuration panels, hidden while the Examples panel shows code-built terrains.
constexpr const char* kConfigPanels[] = {"Terrain", "Inspector"};

/// An example generated off the GUI's thread, with the text it reported.
struct Generation {
    std::unique_ptr<std::ostringstream> out = std::make_unique<std::ostringstream>();
    std::unique_ptr<Example> example;
    std::string error;
    double seconds = 0.0;
};

}  // namespace

struct ExampleBrowser::State {
    gui::GuiApp* app = nullptr;
    ExampleArgs args;
    const ExampleInfo* current = nullptr;
    const ExampleInfo* pending = nullptr;
    std::future<Generation> running;
    Generation done;
    std::size_t view = 0;
    std::string status = "Pick an example";

    ernie::app::Viewer& Viewer() {
        return app->Viewer();
    }

    /// Sends what the example wrote to its stream since the last call to the log, line by line.
    void FlushText() {
        std::istringstream lines(done.out->str());
        for (std::string line; std::getline(lines, line);) {
            Viewer().Log(ernie::app::LogLevel::Info, line);
        }
        done.out->str("");
    }

    void Start(const ExampleInfo& info) {
        current = &info;
        done = Generation();
        view = 0;
        status = "Generating " + info.name + "...";
        Viewer().Log(ernie::app::LogLevel::Info, "Example " + info.name + ": " + info.description);
        const ExampleArgs exampleArgs = args;
        running = std::async(std::launch::async, [&info, exampleArgs] {
            Generation g;
            const auto start = std::chrono::steady_clock::now();
            try {
                g.example = info.create(exampleArgs, *g.out);
                g.example->Generate();
            } catch (const std::exception& error) {
                g.example.reset();
                g.error = error.what();
            }
            g.seconds =
                std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            return g;
        });
    }

    void Show(std::size_t index, bool frame) {
        const std::vector<ExampleView>& views = done.example->Views();
        if (index >= views.size()) {
            return;
        }
        view = index;
        const ExampleView& v = views[index];
        std::ostringstream text;
        text.precision(3);
        text << current->name << ": " << v.name << ", " << v.heights.Width() << " x "
             << v.heights.Height() << ", generated in " << done.seconds << " s";
        app->ShowHeights(v.heights, v.albedo.Empty() ? nullptr : &v.albedo, v.geographic,
                         text.str(), frame);
        status = text.str();
    }
};

ExampleBrowser::ExampleBrowser(gui::GuiApp& app, ExampleArgs args)
    : state_(std::make_unique<State>()) {
    state_->app = &app;
    state_->args = std::move(args);
    ernie::app::Viewer& viewer = app.Viewer();
    for (const char* title : kConfigPanels) {
        if (ernie::app::Panel* panel = viewer.FindPanel(title)) {
            panel->SetVisible(false);
        }
    }
    viewer.AddPanel(kExamplesPanel, ernie::app::Dock::Left,
                    [this](ernie::app::Viewer&) { Draw(); });
    viewer.FocusPanel(kExamplesPanel);
    viewer.OnFrame([this](ernie::app::Viewer&, const ernie::app::FrameInfo&) { Update(); });
}

ExampleBrowser::~ExampleBrowser() {
    if (state_->running.valid()) {
        state_->running.wait();
    }
}

void ExampleBrowser::Select(const ExampleInfo& info) {
    if (Busy()) {
        state_->pending = &info;
        state_->status = "Waiting for " + state_->current->name + " before " + info.name;
        return;
    }
    state_->Start(info);
}

bool ExampleBrowser::Busy() const {
    return state_->running.valid() || state_->pending != nullptr;
}

const ExampleInfo* ExampleBrowser::Current() const {
    return state_->current;
}

Example* ExampleBrowser::CurrentExample() {
    return state_->running.valid() ? nullptr : state_->done.example.get();
}

void ExampleBrowser::ShowView(std::size_t index) {
    if (CurrentExample() != nullptr) {
        state_->Show(index, false);
    }
}

std::size_t ExampleBrowser::ViewIndex() const {
    return state_->view;
}

bool ExampleBrowser::RunTest() {
    State& s = *state_;
    Example* example = CurrentExample();
    if (example == nullptr) {
        s.app->Report(ernie::app::LogLevel::Error, "No example to check");
        return false;
    }
    try {
        example->Test();
        s.FlushText();
        s.app->Report(ernie::app::LogLevel::Info, s.current->name + ": checks passed");
        s.status = s.current->name + ": checks passed";
        return true;
    } catch (const std::exception& error) {
        s.FlushText();
        s.app->Report(ernie::app::LogLevel::Error, s.current->name + ": " + error.what());
        s.status = s.current->name + ": " + error.what();
        return false;
    }
}

bool ExampleBrowser::SaveOutputs() {
    State& s = *state_;
    Example* example = CurrentExample();
    if (example == nullptr) {
        s.app->Report(ernie::app::LogLevel::Error, "No example to save");
        return false;
    }
    try {
        std::filesystem::create_directories(s.args.outputDir);
        example->Save(s.args.outputDir);
        s.FlushText();
        s.app->Report(ernie::app::LogLevel::Info,
                      "Saved " + s.current->name + " to " + s.args.outputDir.string());
        return true;
    } catch (const std::exception& error) {
        s.FlushText();
        s.app->Report(ernie::app::LogLevel::Error,
                      "Cannot save " + s.current->name + ": " + error.what());
        return false;
    }
}

const std::string& ExampleBrowser::Status() const {
    return state_->status;
}

void ExampleBrowser::Update() {
    State& s = *state_;
    if (s.running.valid() &&
        s.running.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        s.done = s.running.get();
        s.FlushText();
        if (!s.done.error.empty()) {
            s.app->Report(ernie::app::LogLevel::Error,
                          s.current->name + " failed: " + s.done.error);
            s.status = s.current->name + " failed: " + s.done.error;
        } else if (s.done.example->Views().empty()) {
            s.status = s.current->name + " has no terrain to show; its results are in the log";
            s.app->Report(ernie::app::LogLevel::Info, s.status);
        } else {
            s.Show(0, true);
        }
    }
    if (!s.running.valid() && s.pending != nullptr) {
        const ExampleInfo* next = s.pending;
        s.pending = nullptr;
        s.Start(*next);
    }
}

void ExampleBrowser::Draw() {
    State& s = *state_;
    const float detailsHeight = ImGui::GetTextLineHeightWithSpacing() * 9.0f;
    if (ImGui::BeginChild("##examples", ImVec2(0.0f, -detailsHeight), ImGuiChildFlags_Borders)) {
        std::string category;
        bool open = false;
        for (const ExampleInfo* info : Examples()) {
            if (info->category != category) {
                if (open) {
                    ImGui::TreePop();
                }
                category = info->category;
                if (s.current != nullptr && s.current->category == category) {
                    ImGui::SetNextItemOpen(true, ImGuiCond_Appearing);
                }
                open = ImGui::TreeNodeEx(category.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth);
            }
            if (!open) {
                continue;
            }
            const std::string label = info->name.substr(info->category.size() + 1);
            if (ImGui::Selectable((label + "##" + info->name).c_str(), info == s.current)) {
                Select(*info);
            }
            ImGui::SetItemTooltip("%s", info->description.c_str());
        }
        if (open) {
            ImGui::TreePop();
        }
    }
    ImGui::EndChild();

    if (s.current == nullptr) {
        return;
    }
    ImGui::TextUnformatted(s.current->Program().c_str());
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
    ImGui::TextWrapped("%s", s.current->description.c_str());
    ImGui::TextWrapped("%s", s.status.c_str());
    ImGui::PopStyleColor();

    Example* example = CurrentExample();
    if (example != nullptr && example->Views().size() > 1) {
        const std::vector<ExampleView>& views = example->Views();
        if (ImGui::BeginCombo("View", views[s.view].name.c_str())) {
            for (std::size_t i = 0; i < views.size(); ++i) {
                if (ImGui::Selectable(views[i].name.c_str(), i == s.view)) {
                    ShowView(i);
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::BeginDisabled(Busy());
    if (ImGui::Button((std::string(ernie::app::icons::kRotateCw) + " Run again").c_str())) {
        Select(*s.current);
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Generate the example again");
    ImGui::SameLine();
    ImGui::BeginDisabled(example == nullptr);
    if (ImGui::Button("Test")) {
        RunTest();
    }
    ImGui::SetItemTooltip("Run the example's checks");
    ImGui::SameLine();
    if (ImGui::Button((std::string(ernie::app::icons::kSave) + " Save").c_str())) {
        SaveOutputs();
    }
    ImGui::SetItemTooltip("Write the example's outputs to %s", s.args.outputDir.string().c_str());
    ImGui::EndDisabled();
}

int RunWindow(const ExampleInfo* info, const ExampleArgs& args, bool headless) {
    if (info == nullptr) {
        const std::vector<const ExampleInfo*> all = Examples();
        if (all.empty()) {
            std::cerr << "terraingen-examples: no examples are linked\n";
            return kExitFailure;
        }
        info = all.front();
    }
    try {
        gui::GuiAppOptions options;
        options.hidden = headless;
        options.title = "TerrainGen Examples";
        gui::GuiApp app(options);
        ExampleBrowser browser(app, args);
        browser.Select(*info);
        if (headless) {
            while (browser.Busy()) {
                app.Step();
            }
            app.Step();
        } else {
            app.Run();
        }
        if (browser.CurrentExample() == nullptr) {
            std::cerr << "terraingen-examples: " << browser.Status() << "\n";
            return kExitFailure;
        }
        if (args.test && !browser.RunTest()) {
            std::cerr << "terraingen-examples: " << browser.Status() << "\n";
            return kExitFailure;
        }
        if (args.test) {
            std::cout << browser.Current()->Program() << ": checks passed\n";
        }
    } catch (const std::exception& error) {
        std::cerr << "terraingen-examples: the GUI stopped: " << error.what() << "\n";
        return kExitFailure;
    }
    return kExitSuccess;
}

}  // namespace terraingen::examples
