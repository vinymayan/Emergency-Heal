#include "Events.h"
#include "Manager.h"

EmergencyHealEventSink* EmergencyHealEventSink::GetSingleton()
{
    static EmergencyHealEventSink singleton;
    return &singleton;
}

void EmergencyHealEventSink::Register()
{
    if (auto* source = SKSE::GetModCallbackEventSource()) {
        source->AddEventSink(this);
    }
}

RE::BSEventNotifyControl EmergencyHealEventSink::ProcessEvent(
    const SKSE::ModCallbackEvent* a_event,
    RE::BSTEventSource<SKSE::ModCallbackEvent>*)
{
    if (!a_event) {
        return RE::BSEventNotifyControl::kContinue;
    }

    const std::string_view eventName = a_event->eventName.c_str();
    if (eventName == "DynamicFormsGeneratorLoaded") {
        logger::info("Emergency Heal: Dynamic Forms Generator terminou o carregamento.");
        Manager::GetSingleton()->PopulateAllLists();
    } else if (eventName == "DynamicFormsGeneratorUpdated") {
        Manager::GetSingleton()->RefreshLists(a_event->strArg.c_str());
    }
    return RE::BSEventNotifyControl::kContinue;
}
