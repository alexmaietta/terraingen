/// @file
/// @brief The configuration editor's outliner and inspector, drawn with ImGui.
#include "terraingen/app/gui/config_editor.h"

#include "terraingen/pipeline/config_json.h"

#include <imgui.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::gui {

namespace {

using Json = ConfigEditor::Json;

/// Lucide glyphs (private-use codepoints of the icon font Ernie's viewer loads), as UTF-8.
constexpr const char* kGlyphPlus = "\xee\x84\xbd";
constexpr const char* kGlyphTrash = "\xee\x86\x8e";
constexpr const char* kGlyphUp = "\xee\x81\xb0";
constexpr const char* kGlyphDown = "\xee\x81\xad";
constexpr const char* kGlyphReset = "\xee\x85\x88";
constexpr const char* kGlyphBox = "\xee\x81\xa1";
constexpr const char* kGlyphFileOutput = "\xee\x83\x88";
constexpr const char* kGlyphFilter = "\xee\x83\x9c";
constexpr const char* kGlyphPalette = "\xee\x87\x9d";

/// The glyph for an item of `type` in root list or field `root`.
const char* TypeGlyph(std::string_view root, std::string_view type) {
    if (root == "outputs") {
        return kGlyphFileOutput;
    }
    if (root == "filters") {
        return kGlyphFilter;
    }
    if (root == "albedo") {
        return kGlyphPalette;
    }
    struct Entry {
        std::string_view type;
        const char* glyph;
    };
    static constexpr Entry kGlyphs[] = {
        {"plane", "\xee\x84\x90"},     {"sphere", "\xee\x87\xb3"},
        {"flat", "\xee\x85\xa7"},      {"raster", "\xee\x83\xb6"},
        {"chain", "\xee\x92\xb3"},     {"noise", "\xee\x88\xb1"},
        {"craters", "\xee\x8d\x85"},   {"boulders", "\xee\x89\x82"},
        {"roughness", "\xee\x90\x92"}, {"bedforms", "\xee\x86\xb0"},
        {"scale", "\xee\x8b\xac"},     {"clamp", "\xee\x85\x8b"},
        {"region", "\xee\x82\xac"},    {"secondaries", "\xee\x81\xb6"},
    };
    for (const Entry& entry : kGlyphs) {
        if (entry.type == type) {
            return entry.glyph;
        }
    }
    return kGlyphBox;
}

/// Text for a setting the configuration leaves at its default.
const ImVec4 kDefaultColour(0.55f, 0.57f, 0.62f, 1.0f);

int ResizeString(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* text = static_cast<std::string*>(data->UserData);
        text->resize(static_cast<std::size_t>(data->BufTextLen));
        data->Buf = text->data();
    }
    return 0;
}

bool InputString(const char* label, std::string& text) {
    return ImGui::InputText(label, text.data(), text.capacity() + 1,
                            ImGuiInputTextFlags_CallbackResize, ResizeString, &text);
}

std::string Lower(std::string_view text) {
    std::string lower(text);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lower;
}

/// Shows `text` (and `extra` below it, dimmed) as the last item's tooltip, wrapped to a readable
/// width; nothing for empty text.
void Tooltip(std::string_view text, const std::string& extra = {}) {
    if (text.empty() && extra.empty()) {
        return;
    }
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 28.0f);
        if (!text.empty()) {
            ImGui::TextUnformatted(text.data(), text.data() + text.size());
        }
        if (!extra.empty()) {
            ImGui::TextColored(kDefaultColour, "%s", extra.c_str());
        }
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/// One item of root list `list`, in words: "layers" is "layer".
std::string Singular(std::string_view list) {
    if (list.size() > 1 && list.back() == 's') {
        list.remove_suffix(1);
    }
    return std::string(list);
}

/// An enum value as the menus show it: its words, as written ("worley_f2" is "worley f2").
std::string ChoiceText(std::string_view choice) {
    std::string text(choice);
    std::replace(text.begin(), text.end(), '_', ' ');
    return text;
}

/// The type of a choice's object, or its first alternative's when it has none.
std::string TypeOf(const Json& value, const Json& choice) {
    if (value.is_object() && value.contains("type") && value["type"].is_string()) {
        return value["type"];
    }
    return choice["alternatives"].front()["type"];
}

