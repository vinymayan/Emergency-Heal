#include "Serialization.h"
#include "Settings.h"

namespace
{
    constexpr std::uint32_t kUsageRecord = 'USE1';
    constexpr std::uint32_t kVersion = 1;
}

namespace EmergencyHeal::Usage
{
    const char* ChannelName(Channel a_channel)
    {
        switch (a_channel) {
        case Channel::kPotion:
            return "Potion";
        case Channel::kMagic:
            return "Magic";
        case Channel::kGodMercy:
            return "UndeservedMercy";
        default:
            return "Unknown";
        }
    }

    const Settings::SourcedValue& ChannelLimitSetting(Channel a_channel)
    {
        const auto& settings = Settings::Get();
        switch (a_channel) {
        case Channel::kPotion:
            return settings.potionActivationsPerDay;
        case Channel::kMagic:
            return settings.magicActivationsPerDay;
        case Channel::kGodMercy:
            return settings.godMercyActivationsPerDay;
        default:
            return settings.activationsPerDay;
        }
    }

    const Settings::Cooldown& ChannelCooldownSetting(Channel a_channel)
    {
        const auto& settings = Settings::Get();
        switch (a_channel) {
        case Channel::kPotion:
            return settings.potionCooldown;
        case Channel::kMagic:
            return settings.magicCooldown;
        case Channel::kGodMercy:
            return settings.godMercyCooldown;
        default:
            return settings.potionCooldown;
        }
    }

    std::uint32_t& ChannelCounter(State& a_state, Channel a_channel)
    {
        switch (a_channel) {
        case Channel::kPotion:
            return a_state.potionActivationsInWindow;
        case Channel::kMagic:
            return a_state.magicActivationsInWindow;
        case Channel::kGodMercy:
            return a_state.godMercyActivationsInWindow;
        default:
            return a_state.activationsInWindow;
        }
    }

    LastUse& ChannelLastUse(State& a_state, Channel a_channel)
    {
        switch (a_channel) {
        case Channel::kPotion:
            return a_state.potionLastUse;
        case Channel::kMagic:
            return a_state.magicLastUse;
        case Channel::kGodMercy:
            return a_state.godMercyLastUse;
        default:
            return a_state.potionLastUse;
        }
    }

