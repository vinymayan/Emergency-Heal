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

    enum class CooldownUnit : std::uint8_t
    {
        kSeconds = 0,
        kMinutes = 1,
        kHours = 2
    };

    enum class CooldownClock : std::uint8_t
    {
        kRealTime = 0,
        kGameTime = 1
    };

    struct SourcedValue
    {
        float fixedValue = 0.0F;
        ValueSource source = ValueSource::kFixed;
        std::string actorValue = "Health";
        RE::FormID global = 0;
    };

    struct Cooldown
    {
        SourcedValue duration;
        CooldownUnit unit = CooldownUnit::kSeconds;
        CooldownClock clock = CooldownClock::kRealTime;
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
        SourcedValue potionActivationsPerDay{ 1.0F, ValueSource::kFixed, "Variable03", 0 };
        SourcedValue magicActivationsPerDay{ 1.0F, ValueSource::kFixed, "Variable04", 0 };
        SourcedValue godMercyActivationsPerDay{ 1.0F, ValueSource::kFixed, "Variable05", 0 };
        Cooldown potionCooldown{
            { 0.0F, ValueSource::kFixed, "Variable06", 0 },
            CooldownUnit::kSeconds,
            CooldownClock::kRealTime
        };
        Cooldown magicCooldown{
            { 0.0F, ValueSource::kFixed, "Variable07", 0 },
            CooldownUnit::kSeconds,
            CooldownClock::kRealTime
        };
        Cooldown godMercyCooldown{
            { 0.0F, ValueSource::kFixed, "Variable08", 0 },
            CooldownUnit::kSeconds,
            CooldownClock::kRealTime
        };

        RE::FormID requiredPerk = 0;
        RE::FormID disablingPerk = 0;
        bool protectFatalDamageAtZeroThreshold = true;
        bool protectFatalDamageAtAnyThreshold = false;
        bool preventFatalKillmoves = true;
        RE::FormID fatalProtectionPerk = 0;

        bool castEmergencyMagicIfNoItems = true;
        RE::FormID emergencyMagicPerk = 0;
        bool castEvenWithoutMana = false;
        RE::FormID noManaCastPerk = 0;

        bool godMercyEnabled = false;
        RE::FormID godMercyPerk = 0;
        SourcedValue godMercyHealAmount{ 25.0F, ValueSource::kFixed, "Health", 0 };
        ThresholdMode godMercyHealMode = ThresholdMode::kPercent;

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
