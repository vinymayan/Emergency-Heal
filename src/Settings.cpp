#include "Settings.h"
#include "Manager.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace ImGui = ImGuiMCP;

namespace
{
    constexpr const char* kModDirectory = "Data/Viny Mods/Emergency Heal";
    constexpr const char* kSettingsPath = "Data/Viny Mods/Emergency Heal/Settings.json";
    constexpr const char* kLanguagePath = "Data/Viny Mods/Emergency Heal/Language.json";

    std::unordered_map<std::string, std::string> g_language;

    std::string ToLower(std::string a_value)
    {
        std::transform(a_value.begin(), a_value.end(), a_value.begin(),
            [](unsigned char a_char) { return static_cast<char>(std::tolower(a_char)); });
        return a_value;
    }

    void WriteString(rapidjson::Value& a_parent, rapidjson::Document::AllocatorType& a_alloc,
        const char* a_name, const std::string& a_value)
    {
        rapidjson::Value value;
        value.SetString(a_value.c_str(), static_cast<rapidjson::SizeType>(a_value.size()), a_alloc);
        a_parent.AddMember(rapidjson::StringRef(a_name), value, a_alloc);
    }

    std::string FormToString(RE::FormID a_formID)
    {
        return FormUtil::NormalizeFormID(RE::TESForm::LookupByID(a_formID));
    }

    void WriteForm(rapidjson::Value& a_parent, rapidjson::Document::AllocatorType& a_alloc,
        const char* a_name, RE::FormID a_formID)
    {
        WriteString(a_parent, a_alloc, a_name, FormToString(a_formID));
    }

    void ReadForm(const rapidjson::Value& a_parent, const char* a_name, RE::FormID& a_formID)
    {
        if (a_parent.HasMember(a_name) && a_parent[a_name].IsString()) {
            a_formID = FormUtil::FormIDFromString(a_parent[a_name].GetString());
        }
    }

    rapidjson::Value SerializeSourcedValue(
        const EmergencyHeal::Settings::SourcedValue& a_value,
        rapidjson::Document::AllocatorType& a_alloc)
    {
        rapidjson::Value object(rapidjson::kObjectType);
        object.AddMember("fixedValue", a_value.fixedValue, a_alloc);
        object.AddMember("source", static_cast<int>(a_value.source), a_alloc);
        WriteString(object, a_alloc, "actorValue", a_value.actorValue);
        WriteForm(object, a_alloc, "global", a_value.global);
        return object;
    }

    void DeserializeSourcedValue(const rapidjson::Value& a_object,
        EmergencyHeal::Settings::SourcedValue& a_value)
    {
        if (!a_object.IsObject()) {
            return;
        }
        if (a_object.HasMember("fixedValue") && a_object["fixedValue"].IsNumber()) {
            a_value.fixedValue = a_object["fixedValue"].GetFloat();
        }
        if (a_object.HasMember("source") && a_object["source"].IsInt()) {
            a_value.source = static_cast<EmergencyHeal::Settings::ValueSource>(
                std::clamp(a_object["source"].GetInt(), 0, 2));
        }
        if (a_object.HasMember("actorValue") && a_object["actorValue"].IsString()) {
            a_value.actorValue = a_object["actorValue"].GetString();
        }
        ReadForm(a_object, "global", a_value.global);
    }

    rapidjson::Value SerializeCooldown(
        const EmergencyHeal::Settings::Cooldown& a_cooldown,
        rapidjson::Document::AllocatorType& a_alloc)
    {
        rapidjson::Value object(rapidjson::kObjectType);
        object.AddMember("duration", SerializeSourcedValue(a_cooldown.duration, a_alloc), a_alloc);
        object.AddMember("unit", static_cast<int>(a_cooldown.unit), a_alloc);
        object.AddMember("clock", static_cast<int>(a_cooldown.clock), a_alloc);
        return object;
    }

    void DeserializeCooldown(const rapidjson::Value& a_object,
        EmergencyHeal::Settings::Cooldown& a_cooldown)
    {
        if (!a_object.IsObject()) {
            return;
        }
        if (a_object.HasMember("duration")) {
            DeserializeSourcedValue(a_object["duration"], a_cooldown.duration);
        }
        if (a_object.HasMember("unit") && a_object["unit"].IsInt()) {
            a_cooldown.unit = static_cast<EmergencyHeal::Settings::CooldownUnit>(
                std::clamp(a_object["unit"].GetInt(), 0, 2));
        }
        if (a_object.HasMember("clock") && a_object["clock"].IsInt()) {
            a_cooldown.clock = static_cast<EmergencyHeal::Settings::CooldownClock>(
                std::clamp(a_object["clock"].GetInt(), 0, 1));
        }
    }

