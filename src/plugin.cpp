#include "logger.h"
#include "Events.h"
#include "Hooks.h"
#include "Manager.h"
#include "Serialization.h"
#include "Settings.h"

namespace
{
    bool g_hasDynamicFormsGenerator = false;
}

void OnMessage(SKSE::MessagingInterface::Message* message) {
    if (message->type == SKSE::MessagingInterface::kPostLoad) {
        g_hasDynamicFormsGenerator = GetModuleHandleA("DynamicFormsGenerator.dll") != nullptr;
        EmergencyHealMenu::Register();
    }
    if (message->type == SKSE::MessagingInterface::kDataLoaded) {
        // Resolve FormIDs novamente agora que o TESDataHandler esta pronto.
        EmergencyHealMenu::LoadSettings();
        EmergencyHeal::Hooks::Install();
        if (!g_hasDynamicFormsGenerator) {
            Manager::GetSingleton()->PopulateAllLists();
        }
    }
    if (message->type == SKSE::MessagingInterface::kNewGame || message->type == SKSE::MessagingInterface::kPostLoadGame) {
    }
}

SKSEPluginLoad(const SKSE::LoadInterface *skse) {

    SetupLog();
    logger::info("Plugin loaded");
    SKSE::Init(skse);
    if (auto* serialization = SKSE::GetSerializationInterface()) {
        serialization->SetUniqueID(EmergencyHeal::Serialization::kSerializationID);
        serialization->SetSaveCallback(EmergencyHeal::Serialization::Save);
        serialization->SetLoadCallback(EmergencyHeal::Serialization::Load);
        serialization->SetRevertCallback(EmergencyHeal::Serialization::Revert);
    }
    EmergencyHealEventSink::GetSingleton()->Register();
    SKSE::GetMessagingInterface()->RegisterListener(OnMessage);
    return true;
}
