#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace terraingen::viewer {

/// An editor for a terrain configuration held as JSON, its widgets chosen from the
/// configuration's schema (`ConfigSchema`), in two parts:
/// - the outliner (`DrawOutliner`) lists what the terrain is made of: its surface and source, the
///   layers, filters and outputs in order, and the albedo. Lists add items of a chosen type,
///   reorder by dragging and remove them; selecting an item opens it in the inspector;
/// - the inspector (`DrawInspector`) edits the selected item: its type, and each setting with a
///   readable label and its unit, numbers dragged or typed, enum and type menus, optional
///   settings switched on and off, nested settings in collapsible groups and nested lists.
///
/// Settings the configuration leaves out show their defaults, dimmed, and are written only once
/// edited; a reset button beside an edited setting removes it again. So an edited configuration
/// stays as short as the one opened. Hovering a setting, type or enum value shows what it means.
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

    /// The schema of the configuration's root field `name` ("source", "layers"...); null if
    /// there is none.
    const Json* RootField(std::string_view name) const;

    /// A setting's label: its key in words, capitalised, without a last word that only repeats
    /// its unit ("age_gyr" with unit "Gyr" is "Age").
    static std::string Label(std::string_view key, std::string_view unit = {});

    /// The outliner's selection: a JSON pointer into the configuration, such as "/layers/1" or
    /// "/source"; empty for none.
    const std::string& Selection() const {
        return selection_;
    }

    void Select(std::string pointer) {
        selection_ = std::move(pointer);
    }

    /// Appends an item of `type` to root list `list` ("layers", "filters", "outputs") and
    /// selects it; returns whether `type` is one of the list's.
    bool AddItem(Json& config, std::string_view list, const std::string& type);

    /// Removes item `index` of root list `list`, keeping the selection on what it showed (or
    /// none, if it showed the item removed); returns whether there was such an item.
    bool RemoveItem(Json& config, std::string_view list, std::size_t index);

    /// Moves item `from` of root list `list` to position `to`, the selection following it;
    /// returns whether both are in range.
    bool MoveItem(Json& config, std::string_view list, std::size_t from, std::size_t to);

    /// Draws the outliner inside an ImGui window; returns whether the user changed `config`.
    bool DrawOutliner(Json& config);

    /// Draws the inspector for the selection inside an ImGui window; returns whether the user
    /// changed `config`.
    bool DrawInspector(Json& config);

    /// Opens, or closes, every group of the inspector on its next draw.
    void ExpandAll() {
        openAll_ = true;
    }

    void CollapseAll() {
        openAll_ = false;
    }

  private:
    /// Opens or closes the next tree node as `ExpandAll` or `CollapseAll` asked.
    void NextNodeOpen() const;
    bool DrawRootItem(Json& config, const Json& field);
    bool DrawRootList(Json& config, const Json& field);
    bool DrawFields(Json& object, const Json& fields);
    bool DrawField(Json& object, const Json& field);
    /// Draws one value of schema `node` as a row of the inspector's table: `label`, the widget,
    /// and a reset button when `reset` is given and `given`.
    bool DrawValue(const std::string& label, Json& value, const Json& node, bool given,
                   std::string_view description, std::string_view unit, const Json* fallback,
                   bool* reset);
    bool DrawLeaf(Json& value, const Json& resolved, std::string_view unit);
    bool DrawArray(const std::string& label, Json& value, const Json& node,
                   std::string_view description);
    bool DrawChoiceFields(Json& value, const Json& choice);
    bool DrawTypeMenu(const char* id, Json& value, const Json& choice);

    Json schema_;
    std::string selection_;
    std::optional<bool> openAll_;
};

}  // namespace terraingen::viewer
