#pragma once

namespace EmergencyHeal::Usage
{
    struct State
    {
        float windowStartGameDays = -1.0F;
        std::uint32_t activationsInWindow = 0;
    };

    State& GetState();
    bool TryConsumeActivation(RE::Actor* a_actor);
    void Reset();
}

namespace EmergencyHeal::Serialization
{
    constexpr std::uint32_t kSerializationID = 'EHL1';

    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);
}
