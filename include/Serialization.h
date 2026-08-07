#pragma once

namespace EmergencyHeal::Usage
{
    enum class Channel : std::uint8_t
    {
        kPotion,
        kMagic,
        kGodMercy
    };

    struct LastUse
    {
        double realTimeSeconds = -1.0;
        double gameTimeSeconds = -1.0;
    };

    struct State
    {
        float windowStartGameDays = -1.0F;
        std::uint32_t activationsInWindow = 0;
        std::uint32_t potionActivationsInWindow = 0;
        std::uint32_t magicActivationsInWindow = 0;
        std::uint32_t godMercyActivationsInWindow = 0;
        LastUse potionLastUse;
        LastUse magicLastUse;
        LastUse godMercyLastUse;
    };

    State& GetState();
    bool CanConsumeActivation(RE::Actor* a_actor);
    bool TryConsumeActivation(RE::Actor* a_actor);
    bool CanUseChannel(RE::Actor* a_actor, Channel a_channel);
    void RecordChannelUse(Channel a_channel);
    void Reset();
}

namespace EmergencyHeal::Serialization
{
    constexpr std::uint32_t kSerializationID = 'EHL1';

    void Save(SKSE::SerializationInterface* a_intfc);
    void Load(SKSE::SerializationInterface* a_intfc);
    void Revert(SKSE::SerializationInterface* a_intfc);
}