const Json* AlternativeOf(const Json& choice, const std::string& type) {
    for (const Json& alternative : choice["alternatives"]) {
        if (alternative["type"] == type) {
            return &alternative;
        }
    }
    return nullptr;
}

/// The description of choice `index` of an enum schema, if it has one.
std::string_view ChoiceDescription(const Json& node, std::size_t index) {
    const Json* descriptions = node.contains("descriptions") ? &node["descriptions"] : nullptr;
    if (descriptions == nullptr || index >= descriptions->size()) {
        return {};
    }
    return (*descriptions)[index].get_ref<const std::string&>();
}

/// A short detail of a list item for the outliner: the file name of its path, if it has one.
std::string ItemDetail(const Json& item) {
    if (item.is_object() && item.contains("path") && item["path"].is_string()) {
        return std::filesystem::path(item["path"].get<std::string>()).filename().string();
    }
    return {};
}

/// The default value shown for a setting, as text.
std::string DefaultText(const Json& fallback, std::string_view unit) {
    if (fallback.is_null() || fallback.is_object() || fallback.is_array()) {
        return {};
    }
    std::string text = fallback.is_string() ? fallback.get<std::string>() : fallback.dump();
    if (!unit.empty()) {
        text += " " + std::string(unit);
    }
    return "Default: " + text;
}

/// A small icon button that does not take keyboard focus.
bool IconButton(const char* glyph, const char* id, const char* tooltip) {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(3.0f, 1.0f));
    const bool pressed = ImGui::SmallButton((std::string(glyph) + "##" + id).c_str());
    ImGui::PopStyleVar();
    ImGui::SetItemTooltip("%s", tooltip);
    return pressed;
}

/// The step of a dragged number: about a hundredth of its size, at least 0.001.
float DragSpeed(double value) {
    return static_cast<float>(std::max(std::abs(value) * 0.01, 0.001));
}

/// Flags of the inspector's settings table.
constexpr ImGuiTableFlags kTableFlags =
    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg;

void BeginSettingsTable() {
    ImGui::BeginTable("##settings", 3, kTableFlags);
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.45f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
    ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
}

}  // namespace

ConfigEditor::ConfigEditor() : schema_(Json::parse(ConfigSchema())) {}

const Json& ConfigEditor::Resolve(const Json& node) const {
    if (node.value("kind", "") == "ref") {
        return schema_["definitions"].at(node["ref"].get<std::string>());
    }
    return node;
}

Json ConfigEditor::NewValue(const Json& node) const {
    const Json& resolved = Resolve(node);
    const std::string kind = resolved["kind"];
    if (kind == "choice") {
        return Json{{"type", resolved["alternatives"].front()["type"]}};
    }
    if (kind == "object") {
        return Json::object();
    }
    if (kind == "array") {
        return Json::array();
    }
    if (kind == "optional") {
        return nullptr;
    }
    if (kind == "enum") {
        return resolved["choices"].front();
    }
    if (kind == "boolean") {
        return false;
    }
    if (kind == "string") {
        return "";
    }
    if (kind == "integer") {
        return 0;
    }
    return 0.0;
}

void ConfigEditor::SetType(Json& value, const Json& node, const std::string& type) const {
    const Json& choice = Resolve(node);
    const Json* alternative = AlternativeOf(choice, type);
    if (alternative == nullptr) {
        return;
    }
    Json next = Json{{"type", type}};
    if (value.is_object()) {
        for (const Json& field : (*alternative)["fields"]) {
            const std::string name = field["name"];
            if (value.contains(name)) {
                next[name] = value[name];
            }
        }
    }
    value = std::move(next);
}

const Json* ConfigEditor::RootField(std::string_view name) const {
    for (const Json& field : schema_["root"]["fields"]) {
        if (field["name"] == name) {
            return &field;
        }
    }
    return nullptr;
}

