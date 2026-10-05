#include "ernie/gui/panel.h"

#include <gtest/gtest.h>

#include <stdexcept>

namespace ernie::gui {
namespace {}  // namespace
}  // namespace ernie::gui

using namespace ernie;
using namespace ernie::gui;

TEST(WidgetPanelTest, WidgetsGetDistinctIds) {
    WidgetPanel panel("Controls");
    EXPECT_EQ(panel.Title(), "Controls");
    EXPECT_TRUE(panel.IsVisible());
    const auto text = panel.Text("hello");
    const auto separator = panel.Separator();
    const auto button = panel.Button("Go", [] {});
    const auto check = panel.Checkbox("On", true, [](bool) {});
    const auto slider = panel.Slider("Sun", 0.0, 90.0, 10.0, [](double) {});
    const auto combo = panel.Combo("Mode", {"a", "b"}, 1, [](std::size_t) {});
    const auto plot = panel.Plot("History", [] { return std::vector<float>{1.0f, 2.0f}; });
    const std::vector<WidgetPanel::Id> ids{text, separator, button, check, slider, combo, plot};
    for (std::size_t i = 0; i < ids.size(); ++i) {
        for (std::size_t j = i + 1; j < ids.size(); ++j) {
            EXPECT_NE(ids[i], ids[j]);
        }
    }
}

TEST(WidgetPanelTest, RejectsInvalidRangesAndSelections) {
    WidgetPanel panel("Controls");
    EXPECT_THROW(panel.Slider("Bad", 1.0, 1.0, 1.0, nullptr), std::invalid_argument);
    EXPECT_THROW(panel.Combo("Bad", {"a"}, 1, nullptr), std::invalid_argument);
    EXPECT_THROW(panel.Combo("Empty", {}, 0, nullptr), std::invalid_argument);
}

TEST(WidgetPanelTest, SetChecksTheWidgetsKind) {
    WidgetPanel panel("Controls");
    const auto check = panel.Checkbox("On", false, nullptr);
    const auto slider = panel.Slider("Sun", 0.0, 1.0, 0.5, nullptr);
    const auto combo = panel.Combo("Mode", {"a", "b"}, 0, nullptr);
    const auto text = panel.Text("t");
    EXPECT_NO_THROW(panel.Set(check, true));
    EXPECT_NO_THROW(panel.Set(slider, 2.0));
    EXPECT_NO_THROW(panel.Set(combo, std::size_t{1}));
    EXPECT_NO_THROW(panel.Set(text, std::string("u")));
    EXPECT_THROW(panel.Set(check, 1.0), std::invalid_argument);
    EXPECT_THROW(panel.Set(slider, true), std::invalid_argument);
    EXPECT_THROW(panel.Set(combo, std::size_t{2}), std::invalid_argument);
    EXPECT_THROW(panel.Set(text, true), std::invalid_argument);
    EXPECT_THROW(panel.Set(WidgetPanel::Id{999}, true), std::invalid_argument);
}

TEST(WidgetPanelTest, RemoveAndClearDropWidgets) {
    WidgetPanel panel("Controls");
    const auto check = panel.Checkbox("On", false, nullptr);
    const auto text = panel.Text("t");
    panel.Remove(check);
    EXPECT_THROW(panel.Set(check, true), std::invalid_argument);
    EXPECT_NO_THROW(panel.Set(text, std::string("u")));
    panel.Clear();
    EXPECT_THROW(panel.Set(text, std::string("v")), std::invalid_argument);
    EXPECT_NE(panel.Text("again"), text);
}

TEST(WidgetPanelTest, VisibilityToggles) {
    WidgetPanel panel("Controls");
    panel.SetVisible(false);
    EXPECT_FALSE(panel.IsVisible());
}
