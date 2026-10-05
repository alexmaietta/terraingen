#include "ernie/gui/theme.h"

#include <imgui.h>

namespace ernie::gui {

namespace {

ImVec4 ToImVec4(const glm::vec4& colour) {
    return {colour.r, colour.g, colour.b, colour.a};
}

ImVec2 ToImVec2(const glm::vec2& size) {
    return {size.x, size.y};
}

/// `colour` with alpha `alpha`.
glm::vec4 WithAlpha(const glm::vec4& colour, float alpha) {
    return {colour.r, colour.g, colour.b, alpha};
}

}  // namespace

Theme DarkTheme() {
    return {};
}

Theme LightTheme() {
    Theme theme;
    theme.background = {0.851f, 0.855f, 0.863f, 1.0f};
    theme.panel = {0.922f, 0.925f, 0.929f, 1.0f};
    theme.header = {0.863f, 0.867f, 0.875f, 1.0f};
    theme.field = {0.980f, 0.980f, 0.984f, 1.0f};
    theme.fieldHovered = {1.0f, 1.0f, 1.0f, 1.0f};
    theme.separator = {0.769f, 0.773f, 0.784f, 1.0f};
    theme.text = {0.133f, 0.141f, 0.153f, 1.0f};
    theme.textMuted = {0.427f, 0.439f, 0.459f, 1.0f};
    theme.overlay = {0.922f, 0.925f, 0.929f, 0.80f};
    return theme;
}

void ApplyTheme(const Theme& theme, float contentScale, ImGuiStyle& style) {
    style = ImGuiStyle();
    style.WindowPadding = {8.0f, 6.0f};
    style.FramePadding = ToImVec2(theme.framePadding);
    style.ItemSpacing = ToImVec2(theme.itemSpacing);
    style.ItemInnerSpacing = {4.0f, 3.0f};
    style.CellPadding = {4.0f, 2.0f};
    style.IndentSpacing = 14.0f;
    style.ScrollbarSize = 10.0f;
    style.GrabMinSize = 8.0f;
    style.WindowRounding = 0.0f;
    style.ChildRounding = 0.0f;
    style.FrameRounding = theme.rounding;
    style.PopupRounding = theme.rounding;
    style.ScrollbarRounding = theme.rounding;
    style.GrabRounding = theme.rounding;
    style.TabRounding = theme.rounding;
    style.WindowBorderSize = 1.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabBorderSize = 0.0f;
    style.TabBarOverlineSize = 2.0f;
    style.DockingSeparatorSize = 1.0f;
    style.WindowMenuButtonPosition = ImGuiDir_None;
    style.WindowTitleAlign = {0.0f, 0.5f};
    style.SeparatorTextBorderSize = 1.0f;

    ImVec4* colours = style.Colors;
    const auto set = [&](ImGuiCol index, const glm::vec4& colour) {
        colours[index] = ToImVec4(colour);
    };
    const glm::vec4 accentDim = WithAlpha(theme.accent, 0.45f);
    const glm::vec4 accentFaint = WithAlpha(theme.accent, 0.25f);
    set(ImGuiCol_Text, theme.text);
    set(ImGuiCol_TextDisabled, theme.textMuted);
    set(ImGuiCol_WindowBg, theme.panel);
    set(ImGuiCol_ChildBg, WithAlpha(theme.panel, 0.0f));
    set(ImGuiCol_PopupBg, theme.panel);
    set(ImGuiCol_Border, theme.separator);
    set(ImGuiCol_BorderShadow, glm::vec4(0.0f));
    set(ImGuiCol_FrameBg, theme.field);
    set(ImGuiCol_FrameBgHovered, theme.fieldHovered);
    set(ImGuiCol_FrameBgActive, theme.fieldHovered);
    set(ImGuiCol_TitleBg, theme.background);
    set(ImGuiCol_TitleBgActive, theme.background);
    set(ImGuiCol_TitleBgCollapsed, theme.background);
    set(ImGuiCol_MenuBarBg, theme.background);
    set(ImGuiCol_ScrollbarBg, WithAlpha(theme.panel, 0.0f));
    set(ImGuiCol_ScrollbarGrab, theme.fieldHovered);
    set(ImGuiCol_ScrollbarGrabHovered, theme.textMuted);
    set(ImGuiCol_ScrollbarGrabActive, theme.accent);
    set(ImGuiCol_CheckMark, theme.accent);
    set(ImGuiCol_SliderGrab, theme.accent);
    set(ImGuiCol_SliderGrabActive, theme.accent);
    set(ImGuiCol_Button, theme.field);
    set(ImGuiCol_ButtonHovered, theme.fieldHovered);
    set(ImGuiCol_ButtonActive, accentDim);
    set(ImGuiCol_Header, theme.header);
    set(ImGuiCol_HeaderHovered, theme.fieldHovered);
    set(ImGuiCol_HeaderActive, accentDim);
    set(ImGuiCol_Separator, theme.separator);
    set(ImGuiCol_SeparatorHovered, accentDim);
    set(ImGuiCol_SeparatorActive, theme.accent);
    set(ImGuiCol_ResizeGrip, glm::vec4(0.0f));
    set(ImGuiCol_ResizeGripHovered, accentDim);
    set(ImGuiCol_ResizeGripActive, theme.accent);
    set(ImGuiCol_InputTextCursor, theme.text);
    set(ImGuiCol_Tab, theme.header);
    set(ImGuiCol_TabHovered, theme.fieldHovered);
    set(ImGuiCol_TabSelected, theme.panel);
    set(ImGuiCol_TabSelectedOverline, theme.accent);
    set(ImGuiCol_TabDimmed, theme.header);
    set(ImGuiCol_TabDimmedSelected, theme.panel);
    set(ImGuiCol_TabDimmedSelectedOverline, WithAlpha(theme.accent, 0.0f));
    set(ImGuiCol_DockingPreview, accentDim);
    set(ImGuiCol_DockingEmptyBg, theme.background);
    set(ImGuiCol_PlotLines, theme.accent);
    set(ImGuiCol_PlotLinesHovered, theme.text);
    set(ImGuiCol_PlotHistogram, theme.accent);
    set(ImGuiCol_PlotHistogramHovered, theme.text);
    set(ImGuiCol_TableHeaderBg, theme.header);
    set(ImGuiCol_TableBorderStrong, theme.separator);
    set(ImGuiCol_TableBorderLight, theme.separator);
    set(ImGuiCol_TableRowBg, glm::vec4(0.0f));
    set(ImGuiCol_TableRowBgAlt, WithAlpha(theme.text, 0.03f));
    set(ImGuiCol_TextLink, theme.accent);
    set(ImGuiCol_TextSelectedBg, accentFaint);
    set(ImGuiCol_TreeLines, theme.separator);
    set(ImGuiCol_DragDropTarget, theme.accent);
    set(ImGuiCol_DragDropTargetBg, accentFaint);
    set(ImGuiCol_UnsavedMarker, theme.warning);
    set(ImGuiCol_NavCursor, theme.accent);
    set(ImGuiCol_NavWindowingHighlight, theme.accent);
    set(ImGuiCol_NavWindowingDimBg, WithAlpha(theme.background, 0.6f));
    set(ImGuiCol_ModalWindowDimBg, WithAlpha(theme.background, 0.6f));

    style.ScaleAllSizes(contentScale);
    style.FontSizeBase = theme.fontSize;
    style.FontScaleDpi = contentScale;
}

}  // namespace ernie::gui
