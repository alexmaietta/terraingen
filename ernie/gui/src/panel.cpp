#include "ernie/gui/panel.h"

#include <imgui.h>
#include <implot.h>

#include <algorithm>
#include <stdexcept>
#include <variant>

namespace ernie::gui {

namespace {

struct TextWidget {
    std::string text;
};

struct SeparatorWidget {};

struct ButtonWidget {
    std::string label;
    std::function<void()> onClick;
};

struct CheckboxWidget {
    std::string label;
    bool value = false;
    std::function<void(bool)> onChange;
};

struct SliderWidget {
    std::string label;
    double min = 0.0;
    double max = 1.0;
    double value = 0.0;
    std::function<void(double)> onChange;
};

struct ComboWidget {
    std::string label;
    std::vector<std::string> items;
    std::size_t selected = 0;
    std::function<void(std::size_t)> onChange;
};

struct PlotWidget {
    std::string label;
    std::function<std::vector<float>()> source;
};

}  // namespace

struct WidgetPanel::Widget {
    Id id = 0;
    std::variant<TextWidget, SeparatorWidget, ButtonWidget, CheckboxWidget, SliderWidget,
                 ComboWidget, PlotWidget>
        data;
};

WidgetPanel::WidgetPanel(std::string title, Dock dock) : Panel(std::move(title), dock) {}

WidgetPanel::~WidgetPanel() = default;

namespace {

/// Calls a copy of `callback`, which stays alive if the callback removes its own widget.
template <typename F, typename... Args> void Call(const F& callback, Args... args) {
    const F copy = callback;
    copy(args...);
}

template <typename T, typename Widgets> T& Find(Widgets& widgets, std::uint32_t id) {
    for (auto& widget : widgets) {
        if (widget->id == id) {
            if (T* typed = std::get_if<T>(&widget->data)) {
                return *typed;
            }
            throw std::invalid_argument("Widget " + std::to_string(id) + " is of another kind");
        }
    }
    throw std::invalid_argument("No widget " + std::to_string(id));
}

}  // namespace

WidgetPanel::Id WidgetPanel::Text(std::string text) {
    widgets_.push_back(std::make_unique<Widget>(Widget{next_, TextWidget{std::move(text)}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Separator() {
    widgets_.push_back(std::make_unique<Widget>(Widget{next_, SeparatorWidget{}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Button(std::string label, std::function<void()> onClick) {
    widgets_.push_back(std::make_unique<Widget>(
        Widget{next_, ButtonWidget{std::move(label), std::move(onClick)}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Checkbox(std::string label, bool value,
                                      std::function<void(bool)> onChange) {
    widgets_.push_back(std::make_unique<Widget>(
        Widget{next_, CheckboxWidget{std::move(label), value, std::move(onChange)}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Slider(std::string label, double min, double max, double value,
                                    std::function<void(double)> onChange) {
    if (!(min < max)) {
        throw std::invalid_argument("Slider '" + label + "' needs min < max");
    }
    widgets_.push_back(std::make_unique<Widget>(
        Widget{next_, SliderWidget{std::move(label), min, max, std::clamp(value, min, max),
                                   std::move(onChange)}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Combo(std::string label, std::vector<std::string> items,
                                   std::size_t selected,
                                   std::function<void(std::size_t)> onChange) {
    if (selected >= items.size()) {
        throw std::invalid_argument("Combo '" + label + "' selects an item it does not have");
    }
    widgets_.push_back(std::make_unique<Widget>(Widget{
        next_, ComboWidget{std::move(label), std::move(items), selected, std::move(onChange)}}));
    return next_++;
}

WidgetPanel::Id WidgetPanel::Plot(std::string label, std::function<std::vector<float>()> source) {
    widgets_.push_back(
        std::make_unique<Widget>(Widget{next_, PlotWidget{std::move(label), std::move(source)}}));
    return next_++;
}

void WidgetPanel::Set(Id widget, bool value) {
    Find<CheckboxWidget>(widgets_, widget).value = value;
}

void WidgetPanel::Set(Id widget, double value) {
    SliderWidget& slider = Find<SliderWidget>(widgets_, widget);
    slider.value = std::clamp(value, slider.min, slider.max);
}

void WidgetPanel::Set(Id widget, std::size_t value) {
    ComboWidget& combo = Find<ComboWidget>(widgets_, widget);
    if (value >= combo.items.size()) {
        throw std::invalid_argument("Combo '" + combo.label + "' has no item " +
                                    std::to_string(value));
    }
    combo.selected = value;
}

void WidgetPanel::Set(Id widget, std::string value) {
    Find<TextWidget>(widgets_, widget).text = std::move(value);
}

void WidgetPanel::Remove(Id widget) {
    std::erase_if(widgets_, [widget](const auto& w) { return w->id == widget; });
}

void WidgetPanel::Clear() {
    widgets_.clear();
}

void WidgetPanel::Draw(Viewer& /*viewer*/) {
    std::vector<Id> ids;
    ids.reserve(widgets_.size());
    for (const auto& widget : widgets_) {
        ids.push_back(widget->id);
    }
    for (const Id id : ids) {
        const auto found = std::find_if(widgets_.begin(), widgets_.end(),
                                        [id](const auto& w) { return w->id == id; });
        if (found == widgets_.end()) {
            continue;
        }
        Widget& widget = **found;
        ImGui::PushID(static_cast<int>(id));
        std::visit(
            [](auto& w) {
                using T = std::decay_t<decltype(w)>;
                if constexpr (std::is_same_v<T, TextWidget>) {
                    ImGui::TextUnformatted(w.text.c_str());
                } else if constexpr (std::is_same_v<T, SeparatorWidget>) {
                    ImGui::Separator();
                } else if constexpr (std::is_same_v<T, ButtonWidget>) {
                    if (ImGui::Button(w.label.c_str()) && w.onClick) {
                        Call(w.onClick);
                    }
                } else if constexpr (std::is_same_v<T, CheckboxWidget>) {
                    if (ImGui::Checkbox(w.label.c_str(), &w.value) && w.onChange) {
                        Call(w.onChange, w.value);
                    }
                } else if constexpr (std::is_same_v<T, SliderWidget>) {
                    if (ImGui::SliderScalar(w.label.c_str(), ImGuiDataType_Double, &w.value, &w.min,
                                            &w.max) &&
                        w.onChange) {
                        Call(w.onChange, w.value);
                    }
                } else if constexpr (std::is_same_v<T, ComboWidget>) {
                    const char* preview = w.items[w.selected].c_str();
                    if (ImGui::BeginCombo(w.label.c_str(), preview)) {
                        for (std::size_t i = 0; i < w.items.size(); ++i) {
                            if (ImGui::Selectable(w.items[i].c_str(), i == w.selected) &&
                                i != w.selected) {
                                w.selected = i;
                                if (w.onChange) {
                                    Call(w.onChange, i);
                                }
                                break;
                            }
                        }
                        ImGui::EndCombo();
                    }
                } else if constexpr (std::is_same_v<T, PlotWidget>) {
                    const std::vector<float> values = w.source ? w.source() : std::vector<float>{};
                    if (ImPlot::BeginPlot(w.label.c_str(), ImVec2(-1.0f, 160.0f))) {
                        ImPlot::PlotLine(w.label.c_str(), values.data(),
                                         static_cast<int>(values.size()));
                        ImPlot::EndPlot();
                    }
                }
            },
            widget.data);
        ImGui::PopID();
    }
}

}  // namespace ernie::gui