    bool DrawDropdown(const char* a_label, const std::string& a_category,
        RE::FormID& a_currentFormID, float a_width = 340.0F)
    {
        const auto& list = Manager::GetSingleton()->GetList(a_category);
        const char* preview = EmergencyHealMenu::GetLoc("common.none", "None");
        for (const auto& form : list) {
            if (form.formID == a_currentFormID) {
                preview = form.cachedDisplayName.c_str();
                break;
            }
        }

        bool changed = false;
        ImGui::PushID(a_label);
        std::string visibleLabel = a_label;
        if (const auto hash = visibleLabel.find("##"); hash != std::string::npos) {
            visibleLabel.resize(hash);
        }
        ImGui::Text("%s:", visibleLabel.c_str());
        ImGui::SameLine();
        ImGui::SetNextItemWidth(a_width);
        if (ImGui::BeginCombo("##dropdown", preview)) {
            static std::map<std::string, std::string> searches;
            char buffer[256]{};
            strcpy_s(buffer, searches[a_label].c_str());
            ImGui::SetNextItemWidth(-1.0F);
            if (ImGui::InputText("##search", buffer, sizeof(buffer))) {
                searches[a_label] = buffer;
            }
            ImGui::Separator();

            const auto search = ToLower(buffer);
            ImGui::BeginChild("##items", ImGui::ImVec2(0.0F, 220.0F), false);
            const bool noneSelected = a_currentFormID == 0;
            if (ImGui::Selectable(EmergencyHealMenu::GetLoc("common.none", "None"), noneSelected)) {
                a_currentFormID = 0;
                searches[a_label].clear();
                changed = true;
            }
            for (const auto& form : list) {
                if (!search.empty() && ToLower(form.cachedDisplayName).find(search) == std::string::npos) {
                    continue;
                }
                const bool selected = form.formID == a_currentFormID;
                if (ImGui::Selectable(form.cachedDisplayName.c_str(), selected)) {
                    a_currentFormID = form.formID;
                    searches[a_label].clear();
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndChild();
            ImGui::EndCombo();
        }
        ImGui::PopID();
        return changed;
    }

    const char* SourceLabel(EmergencyHeal::Settings::ValueSource a_source)
    {
        using enum EmergencyHeal::Settings::ValueSource;
        switch (a_source) {
        case kFixed:
            return EmergencyHealMenu::GetLoc("menu.source_fixed", "Fixed value");
        case kActorValue:
            return EmergencyHealMenu::GetLoc("menu.source_actor_value", "Actor Value");
        case kGlobal:
            return EmergencyHealMenu::GetLoc("menu.source_global", "Global");
        default:
            return EmergencyHealMenu::GetLoc("menu.source_fixed", "Fixed value");
        }
    }

    bool DrawSource(const char* a_label, EmergencyHeal::Settings::ValueSource& a_source)
    {
        bool changed = false;
        if (ImGui::BeginCombo(a_label, SourceLabel(a_source))) {
            for (int value = 0; value <= 2; ++value) {
                const auto source = static_cast<EmergencyHeal::Settings::ValueSource>(value);
                const bool selected = source == a_source;
                if (ImGui::Selectable(SourceLabel(source), selected)) {
                    a_source = source;
                    changed = true;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        return changed;
    }

    bool RenderStringInput(const char* a_label, std::string& a_value)
    {
        char buffer[128]{};
        strcpy_s(buffer, a_value.c_str());
        if (ImGui::InputText(a_label, buffer, sizeof(buffer))) {
            a_value = buffer;
            return true;
        }
        return false;
    }

    bool RenderFloatSliderWithInput(const char* a_label, float& a_value, float a_min, float a_max,
        const char* a_format = "%.2f")
    {
        bool changed = false;
        ImGui::PushID(a_label);
        ImGui::SetNextItemWidth(200.0F);
        if (ImGui::SliderFloat("##slider", &a_value, a_min, a_max, a_format)) {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0F);
        if (ImGui::InputFloat(a_label, &a_value, 0.0F, 0.0F, a_format)) {
            changed = true;
        }
        const float clamped = std::clamp(a_value, a_min, a_max);
        if (clamped != a_value) {
            a_value = clamped;
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool RenderIntSliderWithInput(const char* a_label, float& a_value, int a_min, int a_max)
    {
        int integer = static_cast<int>(std::round(a_value));
        bool changed = false;
        ImGui::PushID(a_label);
        ImGui::SetNextItemWidth(200.0F);
        if (ImGui::SliderInt("##slider", &integer, a_min, a_max)) {
            changed = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0F);
        if (ImGui::InputInt(a_label, &integer)) {
            changed = true;
        }
        integer = std::clamp(integer, a_min, a_max);
        if (changed || a_value != static_cast<float>(integer)) {
            a_value = static_cast<float>(integer);
            changed = true;
        }
        ImGui::PopID();
        return changed;
    }

    bool RenderSourcedValue(const char* a_id, const char* a_label,
        EmergencyHeal::Settings::SourcedValue& a_value, float a_min, float a_max,
        bool a_integerFixed = false)
    {
        bool changed = false;
        ImGui::PushID(a_id);
        const std::string sourceLabel = std::string(a_label) + " " +
            EmergencyHealMenu::GetLoc("menu.source", "source");
        if (DrawSource(sourceLabel.c_str(), a_value.source)) {
            changed = true;
        }

        switch (a_value.source) {
        case EmergencyHeal::Settings::ValueSource::kFixed:
            if (a_integerFixed) {
                changed |= RenderIntSliderWithInput(a_label, a_value.fixedValue,
                    static_cast<int>(a_min), static_cast<int>(a_max));
            } else {
                changed |= RenderFloatSliderWithInput(a_label, a_value.fixedValue, a_min, a_max);
            }
            break;
        case EmergencyHeal::Settings::ValueSource::kActorValue:
            changed |= RenderStringInput(EmergencyHealMenu::GetLoc("menu.actor_value", "Actor Value name"),
                a_value.actorValue);
            break;
        case EmergencyHeal::Settings::ValueSource::kGlobal:
            changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.global", "Global"), "Global", a_value.global);
            break;
        }
        ImGui::PopID();
        return changed;
    }

    bool RenderCooldown(const char* a_id, EmergencyHeal::Settings::Cooldown& a_cooldown)
    {
        bool changed = false;
        ImGui::PushID(a_id);

        const char* clocks[] = {
            EmergencyHealMenu::GetLoc("menu.cooldown_real_time", "Real time"),
            EmergencyHealMenu::GetLoc("menu.cooldown_game_time", "Game time")
        };
        int clock = static_cast<int>(a_cooldown.clock);
        if (ImGui::Combo(EmergencyHealMenu::GetLoc("menu.cooldown_clock", "Cooldown clock"),
                &clock, clocks, 2)) {
            a_cooldown.clock = static_cast<EmergencyHeal::Settings::CooldownClock>(clock);
            changed = true;
        }

        const char* units[] = {
            EmergencyHealMenu::GetLoc("menu.cooldown_seconds", "Seconds"),
            EmergencyHealMenu::GetLoc("menu.cooldown_minutes", "Minutes"),
            EmergencyHealMenu::GetLoc("menu.cooldown_hours", "Hours")
        };
        int unit = static_cast<int>(a_cooldown.unit);
        if (ImGui::Combo(EmergencyHealMenu::GetLoc("menu.cooldown_unit", "Cooldown unit"),
                &unit, units, 3)) {
            a_cooldown.unit = static_cast<EmergencyHeal::Settings::CooldownUnit>(unit);
            changed = true;
        }

        changed |= RenderSourcedValue("duration",
            EmergencyHealMenu::GetLoc("menu.cooldown_duration", "Cooldown duration"),
            a_cooldown.duration, 0.0F, 1000000.0F);
        ImGui::TextWrapped("%s", EmergencyHealMenu::GetLoc("menu.cooldown_zero_hint",
            "A duration of 0 disables this cooldown."));

        ImGui::PopID();
        return changed;
    }

    void GameplayRender()
    {
        auto& config = EmergencyHeal::Settings::Get();
        bool changed = false;

        changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.enabled", "Enable Emergency Heal"), &config.enabled);
        changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.required_perk", "Lock the system behind a perk"),
            "Perk", config.requiredPerk);
        changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.disabling_perk",
            "Disable the system when the player has this perk"), "Perk", config.disablingPerk);

        if (ImGui::CollapsingHeader(EmergencyHealMenu::GetLoc("menu.trigger_header", "Health trigger"))) {
            ImGui::Indent();
            const char* modes[] = {
                EmergencyHealMenu::GetLoc("menu.threshold_flat", "Flat health"),
                EmergencyHealMenu::GetLoc("menu.threshold_percent", "Percentage of maximum health")
            };
            int mode = static_cast<int>(config.thresholdMode);
            if (ImGui::Combo(EmergencyHealMenu::GetLoc("menu.threshold_mode", "Threshold mode"), &mode, modes, 2)) {
                config.thresholdMode = static_cast<EmergencyHeal::Settings::ThresholdMode>(mode);
                changed = true;
            }
            const float maximum = config.thresholdMode == EmergencyHeal::Settings::ThresholdMode::kPercent ?
                100.0F : 10000.0F;
            changed |= RenderSourcedValue("health_threshold",
                EmergencyHealMenu::GetLoc("menu.health_threshold", "Trigger at or below"),
                config.healthThreshold, 0.0F, maximum);

            ImGui::Separator();
            changed |= RenderSourcedValue("usage_period",
                EmergencyHealMenu::GetLoc("menu.usage_period", "Usage window (game days)"),
                config.usagePeriodDays, 1.0F, 365.0F);
            changed |= RenderSourcedValue("uses_per_day",
                EmergencyHealMenu::GetLoc("menu.activations_per_day",
                    "Health trigger activations per game day"),
                config.activationsPerDay, 0.0F, 1000.0F, true);
            ImGui::TextWrapped("%s", EmergencyHealMenu::GetLoc("menu.health_frequency_hint",
                "The total trigger limit is activations per day multiplied by the usage window."));
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader(EmergencyHealMenu::GetLoc(
                "menu.potion_header", "Emergency healing potion"))) {
            ImGui::Indent();
            changed |= RenderSourcedValue("potion_uses_per_day",
                EmergencyHealMenu::GetLoc("menu.potion_activations_per_day",
                    "Potion activations per game day"),
                config.potionActivationsPerDay, 0.0F, 1000.0F, true);
            changed |= RenderCooldown("potion_cooldown", config.potionCooldown);
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader(EmergencyHealMenu::GetLoc("menu.fatal_header", "Fatal damage"))) {
            ImGui::Indent();
            changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.protect_fatal",
                "At a 0/0% threshold, leave the player at 1 health"),
                &config.protectFatalDamageAtZeroThreshold);
            changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.protect_fatal_any_threshold",
                "Protect fatal damage at any threshold"),
                &config.protectFatalDamageAtAnyThreshold);
            if (config.protectFatalDamageAtZeroThreshold || config.protectFatalDamageAtAnyThreshold) {
                changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc(
                    "menu.prevent_fatal_killmoves",
                    "Prevent fatal killmoves before they start"),
                    &config.preventFatalKillmoves);
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.fatal_perk", "Lock fatal protection behind a perk"),
                    "Perk", config.fatalProtectionPerk);
            }
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader(EmergencyHealMenu::GetLoc("menu.magic_header", "Emergency healing magic"))) {
            ImGui::Indent();
            changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.cast_magic_no_items",
                "Cast emergency magic if no items"), &config.castEmergencyMagicIfNoItems);
            if (config.castEmergencyMagicIfNoItems) {
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.magic_perk", "Lock emergency magic behind a perk"),
                    "Perk", config.emergencyMagicPerk);
                changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.cast_without_mana",
                    "Cast even without mana"), &config.castEvenWithoutMana);
                if (config.castEvenWithoutMana) {
                    changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.no_mana_perk", "Lock no-mana casting behind a perk"),
                        "Perk", config.noManaCastPerk);
                }
                ImGui::Separator();
                changed |= RenderSourcedValue("magic_uses_per_day",
                    EmergencyHealMenu::GetLoc("menu.magic_activations_per_day",
                        "Magic activations per game day"),
                    config.magicActivationsPerDay, 0.0F, 1000.0F, true);
                changed |= RenderCooldown("magic_cooldown", config.magicCooldown);
            }
            ImGui::Unindent();
        }

        if (ImGui::CollapsingHeader(EmergencyHealMenu::GetLoc(
                "menu.undeserved_mercy_header", "Undeserved Mercy"))) {
            ImGui::Indent();
            changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.undeserved_mercy_enabled",
                "Use Undeserved Mercy when no potion or healing magic can be used"), &config.godMercyEnabled);
            if (config.godMercyEnabled) {
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.undeserved_mercy_perk",
                    "Lock Undeserved Mercy behind a perk"), "Perk", config.godMercyPerk);

                const char* healModes[] = {
                    EmergencyHealMenu::GetLoc("menu.heal_flat", "Flat health"),
                    EmergencyHealMenu::GetLoc("menu.heal_percent", "Percentage of maximum health")
                };
                int healMode = static_cast<int>(config.godMercyHealMode);
                if (ImGui::Combo(EmergencyHealMenu::GetLoc("menu.undeserved_mercy_heal_mode",
                    "Undeserved Mercy healing mode"), &healMode, healModes, 2)) {
                    config.godMercyHealMode =
                        static_cast<EmergencyHeal::Settings::ThresholdMode>(healMode);
                    changed = true;
                }

                const float maximum =
                    config.godMercyHealMode == EmergencyHeal::Settings::ThresholdMode::kPercent ?
                    100.0F : 10000.0F;
                changed |= RenderSourcedValue("god_mercy_heal",
                    EmergencyHealMenu::GetLoc(
                        "menu.undeserved_mercy_heal_amount", "Undeserved Mercy healing amount"),
                    config.godMercyHealAmount, 0.0F, maximum);
                ImGui::Separator();
                changed |= RenderSourcedValue("favor_uses_per_day",
                    EmergencyHealMenu::GetLoc("menu.undeserved_mercy_activations_per_day",
                        "Undeserved Mercy activations per game day"),
                    config.godMercyActivationsPerDay, 0.0F, 1000.0F, true);
                changed |= RenderCooldown("undeserved_mercy_cooldown", config.godMercyCooldown);
            }
            ImGui::Unindent();
        }

        if (changed) {
            EmergencyHealMenu::SaveSettings();
        }
    }

    void AdditionalSpellsRender()
    {
        auto& spells = EmergencyHeal::Settings::Get().additionalSpells;
        bool changed = false;

        ImGui::TextWrapped("%s", EmergencyHealMenu::GetLoc("menu.effects_hint",
            "These spells are cast on the player whenever Emergency Heal triggers. They do not consume magicka."));
        if (ImGui::Button(EmergencyHealMenu::GetLoc("menu.add_effect", "Add buff/debuff"))) {
            spells.emplace_back();
            changed = true;
        }
        ImGui::Separator();

        std::optional<std::size_t> removeIndex;
        for (std::size_t index = 0; index < spells.size(); ++index) {
            auto& entry = spells[index];
            ImGui::PushID(static_cast<int>(index));
            std::string header = std::format("{} {}##effect{}",
                EmergencyHealMenu::GetLoc("menu.effect", "Effect"), index + 1, index);
            if (ImGui::CollapsingHeader(header.c_str())) {
                ImGui::Indent();
                changed |= ImGui::Checkbox(EmergencyHealMenu::GetLoc("menu.effect_enabled", "Enabled"), &entry.enabled);
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.effect_spell", "Spell"), "Spell", entry.spell);
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.effect_perk", "Lock this effect behind a perk"),
                    "Perk", entry.requiredPerk);
                changed |= DrawDropdown(EmergencyHealMenu::GetLoc("menu.effect_disabling_perk",
                    "Disable this effect when the player has this perk"), "Perk", entry.disablingPerk);
                if (ImGui::Button(EmergencyHealMenu::GetLoc("menu.remove_effect", "Remove"))) {
                    removeIndex = index;
                }
                ImGui::Unindent();
            }
            ImGui::PopID();
        }

        if (removeIndex) {
            spells.erase(spells.begin() + static_cast<std::ptrdiff_t>(*removeIndex));
            changed = true;
        }
        if (changed) {
            EmergencyHealMenu::SaveSettings();
        }
    }
}

