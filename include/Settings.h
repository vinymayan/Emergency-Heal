#pragma once

#include "SKSEMCP/SKSEMenuFramework.hpp"

#include <string>
#include <vector>

namespace EmergencyHeal::Settings
{
    enum class ValueSource : std::uint8_t
    {
        kFixed = 0,
        kActorValue = 1,
        kGlobal = 2
    };

    enum class ThresholdMode : std::uint8_t
    {
        kFlat = 0,
        kPercent = 1
    };

    struct SourcedValue
    {
        float fixedValue = 0.0F;
        ValueSource source = ValueSource::kFixed;
        std::string actorValue = "Health";
        RE::FormID global = 0;
    };

    struct AdditionalSpell
    {
        bool enabled = true;
        RE::FormID spell = 0;
        RE::FormID requiredPerk = 0;
        RE::FormID disablingPerk = 0;
    };

    struct Config
    {
        bool enabled = true;
        SourcedValue healthThreshold{ 20.0F, ValueSource::kFixed, "Health", 0 };
        ThresholdMode thresholdMode = ThresholdMode::kPercent;

        // A quota da janela e: activationsPerDay * usagePeriodDays.
        SourcedValue usagePeriodDays{ 1.0F, ValueSource::kFixed, "Variable01", 0 };
        SourcedValue activationsPerDay{ 1.0F, ValueSource::kFixed, "Variable02", 0 };

        RE::FormID requiredPerk = 0;
        RE::FormID disablingPerk = 0;
        bool protectFatalDamageAtZeroThreshold = true;
        RE::FormID fatalProtectionPerk = 0;

        bool castEmergencyMagicIfNoItems = true;
        RE::FormID emergencyMagicPerk = 0;
        bool castEvenWithoutMana = false;
        RE::FormID noManaCastPerk = 0;

        std::vector<AdditionalSpell> additionalSpells;
    };

    Config& Get();
    float ResolveValue(RE::Actor* a_actor, const SourcedValue& a_value);
    bool HasRequiredPerk(RE::Actor* a_actor, RE::FormID a_perk);
    bool IsDisabledByPerk(RE::Actor* a_actor, RE::FormID a_perk);
}

namespace EmergencyHealMenu
{
    void Register();
    void LoadLanguage();
    void LoadSettings();
    void SaveSettings();
    const char* GetLoc(const std::string& a_key, const char* a_fallback);
}