std::string ConfigEditor::Label(std::string_view key, std::string_view unit) {
    std::vector<std::string> words;
    std::string word;
    for (const char c : key) {
        if (c == '_') {
            if (!word.empty()) {
                words.push_back(word);
            }
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) {
        words.push_back(word);
    }
    const std::string lowerUnit = Lower(unit);
    if (words.size() > 1 && !unit.empty() &&
        (Lower(words.back()) == lowerUnit ||
         (lowerUnit == "deg" && Lower(words.back()) == "degrees"))) {
        words.pop_back();
    }
    std::string label;
    for (std::string& w : words) {
        if (w.size() == 1) {
            w[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        }
        label += (label.empty() ? "" : " ") + w;
    }
    if (!label.empty()) {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    }
    return label;
}

bool ConfigEditor::AddItem(Json& config, std::string_view list, const std::string& type) {
    const Json* field = RootField(list);
    if (field == nullptr || Resolve((*field)["schema"])["kind"] != "array") {
        return false;
    }
    const Json& item = Resolve(Resolve((*field)["schema"])["of"]);
    if (item["kind"] != "choice" || AlternativeOf(item, type) == nullptr) {
        return false;
    }
    Json& items = config[std::string(list)];
    if (!items.is_array()) {
        items = Json::array();
    }
    items.push_back(Json{{"type", type}});
    selection_ = "/" + std::string(list) + "/" + std::to_string(items.size() - 1);
    return true;
}

bool ConfigEditor::RemoveItem(Json& config, std::string_view list, std::size_t index) {
    const std::string key(list);
    if (!config.contains(key) || !config[key].is_array() || index >= config[key].size()) {
        return false;
    }
    config[key].erase(index);
    const std::string prefix = "/" + key + "/";
    if (selection_.rfind(prefix, 0) == 0) {
        const std::size_t selected = std::stoul(selection_.substr(prefix.size()));
        if (selected == index) {
            selection_.clear();
        } else if (selected > index) {
            selection_ = prefix + std::to_string(selected - 1);
        }
    }
    return true;
}

bool ConfigEditor::MoveItem(Json& config, std::string_view list, std::size_t from, std::size_t to) {
    const std::string key(list);
    if (!config.contains(key) || !config[key].is_array()) {
        return false;
    }
    Json& items = config[key];
    if (from >= items.size() || to >= items.size()) {
        return false;
    }
    if (from == to) {
        return true;
    }
    Json item = items[from];
    items.erase(from);
    items.insert(items.begin() + static_cast<std::ptrdiff_t>(to), std::move(item));
    const std::string prefix = "/" + key + "/";
    if (selection_.rfind(prefix, 0) == 0) {
        std::size_t selected = std::stoul(selection_.substr(prefix.size()));
        if (selected == from) {
            selected = to;
        } else if (from < selected && selected <= to) {
            --selected;
        } else if (to <= selected && selected < from) {
            ++selected;
        }
        selection_ = prefix + std::to_string(selected);
    }
    return true;
}

void ConfigEditor::NextNodeOpen() const {
    if (openAll_) {
        ImGui::SetNextItemOpen(*openAll_);
    }
}

// The outliner.

bool ConfigEditor::DrawOutliner(Json& config) {
    if (!config.is_object()) {
        config = Json::object();
    }
    bool changed = false;
    ImGui::PushID("outliner");
    for (const Json& field : schema_["root"]["fields"]) {
        if (field["schema"].value("fixed", false)) {
            continue;
        }
        const std::string kind = Resolve(field["schema"])["kind"];
        changed |= kind == "array" ? DrawRootList(config, field) : DrawRootItem(config, field);
    }
    ImGui::PopID();
    return changed;
}

bool ConfigEditor::DrawRootItem(Json& config, const Json& field) {
    const std::string name = field["name"];
    const std::string pointer = "/" + name;
    const Json& node = Resolve(field["schema"]);
    ImGui::PushID(name.c_str());
    ImGui::SeparatorText(Label(name).c_str());
    Tooltip(field.value("description", ""));
    bool changed = false;
    const bool optional = node["kind"] == "optional";
    const bool present = config.contains(name) && !config[name].is_null();
    if (optional && !present) {
        ImGui::TextDisabled("None");
        ImGui::SameLine();
        if (IconButton(kGlyphPlus, "add", ("Add " + Lower(Label(name))).c_str())) {
            config[name] = Json::object();
            selection_ = pointer;
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }
    const Json& shown = present ? config[name] : field["default"];
    std::string text;
    if (Resolve(optional ? node["of"] : node)["kind"] == "choice") {
        const std::string type = TypeOf(shown, Resolve(optional ? node["of"] : node));
        text = std::string(TypeGlyph(name, type)) + "  " + Label(type);
    } else {
        text = std::string(TypeGlyph(name, name)) + "  " + Label(name);
    }
    const float trash = optional ? ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x : 0;
    if (ImGui::Selectable(text.c_str(), selection_ == pointer, ImGuiSelectableFlags_AllowOverlap,
                          ImVec2(ImGui::GetContentRegionAvail().x - trash, 0.0f))) {
        selection_ = pointer;
    }
    if (optional) {
        ImGui::SameLine();
        if (IconButton(kGlyphTrash, "remove", ("Remove the " + Lower(Label(name))).c_str())) {
            config.erase(name);
            if (selection_.rfind(pointer, 0) == 0) {
                selection_.clear();
            }
            changed = true;
        }
    }
    ImGui::PopID();
    return changed;
}

bool ConfigEditor::DrawRootList(Json& config, const Json& field) {
    const std::string name = field["name"];
    const Json& item = Resolve(Resolve(field["schema"])["of"]);
    ImGui::PushID(name.c_str());
    const Json& items =
        config.contains(name) && config[name].is_array() ? config[name] : field["default"];
    ImGui::SeparatorText((Label(name) + " (" + std::to_string(items.size()) + ")").c_str());
    Tooltip(field.value("description", ""));
    bool changed = false;

    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, std::size_t>> move;
    const std::string payload = "tg_" + name;
    for (std::size_t i = 0; i < items.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const std::string pointer = "/" + name + "/" + std::to_string(i);
        const std::string type = TypeOf(items[i], item);
        std::string text = std::string(TypeGlyph(name, type)) + "  " + Label(type);
        const std::string detail = ItemDetail(items[i]);
        const float trash = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;
        if (ImGui::Selectable(text.c_str(), selection_ == pointer,
                              ImGuiSelectableFlags_AllowOverlap,
                              ImVec2(ImGui::GetContentRegionAvail().x - trash, 0.0f))) {
            selection_ = pointer;
        }
        if (const Json* alternative = AlternativeOf(item, type)) {
            Tooltip((*alternative).value("description", ""), "Drag to reorder");
        }
        if (ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(payload.c_str(), &i, sizeof(i));
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndDragDropSource();
        }
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* dropped = ImGui::AcceptDragDropPayload(payload.c_str())) {
                move = std::pair{*static_cast<const std::size_t*>(dropped->Data), i};
            }
            ImGui::EndDragDropTarget();
        }
        if (!detail.empty()) {
            ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f);
            ImGui::TextDisabled("%s", detail.c_str());
        }
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - ImGui::GetFrameHeight());
        if (IconButton(kGlyphTrash, "remove", "Remove")) {
            remove = i;
        }
        ImGui::PopID();
    }

    if (ImGui::Button((std::string(kGlyphPlus) + "  Add " + Singular(name)).c_str(),
                      ImVec2(-1.0f, 0.0f))) {
        ImGui::OpenPopup("add");
    }
    if (ImGui::BeginPopup("add")) {
        for (const Json& alternative : item["alternatives"]) {
            const std::string type = alternative["type"];
            const std::string text = std::string(TypeGlyph(name, type)) + "  " + Label(type);
            if (ImGui::Selectable(text.c_str())) {
                changed |= AddItem(config, name, type);
            }
            Tooltip(alternative.value("description", ""));
        }
        ImGui::EndPopup();
    }
    if (remove) {
        changed |= RemoveItem(config, name, *remove);
    }
    if (move) {
        changed |= MoveItem(config, name, move->first, move->second);
    }
    ImGui::PopID();
    return changed;
}