namespace EmergencyHeal::Settings
{
    Config& Get()
    {
        static Config config;
        return config;
    }

    float ResolveValue(RE::Actor* a_actor, const SourcedValue& a_value)
    {
        switch (a_value.source) {
        case ValueSource::kActorValue:
            if (a_actor && !a_value.actorValue.empty()) {
                const auto actorValue = RE::ActorValueList::LookupActorValueByName(a_value.actorValue.c_str());
                if (actorValue != RE::ActorValue::kNone) {
                    if (auto* owner = a_actor->AsActorValueOwner()) {
                        return owner->GetActorValue(actorValue);
                    }
                }
            }
            break;
        case ValueSource::kGlobal:
            if (auto* global = RE::TESForm::LookupByID<RE::TESGlobal>(a_value.global)) {
                return global->value;
            }
            break;
        case ValueSource::kFixed:
        default:
            break;
        }
        return a_value.fixedValue;
    }

    bool HasRequiredPerk(RE::Actor* a_actor, RE::FormID a_perk)
    {
        if (a_perk == 0) {
            return true;
        }
        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(a_perk);
        return a_actor && perk && a_actor->HasPerk(perk);
    }

    bool IsDisabledByPerk(RE::Actor* a_actor, RE::FormID a_perk)
    {
        if (a_perk == 0) {
            return false;
        }
        auto* perk = RE::TESForm::LookupByID<RE::BGSPerk>(a_perk);
        return a_actor && perk && a_actor->HasPerk(perk);
    }
}