    double CurrentRealTimeSeconds()
    {
        return std::chrono::duration<double>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    double CurrentGameTimeSeconds()
    {
        constexpr double kSecondsPerGameDay = 86400.0;
        if (auto* calendar = RE::Calendar::GetSingleton()) {
            return static_cast<double>(calendar->GetCurrentGameTime()) * kSecondsPerGameDay;
        }
        return -1.0;
    }

    double CooldownDurationSeconds(RE::Actor* a_actor, const Settings::Cooldown& a_cooldown)
    {
        double duration = static_cast<double>(
            Settings::ResolveValue(a_actor, a_cooldown.duration));
        if (!std::isfinite(duration) || duration <= 0.0) {
            return 0.0;
        }

        switch (a_cooldown.unit) {
        case Settings::CooldownUnit::kMinutes:
            duration *= 60.0;
            break;
        case Settings::CooldownUnit::kHours:
            duration *= 3600.0;
            break;
        case Settings::CooldownUnit::kSeconds:
        default:
            break;
        }
        return duration;
    }

    bool IsCooldownReady(RE::Actor* a_actor, Channel a_channel)
    {
        const auto& cooldown = ChannelCooldownSetting(a_channel);
        const double durationSeconds = CooldownDurationSeconds(a_actor, cooldown);
        if (durationSeconds <= 0.0) {
            logger::debug(
                "[EmergencyHeal][Cooldown][{}] Desativado: duracao resolvida={}.",
                ChannelName(a_channel),
                durationSeconds);
            return true;
        }

        auto& lastUse = ChannelLastUse(GetState(), a_channel);
        const bool useGameTime = cooldown.clock == Settings::CooldownClock::kGameTime;
        const double now = useGameTime ? CurrentGameTimeSeconds() : CurrentRealTimeSeconds();
        const double previous = useGameTime ? lastUse.gameTimeSeconds : lastUse.realTimeSeconds;
        if (now < 0.0 || previous < 0.0) {
            logger::debug(
                "[EmergencyHeal][Cooldown][{}] Permitido: ainda nao ha ultimo uso em {}.",
                ChannelName(a_channel),
                useGameTime ? "game time" : "real time");
            return true;
        }
        if (now < previous) {
            logger::debug(
                "[EmergencyHeal][Cooldown][{}] Permitido: relogio retrocedeu (agora={}, ultimo={}).",
                ChannelName(a_channel),
                now,
                previous);
            return true;
        }

        const double elapsedSeconds = now - previous;
        const bool ready = elapsedSeconds >= durationSeconds;
        logger::debug(
            "[EmergencyHeal][Cooldown][{}] clock={}, now={}, last={}, elapsed={}, required={}, "
            "remaining={}, allowed={}.",
            ChannelName(a_channel),
            useGameTime ? "game" : "real",
            now,
            previous,
            elapsedSeconds,
            durationSeconds,
            std::max(0.0, durationSeconds - elapsedSeconds),
            ready);
        return ready;
    }

    State& GetState()
    {
        static State state;
        return state;
    }

    bool CanConsumeActivation(RE::Actor* a_actor)
    {
        auto* calendar = RE::Calendar::GetSingleton();
        if (!calendar || !a_actor) {
            return false;
        }

        const float now = calendar->GetCurrentGameTime();
        const float periodDays = std::max(1.0F,
            Settings::ResolveValue(a_actor, Settings::Get().usagePeriodDays));
        const float usesPerDay = std::max(0.0F,
            Settings::ResolveValue(a_actor, Settings::Get().activationsPerDay));
        const auto quota = static_cast<std::uint32_t>(std::max(0.0F, std::ceil(usesPerDay * periodDays)));
        logger::debug(
            "[EmergencyHeal][Usage] now={}, periodDays={}, usesPerDay={}, quota={}.",
            now, periodDays, usesPerDay, quota);
        if (quota == 0) {
            logger::debug("[EmergencyHeal][Usage] Bloqueado: quota resolvida para zero.");
            return false;
        }

        auto& state = GetState();
        if (state.windowStartGameDays < 0.0F || now < state.windowStartGameDays ||
            now - state.windowStartGameDays >= periodDays) {
            state.windowStartGameDays = now;
            state.activationsInWindow = 0;
            state.potionActivationsInWindow = 0;
            state.magicActivationsInWindow = 0;
            state.godMercyActivationsInWindow = 0;
            logger::debug("[EmergencyHeal][Usage] Nova janela iniciada em {}.", now);
        }
        if (state.activationsInWindow >= quota) {
            logger::debug(
                "[EmergencyHeal][Usage] Bloqueado: ativacoes {} >= quota {} (inicio={}).",
                state.activationsInWindow, quota, state.windowStartGameDays);
            return false;
        }

        return true;
    }

    bool TryConsumeActivation(RE::Actor* a_actor)
    {
        if (!CanConsumeActivation(a_actor)) {
            return false;
        }

        auto& state = GetState();
        ++state.activationsInWindow;
        logger::debug(
            "[EmergencyHeal][Usage] Ativacao consumida; total na janela={} (inicio={}).",
            state.activationsInWindow,
            state.windowStartGameDays);
        return true;
    }

    bool CanUseChannel(RE::Actor* a_actor, Channel a_channel)
    {
        if (!a_actor) {
            return false;
        }

        const float periodDays = std::max(1.0F,
            Settings::ResolveValue(a_actor, Settings::Get().usagePeriodDays));
        const float usesPerDay = std::max(0.0F,
            Settings::ResolveValue(a_actor, ChannelLimitSetting(a_channel)));
        const auto quota = static_cast<std::uint32_t>(
            std::max(0.0F, std::ceil(usesPerDay * periodDays)));
        auto& state = GetState();
        const auto counter = ChannelCounter(state, a_channel);
        const bool quotaAvailable = quota > 0 && counter < quota;
        const bool cooldownReady = quotaAvailable && IsCooldownReady(a_actor, a_channel);
        const bool allowed = quotaAvailable && cooldownReady;
        logger::debug(
            "[EmergencyHeal][Usage][{}] usesPerDay={}, periodDays={}, counter={}, quota={}, "
            "quotaAvailable={}, cooldownReady={}, allowed={}.",
            ChannelName(a_channel),
            usesPerDay,
            periodDays,
            counter,
            quota,
            quotaAvailable,
            cooldownReady,
            allowed);
        return allowed;
    }

    void RecordChannelUse(Channel a_channel)
    {
        auto& state = GetState();
        auto& counter = ChannelCounter(state, a_channel);
        ++counter;
        auto& lastUse = ChannelLastUse(state, a_channel);
        lastUse.realTimeSeconds = CurrentRealTimeSeconds();
        lastUse.gameTimeSeconds = CurrentGameTimeSeconds();
        logger::debug(
            "[EmergencyHeal][Usage][{}] Uso registrado; contador={}, lastReal={}, lastGame={}.",
            ChannelName(a_channel),
            counter,
            lastUse.realTimeSeconds,
            lastUse.gameTimeSeconds);
    }

    void Reset()
    {
        GetState() = {};
        logger::debug("[EmergencyHeal][Usage] Contadores e timestamps de ultimo uso resetados.");
    }
}

namespace EmergencyHeal::Serialization
{
    void Save(SKSE::SerializationInterface* a_intfc)
    {
        if (a_intfc->OpenRecord(kUsageRecord, kVersion)) {
            const auto& state = Usage::GetState();
            a_intfc->WriteRecordData(state.windowStartGameDays);
            a_intfc->WriteRecordData(state.activationsInWindow);
            a_intfc->WriteRecordData(state.potionActivationsInWindow);
            a_intfc->WriteRecordData(state.magicActivationsInWindow);
            a_intfc->WriteRecordData(state.godMercyActivationsInWindow);
            a_intfc->WriteRecordData(state.potionLastUse.realTimeSeconds);
            a_intfc->WriteRecordData(state.potionLastUse.gameTimeSeconds);
            a_intfc->WriteRecordData(state.magicLastUse.realTimeSeconds);
            a_intfc->WriteRecordData(state.magicLastUse.gameTimeSeconds);
            a_intfc->WriteRecordData(state.godMercyLastUse.realTimeSeconds);
            a_intfc->WriteRecordData(state.godMercyLastUse.gameTimeSeconds);
        }
    }

