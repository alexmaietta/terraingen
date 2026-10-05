#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ernie::gui {

class Viewer;

/// A dockable window of the viewer. Subclasses draw with Dear ImGui in `Draw`, between the
/// window's `Begin` and `End`, which the viewer calls.
class Panel {
  public:
    explicit Panel(std::string title) : title_(std::move(title)) {}
    virtual ~Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    /// Title, also the panel's id in saved layouts.
    const std::string& Title() const {
        return title_;
    }

    bool IsVisible() const {
        return visible_;
    }

    void SetVisible(bool visible) {
        visible_ = visible;
    }

    /// Draws the panel's contents for the current frame.
    virtual void Draw(Viewer& viewer) = 0;

  private:
    std::string title_;
    bool visible_ = true;
};

/// A panel of simple widgets described by data rather than ImGui calls, so code without ImGui
/// bindings (Python) can build one. Each widget calls its callback when the user changes it;
/// `Set` changes a widget's value from code without calling it.
class WidgetPanel final : public Panel {
  public:
    using Id = std::uint32_t;

    explicit WidgetPanel(std::string title);
    ~WidgetPanel() override;

    Id Text(std::string text);
    Id Separator();
    Id Button(std::string label, std::function<void()> onClick);
    Id Checkbox(std::string label, bool value, std::function<void(bool)> onChange);
    Id Slider(std::string label, double min, double max, double value,
              std::function<void(double)> onChange);
    Id Combo(std::string label, std::vector<std::string> items, std::size_t selected,
             std::function<void(std::size_t)> onChange);

    /// A line plot of the values `source` returns, read each frame.
    Id Plot(std::string label, std::function<std::vector<float>()> source);

    /// Sets the value of a checkbox (`bool`), slider (`double`) or combo (index), or a text's text.
    void Set(Id widget, bool value);
    void Set(Id widget, double value);
    void Set(Id widget, std::size_t value);
    void Set(Id widget, std::string value);

    void Remove(Id widget);
    void Clear();

    void Draw(Viewer& viewer) override;

  private:
    struct Widget;
    std::vector<std::unique_ptr<Widget>> widgets_;
    Id next_ = 1;
};

}  // namespace ernie::gui