namespace EmergencyHealMenu
{
    const char* GetLoc(const std::string& a_key, const char* a_fallback)
    {
        if (const auto it = g_language.find(a_key); it != g_language.end()) {
            return it->second.c_str();
        }
        return a_fallback;
    }

    void LoadLanguage()
    {
        g_language.clear();
        std::ifstream file(kLanguagePath, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
            static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF) {
            json.erase(0, 3);
        }

        rapidjson::Document document;
        document.Parse(json.c_str());
        if (!document.IsObject()) {
            return;
        }
        for (auto member = document.MemberBegin(); member != document.MemberEnd(); ++member) {
            if (member->value.IsString()) {
                g_language[member->name.GetString()] = member->value.GetString();
            } else if (member->value.IsObject()) {
                for (auto child = member->value.MemberBegin(); child != member->value.MemberEnd(); ++child) {
                    if (child->value.IsString()) {
                        g_language[std::string(member->name.GetString()) + "." + child->name.GetString()] =
                            child->value.GetString();
                    }
                }
            }
        }
    }

    void LoadSettings()
    {
        auto& config = EmergencyHeal::Settings::Get();
        config = {};

        std::ifstream file(kSettingsPath, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json = buffer.str();
        if (json.size() >= 3 && static_cast<unsigned char>(json[0]) == 0xEF &&
            static_cast<unsigned char>(json[1]) == 0xBB && static_cast<unsigned char>(json[2]) == 0xBF) {
            json.erase(0, 3);
        }

        rapidjson::Document document;
        document.Parse(json.c_str());
        if (!document.IsObject()) {
            logger::error("Emergency Heal: Settings.json invalido.");
            return;
        }

        if (document.HasMember("enabled") && document["enabled"].IsBool()) config.enabled = document["enabled"].GetBool();
        if (document.HasMember("healthThreshold")) DeserializeSourcedValue(document["healthThreshold"], config.healthThreshold);
        if (document.HasMember("thresholdMode") && document["thresholdMode"].IsInt()) {
            config.thresholdMode = static_cast<EmergencyHeal::Settings::ThresholdMode>(
                std::clamp(document["thresholdMode"].GetInt(), 0, 1));
        }
        if (document.HasMember("usagePeriodDays")) DeserializeSourcedValue(document["usagePeriodDays"], config.usagePeriodDays);
        if (document.HasMember("activationsPerDay")) DeserializeSourcedValue(document["activationsPerDay"], config.activationsPerDay);
        if (document.HasMember("potionActivationsPerDay")) {
            DeserializeSourcedValue(document["potionActivationsPerDay"], config.potionActivationsPerDay);
        }
        if (document.HasMember("magicActivationsPerDay")) {
            DeserializeSourcedValue(document["magicActivationsPerDay"], config.magicActivationsPerDay);
        }
        if (document.HasMember("godMercyActivationsPerDay")) {
            DeserializeSourcedValue(document["godMercyActivationsPerDay"], config.godMercyActivationsPerDay);
        }
        const bool hasChannelCooldowns =
            document.HasMember("potionCooldown") ||
            document.HasMember("magicCooldown") ||
            document.HasMember("godMercyCooldown");
        if (document.HasMember("potionCooldown")) {
            DeserializeCooldown(document["potionCooldown"], config.potionCooldown);
        }
        if (document.HasMember("magicCooldown")) {
            DeserializeCooldown(document["magicCooldown"], config.magicCooldown);
        }
        if (document.HasMember("godMercyCooldown")) {
            DeserializeCooldown(document["godMercyCooldown"], config.godMercyCooldown);
        }
        if (!hasChannelCooldowns && document.HasMember("triggerCooldownSeconds")) {
            EmergencyHeal::Settings::SourcedValue legacyCooldown;
            DeserializeSourcedValue(document["triggerCooldownSeconds"], legacyCooldown);
            config.potionCooldown.duration = legacyCooldown;
            config.magicCooldown.duration = legacyCooldown;
            config.godMercyCooldown.duration = legacyCooldown;
            logger::info(
                "Emergency Heal: cooldown geral anterior aplicado aos tres canais.");
        }
        ReadForm(document, "requiredPerk", config.requiredPerk);
        ReadForm(document, "disablingPerk", config.disablingPerk);
        if (document.HasMember("protectFatalDamageAtZeroThreshold") && document["protectFatalDamageAtZeroThreshold"].IsBool()) {
            config.protectFatalDamageAtZeroThreshold = document["protectFatalDamageAtZeroThreshold"].GetBool();
        }
        if (document.HasMember("protectFatalDamageAtAnyThreshold") && document["protectFatalDamageAtAnyThreshold"].IsBool()) {
            config.protectFatalDamageAtAnyThreshold = document["protectFatalDamageAtAnyThreshold"].GetBool();
        }
        if (document.HasMember("preventFatalKillmoves") && document["preventFatalKillmoves"].IsBool()) {
            config.preventFatalKillmoves = document["preventFatalKillmoves"].GetBool();
        }
        ReadForm(document, "fatalProtectionPerk", config.fatalProtectionPerk);
        if (document.HasMember("castEmergencyMagicIfNoItems") && document["castEmergencyMagicIfNoItems"].IsBool()) {
            config.castEmergencyMagicIfNoItems = document["castEmergencyMagicIfNoItems"].GetBool();
        }
        ReadForm(document, "emergencyMagicPerk", config.emergencyMagicPerk);
        if (document.HasMember("castEvenWithoutMana") && document["castEvenWithoutMana"].IsBool()) {
            config.castEvenWithoutMana = document["castEvenWithoutMana"].GetBool();
        }
        ReadForm(document, "noManaCastPerk", config.noManaCastPerk);
        if (document.HasMember("godMercyEnabled") && document["godMercyEnabled"].IsBool()) {
            config.godMercyEnabled = document["godMercyEnabled"].GetBool();
        }
        ReadForm(document, "godMercyPerk", config.godMercyPerk);
        if (document.HasMember("godMercyHealAmount")) {
            DeserializeSourcedValue(document["godMercyHealAmount"], config.godMercyHealAmount);
        }
        if (document.HasMember("godMercyHealMode") && document["godMercyHealMode"].IsInt()) {
            config.godMercyHealMode = static_cast<EmergencyHeal::Settings::ThresholdMode>(
                std::clamp(document["godMercyHealMode"].GetInt(), 0, 1));
        }

        if (document.HasMember("additionalSpells") && document["additionalSpells"].IsArray()) {
            config.additionalSpells.clear();
            for (const auto& value : document["additionalSpells"].GetArray()) {
                if (!value.IsObject()) continue;
                EmergencyHeal::Settings::AdditionalSpell entry;
                if (value.HasMember("enabled") && value["enabled"].IsBool()) entry.enabled = value["enabled"].GetBool();
                ReadForm(value, "spell", entry.spell);
                ReadForm(value, "requiredPerk", entry.requiredPerk);
                ReadForm(value, "disablingPerk", entry.disablingPerk);
                config.additionalSpells.push_back(entry);
            }
        }

        config.healthThreshold.fixedValue = std::max(0.0F, config.healthThreshold.fixedValue);
        config.usagePeriodDays.fixedValue = std::clamp(config.usagePeriodDays.fixedValue, 1.0F, 365.0F);
        config.activationsPerDay.fixedValue = std::clamp(config.activationsPerDay.fixedValue, 0.0F, 1000.0F);
        config.potionActivationsPerDay.fixedValue =
            std::clamp(config.potionActivationsPerDay.fixedValue, 0.0F, 1000.0F);
        config.magicActivationsPerDay.fixedValue =
            std::clamp(config.magicActivationsPerDay.fixedValue, 0.0F, 1000.0F);
        config.godMercyActivationsPerDay.fixedValue =
            std::clamp(config.godMercyActivationsPerDay.fixedValue, 0.0F, 1000.0F);
        config.potionCooldown.duration.fixedValue =
            std::max(0.0F, config.potionCooldown.duration.fixedValue);
        config.magicCooldown.duration.fixedValue =
            std::max(0.0F, config.magicCooldown.duration.fixedValue);
        config.godMercyCooldown.duration.fixedValue =
            std::max(0.0F, config.godMercyCooldown.duration.fixedValue);
        config.godMercyHealAmount.fixedValue = std::max(0.0F, config.godMercyHealAmount.fixedValue);
    }

    void SaveSettings()
    {
        std::filesystem::create_directories(kModDirectory);
        const auto& config = EmergencyHeal::Settings::Get();

        rapidjson::Document document;
        document.SetObject();
        auto& alloc = document.GetAllocator();
        document.AddMember("enabled", config.enabled, alloc);
        document.AddMember("healthThreshold", SerializeSourcedValue(config.healthThreshold, alloc), alloc);
        document.AddMember("thresholdMode", static_cast<int>(config.thresholdMode), alloc);
        document.AddMember("usagePeriodDays", SerializeSourcedValue(config.usagePeriodDays, alloc), alloc);
        document.AddMember("activationsPerDay", SerializeSourcedValue(config.activationsPerDay, alloc), alloc);
        document.AddMember("potionActivationsPerDay",
            SerializeSourcedValue(config.potionActivationsPerDay, alloc), alloc);
        document.AddMember("magicActivationsPerDay",
            SerializeSourcedValue(config.magicActivationsPerDay, alloc), alloc);
        document.AddMember("godMercyActivationsPerDay",
            SerializeSourcedValue(config.godMercyActivationsPerDay, alloc), alloc);
        document.AddMember("potionCooldown", SerializeCooldown(config.potionCooldown, alloc), alloc);
        document.AddMember("magicCooldown", SerializeCooldown(config.magicCooldown, alloc), alloc);
        document.AddMember("godMercyCooldown", SerializeCooldown(config.godMercyCooldown, alloc), alloc);
        WriteForm(document, alloc, "requiredPerk", config.requiredPerk);
        WriteForm(document, alloc, "disablingPerk", config.disablingPerk);
        document.AddMember("protectFatalDamageAtZeroThreshold", config.protectFatalDamageAtZeroThreshold, alloc);
        document.AddMember("protectFatalDamageAtAnyThreshold", config.protectFatalDamageAtAnyThreshold, alloc);
        document.AddMember("preventFatalKillmoves", config.preventFatalKillmoves, alloc);
        WriteForm(document, alloc, "fatalProtectionPerk", config.fatalProtectionPerk);
        document.AddMember("castEmergencyMagicIfNoItems", config.castEmergencyMagicIfNoItems, alloc);
        WriteForm(document, alloc, "emergencyMagicPerk", config.emergencyMagicPerk);
        document.AddMember("castEvenWithoutMana", config.castEvenWithoutMana, alloc);
        WriteForm(document, alloc, "noManaCastPerk", config.noManaCastPerk);
        document.AddMember("godMercyEnabled", config.godMercyEnabled, alloc);
        WriteForm(document, alloc, "godMercyPerk", config.godMercyPerk);
        document.AddMember("godMercyHealAmount", SerializeSourcedValue(config.godMercyHealAmount, alloc), alloc);
        document.AddMember("godMercyHealMode", static_cast<int>(config.godMercyHealMode), alloc);

        rapidjson::Value spells(rapidjson::kArrayType);
        for (const auto& entry : config.additionalSpells) {
            rapidjson::Value value(rapidjson::kObjectType);
            value.AddMember("enabled", entry.enabled, alloc);
            WriteForm(value, alloc, "spell", entry.spell);
            WriteForm(value, alloc, "requiredPerk", entry.requiredPerk);
            WriteForm(value, alloc, "disablingPerk", entry.disablingPerk);
            spells.PushBack(value, alloc);
        }
        document.AddMember("additionalSpells", spells, alloc);

        std::ofstream file(kSettingsPath, std::ios::binary | std::ios::trunc);
        if (!file.is_open()) {
            logger::error("Emergency Heal: nao foi possivel salvar {}.", kSettingsPath);
            return;
        }
        rapidjson::StringBuffer buffer;
        rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
        document.Accept(writer);
        file.write(buffer.GetString(), static_cast<std::streamsize>(buffer.GetSize()));
    }

    void Register()
    {
        static bool registered = false;
        if (registered || !SKSEMenuFramework::IsInstalled()) {
            return;
        }
        registered = true;
        LoadLanguage();
        LoadSettings();
        SKSEMenuFramework::SetSection(GetLoc("menu.section", "Emergency Healing"));
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.gameplay", "Emergency Healing settings"), GameplayRender);
        SKSEMenuFramework::AddSectionItem(GetLoc("menu.effects", "Add buffs or debuffs"), AdditionalSpellsRender);
    }
}