    void Load(SKSE::SerializationInterface* a_intfc)
    {
        Usage::Reset();
        std::uint32_t type = 0;
        std::uint32_t version = 0;
        std::uint32_t length = 0;
        while (a_intfc->GetNextRecordInfo(type, version, length)) {
            if (type == kUsageRecord && version == kVersion) {
                auto& state = Usage::GetState();
                a_intfc->ReadRecordData(state.windowStartGameDays);
                a_intfc->ReadRecordData(state.activationsInWindow);
                a_intfc->ReadRecordData(state.potionActivationsInWindow);
                a_intfc->ReadRecordData(state.magicActivationsInWindow);
                a_intfc->ReadRecordData(state.godMercyActivationsInWindow);
                a_intfc->ReadRecordData(state.potionLastUse.realTimeSeconds);
                a_intfc->ReadRecordData(state.potionLastUse.gameTimeSeconds);
                a_intfc->ReadRecordData(state.magicLastUse.realTimeSeconds);
                a_intfc->ReadRecordData(state.magicLastUse.gameTimeSeconds);
                a_intfc->ReadRecordData(state.godMercyLastUse.realTimeSeconds);
                a_intfc->ReadRecordData(state.godMercyLastUse.gameTimeSeconds);
                logger::debug(
                    "[EmergencyHeal][Serialization] Uso e timestamps carregados; "
                    "potion(real={}, game={}), magic(real={}, game={}), mercy(real={}, game={}).",
                    state.potionLastUse.realTimeSeconds,
                    state.potionLastUse.gameTimeSeconds,
                    state.magicLastUse.realTimeSeconds,
                    state.magicLastUse.gameTimeSeconds,
                    state.godMercyLastUse.realTimeSeconds,
                    state.godMercyLastUse.gameTimeSeconds);
            } else {
                logger::warn("Emergency Heal: registro de serializacao desconhecido {:08X} v{}.", type, version);
            }
        }
    }

    void Revert(SKSE::SerializationInterface*)
    {
        Usage::Reset();
    }
}
