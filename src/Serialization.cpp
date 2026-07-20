#include "Serialization.h"
#include "Settings.h"

namespace
{
    constexpr std::uint32_t kUsageRecord = 'USE1';
    constexpr std::uint32_t kVersion = 1;
}

namespace EmergencyHeal::Usage
{
    State& GetState()
    {
        static State state;
        return state;
    }

    bool TryConsumeActivation(RE::Actor* a_actor)
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
            logger::debug("[EmergencyHeal][Usage] Nova janela iniciada em {}.", now);
        }
        if (state.activationsInWindow >= quota) {
            logger::debug(
                "[EmergencyHeal][Usage] Bloqueado: ativacoes {} >= quota {} (inicio={}).",
                state.activationsInWindow, quota, state.windowStartGameDays);
            return false;
        }
        ++state.activationsInWindow;
        logger::debug(
            "[EmergencyHeal][Usage] Ativacao consumida: {}/{} (inicio={}).",
            state.activationsInWindow, quota, state.windowStartGameDays);
        return true;
    }

    void Reset()
    {
        GetState() = {};
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
