#pragma once

#include <glm/glm.hpp>

struct ImGuiStyle;

namespace ernie::gui {

/// Colours (sRGB-encoded with alpha, as written to the screen) and metrics of the viewer's UI. The
/// defaults are a dark, dense look: charcoal panels, one cool accent for selection and active
/// controls, flat headers and thin separators.
struct Theme {
    glm::vec4 background{0.106f, 0.110f, 0.118f, 1.0f};  ///< Behind the viewport and docks.
    glm::vec4 panel{0.149f, 0.157f, 0.169f, 1.0f};       ///< Panel bodies.
    glm::vec4 header{0.188f, 0.196f, 0.212f, 1.0f};      ///< Collapsing headers and tab bars.
    glm::vec4 field{0.196f, 0.208f, 0.227f, 1.0f};       ///< Input fields, combos, sliders.
    glm::vec4 fieldHovered{0.235f, 0.247f, 0.267f, 1.0f};
    glm::vec4 separator{0.078f, 0.082f, 0.086f, 1.0f};  ///< One-pixel lines between areas.
    glm::vec4 text{0.800f, 0.808f, 0.820f, 1.0f};
    glm::vec4 textMuted{0.502f, 0.514f, 0.533f, 1.0f};  ///< Labels and disabled text.
    glm::vec4 accent{0.204f, 0.659f, 0.859f, 1.0f};     ///< Selection, active tab, slider fill.
    glm::vec4 warning{0.910f, 0.698f, 0.235f, 1.0f};
    glm::vec4 error{0.886f, 0.341f, 0.318f, 1.0f};
    glm::vec4 overlay{0.078f, 0.082f, 0.086f, 0.70f};  ///< Viewport toolbar and HUD.

    float fontSize = 14.0f;  ///< Text size in pixels at a content scale of 1.
    float iconSize = 15.0f;  ///< Icon size in pixels at a content scale of 1.
    float rounding = 2.0f;   ///< Corner radius of frames, tabs and popups.
    glm::vec2 framePadding{6.0f, 3.0f};
    glm::vec2 itemSpacing{6.0f, 3.0f};
    float propertyLabelWidth = 120.0f;  ///< Width of the label column in the Property panel.
};

/// The default dark theme.
Theme DarkTheme();

/// A light theme with the same metrics and accent.
Theme LightTheme();

/// Resets `style` and writes `theme`'s colours and metrics into it, with sizes and fonts scaled by
/// `contentScale`.
void ApplyTheme(const Theme& theme, float contentScale, ImGuiStyle& style);

}  // namespace ernie::gui
