#include "terraingen/app/viewer/config_editor.h"

#include "terraingen/pipeline/config_json.h"

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace terraingen::viewer {

namespace {

using Json = nlohmann::json;

/// Text for a dimmed field the configuration leaves at its default.
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

/// A short summary of a list item for its tree node: its type, else its position.
std::string ItemLabel(const Json& item, std::size_t index) {
    std::string label = "[" + std::to_string(index) + "]";
    if (item.is_object() && item.contains("type") && item["type"].is_string()) {
        label += " " + item["type"].get<std::string>();
    }
    return label;
}

/// The label column: the field's name, dimmed when the configuration leaves it out.
void Label(const std::string& name, bool given) {
    ImGui::AlignTextToFramePadding();
    if (given) {
        ImGui::TextUnformatted(name.c_str());
    } else {
        ImGui::TextColored(kDefaultColour, "%s", name.c_str());
    }
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

bool ConfigEditor::Draw(Json& config) {
    if (!config.is_object()) {
        config = Json::object();
    }
    ImGui::PushID("config");
    const bool changed = DrawFields(config, schema_["root"]["fields"]);
    ImGui::PopID();
    openAll_.reset();
    return changed;
}

void ConfigEditor::NextNodeOpen() const {
    if (openAll_) {
        ImGui::SetNextItemOpen(*openAll_);
    }
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
    ImGui::PushID(name.c_str());
    const bool changed = DrawValue(name, value, node, given);
    bool reset = false;
    if (given && ImGui::BeginPopupContextItem("reset")) {
        if (ImGui::MenuItem("Reset to default")) {
            reset = true;
        }
        ImGui::EndPopup();
    }
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

bool ConfigEditor::DrawValue(const std::string& label, Json& value, const Json& node, bool given) {
    const Json& resolved = Resolve(node);
    const std::string kind = resolved["kind"];
    if (kind == "object") {
        bool changed = false;
        if (!value.is_object()) {
            value = Json::object();
        }
        if (given) {
            ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        }
        NextNodeOpen();
        if (ImGui::TreeNode(label.c_str())) {
            changed = DrawFields(value, resolved["fields"]);
            ImGui::TreePop();
        }
        return changed;
    }
    if (kind == "choice") {
        return DrawChoice(label, value, resolved);
    }
    if (kind == "array") {
        return DrawArray(label, value, resolved);
    }

    // Leaves: a label, then the widget filling the rest of the row.
    Label(label, given);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::SetNextItemWidth(-1.0f);
    if (kind != "optional") {
        return DrawLeaf(value, resolved);
    }
    bool set = !value.is_null();
    bool changed = false;
    if (ImGui::Checkbox("##set", &set)) {
        value = set ? resolved["default"] : Json(nullptr);
        changed = true;
    }
    ImGui::SetItemTooltip(set ? "Set; clear to leave it unset" : "Unset; check to set it");
    if (!set) {
        return changed;
    }
    const Json& inner = Resolve(resolved["of"]);
    const std::string innerKind = inner["kind"];
    if (innerKind == "object" || innerKind == "choice" || innerKind == "array") {
        ImGui::Indent();
        changed |= DrawValue(label, value, resolved["of"], true);
        ImGui::Unindent();
        return changed;
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    return DrawLeaf(value, inner) || changed;
}

bool ConfigEditor::DrawLeaf(Json& value, const Json& resolved) {
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
        bool changed = false;
        if (ImGui::BeginCombo("##value", current.c_str())) {
            for (const Json& choice : resolved["choices"]) {
                const std::string name = choice;
                if (ImGui::Selectable(name.c_str(), name == current)) {
                    value = name;
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
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
    if (kind == "integer") {
        std::int64_t number = value.is_number() ? value.get<std::int64_t>() : 0;
        if (ImGui::InputScalar("##value", ImGuiDataType_S64, &number, nullptr, nullptr, "%lld",
                               ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (resolved.contains("minimum")) {
                number = std::max<std::int64_t>(number, resolved["minimum"].get<std::int64_t>());
            }
            value = number;
            return true;
        }
        return false;
    }
    double number = value.is_number() ? value.get<double>() : 0.0;
    if (ImGui::InputDouble("##value", &number, 0.0, 0.0, "%g",
                           ImGuiInputTextFlags_EnterReturnsTrue)) {
        value = number;
        return true;
    }
    return false;
}

bool ConfigEditor::DrawChoice(const std::string& label, Json& value, const Json& choice) {
    bool changed = false;
    const std::string type = TypeOf(value, choice);
    if (!value.is_object()) {
        value = Json{{"type", type}};
    }
    NextNodeOpen();
    const bool open = ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_AllowOverlap);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.42f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::PushID(label.c_str());
    if (ImGui::BeginCombo("##type", type.c_str())) {
        for (const Json& alternative : choice["alternatives"]) {
            const std::string name = alternative["type"];
            if (ImGui::Selectable(name.c_str(), name == type)) {
                SetType(value, choice, name);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    if (open) {
        if (const Json* alternative = AlternativeOf(choice, TypeOf(value, choice))) {
            value["type"] = (*alternative)["type"];
            changed |= DrawFields(value, (*alternative)["fields"]);
        }
        ImGui::TreePop();
    }
    return changed;
}

bool ConfigEditor::DrawArray(const std::string& label, Json& value, const Json& node) {
    if (!value.is_array()) {
        value = Json::array();
    }
    bool changed = false;
    const std::string title = label + " (" + std::to_string(value.size()) + ")";
    NextNodeOpen();
    if (!ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_DefaultOpen, "%s", title.c_str())) {
        return false;
    }
    const Json& of = node["of"];
    const Json& item = Resolve(of);
    std::optional<std::size_t> remove;
    std::optional<std::pair<std::size_t, std::size_t>> swap;
    for (std::size_t i = 0; i < value.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton("^") && i > 0) {
            swap = std::pair{i - 1, i};
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("v") && i + 1 < value.size()) {
            swap = std::pair{i, i + 1};
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("x")) {
            remove = i;
        }
        ImGui::SameLine();
        changed |= DrawValue(ItemLabel(value[i], i), value[i], of, true);
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
    if (item["kind"] == "choice") {
        if (ImGui::BeginCombo("##add", "Add...", ImGuiComboFlags_NoArrowButton)) {
            for (const Json& alternative : item["alternatives"]) {
                const std::string name = alternative["type"];
                if (ImGui::Selectable(name.c_str())) {
                    value.push_back(Json{{"type", name}});
                    changed = true;
                }
            }
            ImGui::EndCombo();
        }
    } else if (ImGui::SmallButton("Add")) {
        value.push_back(item["kind"] == "object" ? Json::object() : node["default"]);
        changed = true;
    }
    ImGui::TreePop();
    return changed;
}

}  // namespace terraingen::viewer
