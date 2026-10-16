/// @file
/// @brief The GUI's editor of a terrain configuration: an outliner and an inspector.
#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace terraingen::gui {

/// An editor for a terrain configuration held as JSON, its widgets chosen from the
/// configuration's schema (`ConfigSchema`).
///
/// - The outliner (`DrawOutliner`) lists the surface, source, layers, filters, outputs and albedo;
///   lists add items of a chosen type, reorder by dragging and remove them, and selecting an item
///   opens it in the inspector.
/// - The inspector (`DrawInspector`) edits the selected item: its type and each setting, with a
///   label, unit and tooltip.
///
/// Settings the configuration leaves out show their defaults, dimmed, and are written only once
/// edited; resetting one removes it again, so an edited configuration stays as short as the one
/// opened.
class ConfigEditor {
  public:
    using Json = nlohmann::ordered_json;  ///< Keeps the keys in the order the file has them.

    /// Loads the configuration's schema.
    ConfigEditor();

    /// The configuration's schema, as `ConfigSchema` gives it.
    const Json& Schema() const {
        return schema_;
    }

    /// The definition schema node `node` refers to, or `node` itself.
    const Json& Resolve(const Json& node) const;

    /// A value for a new item of schema `node`: an object or choice with only its "type" (its first
    /// alternative's), an empty list, null for an optional, or the kind's zero.
    Json NewValue(const Json& node) const;

    /// Switches an object of a choice to another alternative, keeping the fields that
    /// alternative also has; does nothing for an unknown type.
    void SetType(Json& value, const Json& node, const std::string& type) const;

    /// The schema entry of a root field such as "source" or "layers"; null if there is none.
    const Json* RootField(std::string_view name) const;

    /// A setting's label: its key in words, capitalised, without a last word that only
    /// repeats its unit ("age_gyr" with unit "Gyr" is "Age").
    static std::string Label(std::string_view key, std::string_view unit = {});

    /// The outliner's selection, a JSON pointer such as "/layers/1" or "/source"; empty for none.
    const std::string& Selection() const {
        return selection_;
    }

    /// Selects the part of the configuration at a JSON pointer; empty for none.
    void Select(std::string pointer) {
        selection_ = std::move(pointer);
    }

    /// Appends an item of `type` to a root list and selects it; false if `type` is not one of the
    /// list's.
    /// @param list The root list: "layers", "filters" or "outputs".
    bool AddItem(Json& config, std::string_view list, const std::string& type);

    /// Removes an item of a root list, keeping the selection on what it showed, or none if
    /// it showed the item removed. Returns whether there was such an item.
    bool RemoveItem(Json& config, std::string_view list, std::size_t index);

    /// Moves an item of a root list, the selection following it; false if an index is out of range.
    bool MoveItem(Json& config, std::string_view list, std::size_t from, std::size_t to);

    /// Draws the outliner inside an ImGui window; returns whether the user changed `config`.
    bool DrawOutliner(Json& config);

    /// Draws the inspector for the selection inside an ImGui window; returns whether the user
    /// changed `config`.
    bool DrawInspector(Json& config);

    /// Opens every group of the inspector on its next draw.
    void ExpandAll() {
        openAll_ = true;
    }

    /// Closes every group of the inspector on its next draw.
    void CollapseAll() {
        openAll_ = false;
    }

  private:
    /// Opens or closes the next tree node as `ExpandAll` or `CollapseAll` asked.
    void NextNodeOpen() const;
    /// Draws a single root field of the outliner; returns whether `config` changed.
    bool DrawRootItem(Json& config, const Json& field);
    /// Draws a root list of the outliner; returns whether `config` changed.
    bool DrawRootList(Json& config, const Json& field);
    /// Draws each of `fields` of `object`; returns whether it changed.
    bool DrawFields(Json& object, const Json& fields);
    /// Draws one field of `object`, its default when unset; returns whether it changed.
    bool DrawField(Json& object, const Json& field);
    /// Draws one value as a row of the inspector's table: the label, the widget, and a
    /// reset button when `reset` is given and the value `given`; `fallback` is the default shown
    /// in the tooltip. Returns whether `value` changed.
    bool DrawValue(const std::string& label, Json& value, const Json& node, bool given,
                   std::string_view description, std::string_view unit, const Json* fallback,
                   bool* reset);
    /// Draws the widget of a number, integer, boolean, string or enum; returns whether
    /// `value` changed.
    bool DrawLeaf(Json& value, const Json& resolved, std::string_view unit);
    /// Draws a list as a group of its items; returns whether `value` changed.
    bool DrawArray(const std::string& label, Json& value, const Json& node,
                   std::string_view description);
    /// Draws the fields of a choice's selected alternative; returns whether `value` changed.
    bool DrawChoiceFields(Json& value, const Json& choice);
    /// Draws a choice's type menu; returns whether `value` changed type.
    bool DrawTypeMenu(const char* id, Json& value, const Json& choice);

    Json schema_;
    std::string selection_;
    std::optional<bool> openAll_;
};

}  // namespace terraingen::gui
