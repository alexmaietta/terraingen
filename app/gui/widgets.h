// SPDX-License-Identifier: BSD-3-Clause

/// @file
/// @brief ImGui widgets the GUI's panels share.
#pragma once

#include <imgui.h>

#include <cstddef>
#include <string>

namespace terraingen::gui {

/// The ImGui callback that grows a `std::string` (the callback's user data) as its field's text
/// grows.
inline int ResizeString(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

/// An ImGui text field editing `text`, single line, or multiline of `size` when `size` is set.
inline bool InputString(const char* label, std::string& text, const ImVec2* size = nullptr) {
    const ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize;
    if (size != nullptr) {
        return ImGui::InputTextMultiline(label, text.data(), text.capacity() + 1, *size,
                                         flags | ImGuiInputTextFlags_AllowTabInput, ResizeString,
                                         &text);
    }
    return ImGui::InputText(label, text.data(), text.capacity() + 1, flags, ResizeString, &text);
}

}  // namespace terraingen::gui
