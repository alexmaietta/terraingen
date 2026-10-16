/// @file
/// @brief The GUI's editor of a terrain configuration: an outliner and an inspector.
#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace terraingen::gui {

/// @brief An editor for a terrain configuration held as JSON, its widgets chosen from the
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
    using Json = nlohmann::json;

    /// @brief Loads the configuration's schema.
    ConfigEditor();

    /// @brief The configuration's schema, as `ConfigSchema` gives it.
    /// @return The schema document.
    const Json& Schema() const {
        return schema_;
    }

    /// @brief The schema a node stands for, following a reference to a definition.
    /// @param node A schema node.
    /// @return The definition `node` refers to, or `node` itself.
    const Json& Resolve(const Json& node) const;

    /// @brief A value for a new item of a schema.
    /// @param node The item's schema.
    /// @return An object or choice with only its "type" (its first alternative's), an empty list,
    /// null for an optional, or the kind's zero.
    Json NewValue(const Json& node) const;

    /// @brief Switches an object of a choice to another alternative, keeping the fields that
    /// alternative also has; does nothing for an unknown type.
    /// @param value The object to change.
    /// @param node The choice's schema.
    /// @param type The alternative's "type".
    void SetType(Json& value, const Json& node, const std::string& type) const;

    /// @brief The schema of a root field of the configuration.
    /// @param name The field, such as "source" or "layers".
    /// @return The field's schema entry; null if there is none.
    const Json* RootField(std::string_view name) const;

    /// @brief A setting's label: its key in words, capitalised, without a last word that only
    /// repeats its unit ("age_gyr" with unit "Gyr" is "Age").
    /// @param key The JSON key.
    /// @param unit The setting's unit symbol, or empty.
    /// @return The label.
    static std::string Label(std::string_view key, std::string_view unit = {});

    /// @brief The outliner's selection.
    /// @return A JSON pointer into the configuration, such as "/layers/1" or "/source"; empty for
    /// none.
    const std::string& Selection() const {
        return selection_;
    }

    /// @brief Selects part of the configuration.
    /// @param pointer A JSON pointer into the configuration; empty for none.
    void Select(std::string pointer) {
        selection_ = std::move(pointer);
    }

    /// @brief Appends an item to a root list and selects it.
    /// @param config The configuration.
    /// @param list The root list: "layers", "filters" or "outputs".
    /// @param type The new item's "type".
    /// @return Whether `type` is one of the list's.
    bool AddItem(Json& config, std::string_view list, const std::string& type);

    /// @brief Removes an item of a root list, keeping the selection on what it showed, or none if
    /// it showed the item removed.
    /// @param config The configuration.
    /// @param list The root list.
    /// @param index The item's index.
    /// @return Whether there was such an item.
    bool RemoveItem(Json& config, std::string_view list, std::size_t index);

    /// @brief Moves an item of a root list, the selection following it.
    /// @param config The configuration.
    /// @param list The root list.
    /// @param from The item's index.
    /// @param to Its new index.
    /// @return Whether both indices are in range.
    bool MoveItem(Json& config, std::string_view list, std::size_t from, std::size_t to);

    /// @brief Draws the outliner inside an ImGui window.
    /// @param config The configuration.
    /// @return Whether the user changed `config`.
    bool DrawOutliner(Json& config);

    /// @brief Draws the inspector for the selection inside an ImGui window.
    /// @param config The configuration.
    /// @return Whether the user changed `config`.
    bool DrawInspector(Json& config);

    /// @brief Opens every group of the inspector on its next draw.
    void ExpandAll() {
        openAll_ = true;
    }

    /// @brief Closes every group of the inspector on its next draw.
    void CollapseAll() {
        openAll_ = false;
    }

  private:
    /// @brief Opens or closes the next tree node as `ExpandAll` or `CollapseAll` asked.
    void NextNodeOpen() const;
    /// @brief Draws a single root field of the outliner; returns whether `config` changed.
    bool DrawRootItem(Json& config, const Json& field);
    /// @brief Draws a root list of the outliner; returns whether `config` changed.
    bool DrawRootList(Json& config, const Json& field);
    /// @brief Draws each of `fields` of `object`; returns whether it changed.
    bool DrawFields(Json& object, const Json& fields);
    /// @brief Draws one field of `object`, its default when unset; returns whether it changed.
    bool DrawField(Json& object, const Json& field);
    /// @brief Draws one value as a row of the inspector's table: the label, the widget, and a
    /// reset button when `reset` is given and the value `given`; `fallback` is the default shown
    /// in the tooltip. Returns whether `value` changed.
    bool DrawValue(const std::string& label, Json& value, const Json& node, bool given,
                   std::string_view description, std::string_view unit, const Json* fallback,
                   bool* reset);
    /// @brief Draws the widget of a number, integer, boolean, string or enum; returns whether
    /// `value` changed.
    bool DrawLeaf(Json& value, const Json& resolved, std::string_view unit);
    /// @brief Draws a list as a group of its items; returns whether `value` changed.
    bool DrawArray(const std::string& label, Json& value, const Json& node,
                   std::string_view description);
    /// @brief Draws the fields of a choice's selected alternative; returns whether `value` changed.
    bool DrawChoiceFields(Json& value, const Json& choice);
    /// @brief Draws a choice's type menu; returns whether `value` changed type.
    bool DrawTypeMenu(const char* id, Json& value, const Json& choice);

    Json schema_;
    std::string selection_;
    std::optional<bool> openAll_;
};

}  // namespace terraingen::gui
