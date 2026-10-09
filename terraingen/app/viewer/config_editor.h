#pragma once

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace terraingen::viewer {

/// A tree editor for a terrain configuration held as JSON, its widgets chosen from the
/// configuration's schema (`ConfigSchema`): numbers, integers, checkboxes, text, menus of an
/// enum's choices, a type menu for each choice of types (surface, source, layers, models...),
/// optional fields switched on and off, and lists with Add, move and remove.
///
/// Fields the configuration leaves out show their defaults, dimmed, and are written only once
/// edited; right-clicking a field removes it, back to its default. So an edited configuration
/// stays as short as the one opened. Hovering a field shows its description and default, and
/// hovering a type or enum value in a menu shows what it means.
class ConfigEditor {
  public:
    using Json = nlohmann::json;

    ConfigEditor();

    const Json& Schema() const {
        return schema_;
    }

    /// The schema `node` stands for, following a reference to a definition.
    const Json& Resolve(const Json& node) const;

    /// A value for a new item of schema `node`: an object or choice with only its "type" (its
    /// first alternative's), an empty list, null for an optional, or the kind's zero.
    Json NewValue(const Json& node) const;

    /// Switches `value`, an object of choice `node`, to the alternative `type`, keeping the
    /// fields that alternative also has.
    void SetType(Json& value, const Json& node, const std::string& type) const;

    /// Draws `config`'s widgets inside an ImGui frame; returns whether the user changed it.
    bool Draw(Json& config);

    /// Opens, or closes, every node of the tree on the next `Draw`.
    void ExpandAll() {
        openAll_ = true;
    }

    void CollapseAll() {
        openAll_ = false;
    }

  private:
    /// Opens or closes the next tree node as `ExpandAll` or `CollapseAll` asked.
    void NextNodeOpen() const;
    bool DrawFields(Json& object, const Json& fields);
    bool DrawField(Json& object, const Json& field);
    /// Draws one value of schema `node`; `description` and `fallback` (the default) show as the
    /// label's tooltip.
    bool DrawValue(const std::string& label, Json& value, const Json& node, bool given,
                   std::string_view description = {}, const Json* fallback = nullptr);
    bool DrawLeaf(Json& value, const Json& resolved);
    bool DrawArray(const std::string& label, Json& value, const Json& node,
                   std::string_view description);
    bool DrawChoice(const std::string& label, Json& value, const Json& node,
                    std::string_view description);

    Json schema_;
    std::optional<bool> openAll_;
};

}  // namespace terraingen::viewer