// The inspector.

bool ConfigEditor::DrawInspector(Json& config) {
    if (!config.is_object()) {
        config = Json::object();
    }
    Json::json_pointer pointer;
    try {
        pointer = Json::json_pointer(selection_);
    } catch (const Json::exception&) {
        selection_.clear();
    }
    if (selection_.empty()) {
        ImGui::TextDisabled("Select part of the terrain in the outliner to edit it.");
        return false;
    }
    // The selection's schema: a root field, or an item of a root list.
    const std::string root = selection_.substr(1, selection_.find('/', 1) - 1);
    const Json* field = RootField(root);
    if (field == nullptr) {
        selection_.clear();
        return false;
    }
    const Json* node = &Resolve((*field)["schema"]);
    const bool item = selection_.find('/', 1) != std::string::npos;
    if (item) {
        if ((*node)["kind"] != "array" || !config.contains(pointer)) {
            selection_.clear();
            return false;
        }
        node = &Resolve((*node)["of"]);
    } else if ((*node)["kind"] == "optional") {
        if (!config.contains(root) || config[root].is_null()) {
            selection_.clear();
            return false;
        }
        node = &Resolve((*node)["of"]);
    }
    Json value = config.contains(pointer) ? config[pointer] : (*field)["default"];

    ImGui::PushID(selection_.c_str());
    bool changed = false;
    if ((*node)["kind"] == "choice") {
        const std::string type = TypeOf(value, *node);
        const std::string title = item ? Label(Singular(root)) : Label(root);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted((std::string(TypeGlyph(root, type)) + "  " + title).c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        changed |= DrawTypeMenu("##type", value, *node);
        if (const Json* alternative = AlternativeOf(*node, TypeOf(value, *node))) {
            ImGui::PushStyleColor(ImGuiCol_Text, kDefaultColour);
            ImGui::TextWrapped("%s", alternative->value("description", "").c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
        BeginSettingsTable();
        changed |= DrawChoiceFields(value, *node);
        ImGui::EndTable();
    } else {
        ImGui::TextUnformatted((std::string(TypeGlyph(root, root)) + "  " + Label(root)).c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, kDefaultColour);
        ImGui::TextWrapped("%s", field->value("description", "").c_str());
        ImGui::PopStyleColor();
        ImGui::Separator();
        if (!value.is_object()) {
            value = Json::object();
        }
        BeginSettingsTable();
        changed |= DrawFields(value, (*node)["fields"]);
        ImGui::EndTable();
    }
    ImGui::PopID();
    openAll_.reset();
    if (changed) {
        config[pointer] = std::move(value);
    }
    return changed;
}

bool ConfigEditor::DrawTypeMenu(const char* id, Json& value, const Json& choice) {
    const std::string type = TypeOf(value, choice);
    bool changed = false;
    const bool open = ImGui::BeginCombo(id, Label(type).c_str());
    if (!open) {
        if (const Json* current = AlternativeOf(choice, type)) {
            Tooltip(current->value("description", ""));
        }
        return false;
    }
    for (const Json& alternative : choice["alternatives"]) {
        const std::string name = alternative["type"];
        if (ImGui::Selectable(Label(name).c_str(), name == type)) {
            SetType(value, choice, name);
            changed = true;
        }
        Tooltip(alternative.value("description", ""));
    }
    ImGui::EndCombo();
    return changed;
}

bool ConfigEditor::DrawChoiceFields(Json& value, const Json& choice) {
    if (!value.is_object()) {
        value = Json{{"type", TypeOf(value, choice)}};
    }
    const Json* alternative = AlternativeOf(choice, TypeOf(value, choice));
    if (alternative == nullptr) {
        return false;
    }
    value["type"] = (*alternative)["type"];
    return DrawFields(value, (*alternative)["fields"]);
}

bool ConfigEditor::DrawFields(Json& object, const Json& fields) {
    bool changed = false;
    for (const Json& field : fields) {
        changed |= DrawField(object, field);
    }
    return changed;
}

bool ConfigEditor::DrawField(Json& object, const Json& field) {
    const std::string name = field["name"];
    if (name == "type") {
        return false;
    }
    const Json& node = field["schema"];
    if (node.value("fixed", false)) {
        if (!object.contains(name)) {
            object[name] = field["default"];
        }
        return false;
    }
    const bool given = object.contains(name);
    Json value = given ? object[name] : field["default"];
    const std::string unit = field.value("unit", "");
    ImGui::PushID(name.c_str());
    bool reset = false;
    const bool changed = DrawValue(Label(name, unit), value, node, given,
                                   field.value("description", ""), unit, &field["default"], &reset);
    ImGui::PopID();
    if (reset) {
        object.erase(name);
        return true;
    }
    if (changed) {
        object[name] = std::move(value);
    }
    return changed;
}

bool ConfigEditor::DrawValue(const std::string& label, Json& value, const Json& node, bool given,
                             std::string_view description, std::string_view unit,
                             const Json* fallback, bool* reset) {
    const Json& resolved = Resolve(node);
    const std::string kind = resolved["kind"];
    const auto resetCell = [&] {
        ImGui::TableSetColumnIndex(2);
        if (reset != nullptr && given && IconButton(kGlyphReset, "reset", "Reset to default")) {
            *reset = true;
        }
    };
    if (kind == "array") {
        return DrawArray(label, value, resolved, description);
    }
    if (kind == "object" || kind == "choice") {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        if (given) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        }
        NextNodeOpen();
        const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
        Tooltip(description);
        bool changed = false;
        if (kind == "choice") {
            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1.0f);
            changed |= DrawTypeMenu("##type", value, resolved);
        }
        resetCell();
        if (open) {
            if (kind == "choice") {
                changed |= DrawChoiceFields(value, resolved);
            } else {
                if (!value.is_object()) {
                    value = Json::object();
                }
                changed |= DrawFields(value, resolved["fields"]);
            }
            ImGui::TreePop();
        }
        return changed;
    }

    // Leaves: the label, then the widget, then the reset button.
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    if (given) {
        ImGui::TextUnformatted(label.c_str());
    } else {
        ImGui::TextColored(kDefaultColour, "%s", label.c_str());
    }
    Tooltip(description, DefaultText(fallback != nullptr ? *fallback : Json(), unit));
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-1.0f);
    bool changed = false;
    if (kind != "optional") {
        changed = DrawLeaf(value, resolved, unit);
    } else {
        bool set = !value.is_null();
        if (ImGui::Checkbox("##set", &set)) {
            value = set ? resolved["default"] : Json(nullptr);
            changed = true;
        }
        ImGui::SetItemTooltip(set ? "Set; clear to leave it unset" : "Unset; check to set it");
        if (set) {
            const Json& inner = Resolve(resolved["of"]);
            const std::string innerKind = inner["kind"];
            if (innerKind == "object" || innerKind == "choice" || innerKind == "array") {
                resetCell();
                ImGui::TreePush(label.c_str());
                changed |= DrawValue(label, value, resolved["of"], true, description, unit, nullptr,
                                     nullptr);
                ImGui::TreePop();
                return changed;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(-1.0f);
            changed |= DrawLeaf(value, inner, unit);
        }
    }
    resetCell();
    return changed;
}

bool ConfigEditor::DrawLeaf(Json& value, const Json& resolved, std::string_view unit) {
    const std::string kind = resolved["kind"];
    if (kind == "boolean") {
        bool checked = value.is_boolean() && value.get<bool>();
        if (ImGui::Checkbox("##value", &checked)) {
            value = checked;
            return true;
        }
        return false;
    }
    if (kind == "enum") {
        const std::string current = value.is_string() ? value.get<std::string>() : "";
        const Json& choices = resolved["choices"];
        bool changed = false;
        const bool open = ImGui::BeginCombo("##value", ChoiceText(current).c_str());
        if (!open) {
            for (std::size_t i = 0; i < choices.size(); ++i) {
                if (choices[i] == current) {
                    Tooltip(ChoiceDescription(resolved, i));
                }
            }
            return false;
        }
        for (std::size_t i = 0; i < choices.size(); ++i) {
            const std::string name = choices[i];
            if (ImGui::Selectable(ChoiceText(name).c_str(), name == current)) {
                value = name;
                changed = true;
            }
            Tooltip(ChoiceDescription(resolved, i));
        }
        ImGui::EndCombo();
        return changed;
    }
    if (kind == "string") {
        std::string text = value.is_string() ? value.get<std::string>() : "";
        if (InputString("##value", text)) {
            value = text;
            return true;
        }
        return false;
    }
    const std::string suffix = unit.empty() ? "" : " " + std::string(unit);
    if (kind == "integer") {
        std::int64_t number = value.is_number() ? value.get<std::int64_t>() : 0;
        const std::int64_t low = resolved.contains("minimum")
                                     ? resolved["minimum"].get<std::int64_t>()
                                     : std::numeric_limits<std::int64_t>::lowest();
        const std::int64_t high = std::numeric_limits<std::int64_t>::max();
        const std::string format = "%lld" + suffix;
        if (ImGui::DragScalar("##value", ImGuiDataType_S64, &number, 0.2f, &low, &high,
                              format.c_str(), ImGuiSliderFlags_AlwaysClamp)) {
            value = number;
            return true;
        }
        return false;
    }
    double number = value.is_number() ? value.get<double>() : 0.0;
    const std::string format = "%.6g" + suffix;
    if (ImGui::DragScalar("##value", ImGuiDataType_Double, &number, DragSpeed(number), nullptr,
                          nullptr, format.c_str())) {
        value = number;
        return true;
    }
    return false;
}

bool ConfigEditor::DrawArray(const std::string& label, Json& value, const Json& node,
                             std::string_view description) {
    if (!value.is_array()) {
        value = Json::array();
    }
    const Json& of = node["of"];
    const Json& item = Resolve(of);
    const std::string itemKind = item["kind"];
    bool changed = false;

    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    NextNodeOpen();
    const std::string title = label + " (" + std::to_string(value.size()) + ")";
    const bool open = ImGui::TreeNodeEx(
        label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth | ImGuiTreeNodeFlags_DefaultOpen, "%s",
        title.c_str());
    Tooltip(description);
    ImGui::TableSetColumnIndex(1);
    if (itemKind == "choice") {
        if (IconButton(kGlyphPlus, "add", "Add an item")) {
            ImGui::OpenPopup("add");
        }
        if (ImGui::BeginPopup("add")) {
            for (const Json& alternative : item["alternatives"]) {
                const std::string type = alternative["type"];
                if (ImGui::Selectable(Label(type).c_str())) {
                    value.push_back(Json{{"type", type}});
                    changed = true;
                }
                Tooltip(alternative.value("description", ""));
            }
            ImGui::EndPopup();
        }
    } else if (IconButton(kGlyphPlus, "add", "Add an item")) {
        value.push_back(itemKind == "object" ? Json::object() : node["default"]);
        changed = true;
    }
    if (!open) {
        return changed;
    }

    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, std::size_t>> swap;
    const bool composite = itemKind == "object" || itemKind == "choice";
    for (std::size_t i = 0; i < value.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        const auto buttons = [&] {
            ImGui::SameLine();
            if (IconButton(kGlyphUp, "up", "Move up") && i > 0) {
                swap = std::pair{i - 1, i};
            }
            ImGui::SameLine();
            if (IconButton(kGlyphDown, "down", "Move down") && i + 1 < value.size()) {
                swap = std::pair{i, i + 1};
            }
            ImGui::SameLine();
            if (IconButton(kGlyphTrash, "remove", "Remove")) {
                remove = i;
            }
        };
        if (composite) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            NextNodeOpen();
            const bool itemOpen =
                ImGui::TreeNodeEx("##item", ImGuiTreeNodeFlags_SpanFullWidth, "#%zu", i + 1);
            ImGui::TableSetColumnIndex(1);
            if (itemKind == "choice") {
                const float width =
                    ImGui::GetContentRegionAvail().x -
                    3.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
                ImGui::SetNextItemWidth(std::max(width, 40.0f));
                changed |= DrawTypeMenu("##type", value[i], item);
            } else {
                ImGui::Dummy(ImVec2(0.0f, 0.0f));
            }
            buttons();
            if (itemOpen) {
                if (itemKind == "choice") {
                    changed |= DrawChoiceFields(value[i], item);
                } else {
                    if (!value[i].is_object()) {
                        value[i] = Json::object();
                    }
                    changed |= DrawFields(value[i], item["fields"]);
                }
                ImGui::TreePop();
            }
        } else {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("#%zu", i + 1);
            ImGui::TableSetColumnIndex(1);
            const float width = ImGui::GetContentRegionAvail().x -
                                3.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(std::max(width, 40.0f));
            changed |= DrawLeaf(value[i], item, {});
            buttons();
        }
        ImGui::PopID();
    }
    if (remove) {
        value.erase(*remove);
        changed = true;
    }
    if (swap) {
        std::swap(value[swap->first], value[swap->second]);
        changed = true;
    }
    ImGui::TreePop();
    return changed;
}

}  // namespace terraingen::gui
