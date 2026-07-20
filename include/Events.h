#pragma once

class EmergencyHealEventSink final : public RE::BSTEventSink<SKSE::ModCallbackEvent>
{
public:
    static EmergencyHealEventSink* GetSingleton();
    void Register();

    RE::BSEventNotifyControl ProcessEvent(
        const SKSE::ModCallbackEvent* a_event,
        RE::BSTEventSource<SKSE::ModCallbackEvent>*) override;
};
