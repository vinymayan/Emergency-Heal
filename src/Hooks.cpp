#include "Hooks.h"
#include "Serialization.h"
#include "Settings.h"

namespace
{
    thread_local bool g_handlingEmergency = false;

    float GetPositiveHealthMagnitude(const RE::MagicItem* a_item)
    {
        if (!a_item || a_item->IsHostile()) {
            return 0.0F;
        }

        float score = 0.0F;
        for (const auto* effect : a_item->effects) {
            if (!effect || !effect->baseEffect || effect->baseEffect->IsDetrimental()) {
                continue;
            }
            if (effect->baseEffect->data.primaryAV == RE::ActorValue::kHealth) {
                score += std::max(0.0F, effect->GetMagnitude()) *
                         static_cast<float>(std::max<std::uint32_t>(1, effect->GetDuration()));
            }
        }
        return score;
    }

    struct HealingPotionCandidate
    {
        RE::AlchemyItem* potion = nullptr;
        RE::ExtraDataList* extra = nullptr;
        float score = 0.0F;
        std::int32_t count = 0;
    };

    bool ConsumeBestHealingItem(RE::PlayerCharacter* a_player)
    {
        std::vector<HealingPotionCandidate> candidates;

        auto inventory = a_player->GetInventory([](RE::TESBoundObject& a_object) {
            return a_object.As<RE::AlchemyItem>() != nullptr;
        });
        logger::debug("[EmergencyHeal][Items] Iniciando busca em {} tipos de itens alquimicos.", inventory.size());

        for (const auto& [object, data] : inventory) {
            const auto formID = object ? object->GetFormID() : 0;
            if (data.first <= 0) {
                logger::debug("[EmergencyHeal][Items] {:08X} rejeitado: quantidade {}.", formID, data.first);
                continue;
            }
            auto* potion = object ? object->As<RE::AlchemyItem>() : nullptr;
            if (!potion) {
                logger::debug("[EmergencyHeal][Items] {:08X} rejeitado: nao e AlchemyItem.", formID);
                continue;
            }
            if (potion->IsPoison()) {
                logger::debug("[EmergencyHeal][Items] {:08X} '{}' rejeitado: e veneno.",
                    formID, potion->GetName());
                continue;
            }
            if (potion->IsFood()) {
                logger::debug("[EmergencyHeal][Items] {:08X} '{}' rejeitado: e comida.",
                    formID, potion->GetName());
                continue;
            }

            const float score = GetPositiveHealthMagnitude(potion);
            if (score <= 0.0F) {
                logger::debug(
                    "[EmergencyHeal][Items] {:08X} '{}' rejeitado: nenhum efeito positivo de Health (efeitos={}, hostile={}).",
                    formID,
                    potion->GetName(),
                    potion->effects.size(),
                    potion->IsHostile());
                continue;
            }

            auto* entryData = data.second.get();
            auto* extra = entryData && entryData->extraLists && !entryData->extraLists->empty() ?
                entryData->extraLists->front() : nullptr;
            candidates.push_back({ potion, extra, score, data.first });
            logger::debug(
                "[EmergencyHeal][Items] {:08X} '{}' aceito: quantidade={}, forca={}, extraData={}.",
                formID,
                potion->GetName(),
                data.first,
                score,
                static_cast<const void*>(extra));
        }

        if (candidates.empty()) {
            logger::debug("[EmergencyHeal][Items] Nenhuma pocao de cura valida foi encontrada.");
            return false;
        }

        std::ranges::sort(candidates, [](const auto& a_left, const auto& a_right) {
            return a_left.score > a_right.score;
        });

        for (const auto& candidate : candidates) {
            const bool consumed = a_player->DrinkPotion(candidate.potion, candidate.extra);
            logger::debug(
                "[EmergencyHeal][Items] DrinkPotion {:08X} '{}' retornou {} (forca={}, quantidade antes={}).",
                candidate.potion->GetFormID(),
                candidate.potion->GetName(),
                consumed,
                candidate.score,
                candidate.count);
            if (consumed) {
                logger::info("Emergency Heal consumiu a pocao {:08X} (forca {}).",
                    candidate.potion->GetFormID(), candidate.score);
                return true;
            }
        }

        logger::debug("[EmergencyHeal][Items] Todas as chamadas de DrinkPotion falharam.");
        return false;
    }

    RE::SpellItem* FindStrongestKnownHealingSpell(RE::PlayerCharacter* a_player)
    {
        auto* dataHandler = RE::TESDataHandler::GetSingleton();
        if (!dataHandler) {
            return nullptr;
        }

        RE::SpellItem* best = nullptr;
        float bestScore = 0.0F;
        std::uint32_t knownSpells = 0;
        std::uint32_t healingSpells = 0;
        for (auto* spell : dataHandler->GetFormArray<RE::SpellItem>()) {
            if (!spell || spell->GetSpellType() != RE::MagicSystem::SpellType::kSpell ||
                !a_player->HasSpell(spell)) {
                continue;
            }
            ++knownSpells;
            const float score = GetPositiveHealthMagnitude(spell);
            if (score > 0.0F) {
                ++healingSpells;
            }
            if (score > bestScore) {
                best = spell;
                bestScore = score;
            }
        }
        logger::debug(
            "[EmergencyHeal][Magic] Magias conhecidas={}, magias de cura={}, melhor={:08X}, forca={}.",
            knownSpells,
            healingSpells,
            best ? best->GetFormID() : 0,
            bestScore);
        return best;
    }

    bool CastImmediate(RE::PlayerCharacter* a_player, RE::SpellItem* a_spell)
    {
        if (!a_player || !a_spell) {
            return false;
        }
        auto* caster = a_player->GetMagicCaster(RE::MagicSystem::CastingSource::kInstant);
        if (!caster) {
            return false;
        }
        caster->CastSpellImmediate(a_spell, false, a_player, 1.0F, false, -1.0F, a_player);
        return true;
    }

    bool CastEmergencyHealingSpell(RE::PlayerCharacter* a_player)
    {
        const auto& config = EmergencyHeal::Settings::Get();
        if (!config.castEmergencyMagicIfNoItems ||
            !EmergencyHeal::Settings::HasRequiredPerk(a_player, config.emergencyMagicPerk)) {
            logger::debug(
                "[EmergencyHeal][Magic] Fallback bloqueado: habilitado={}, perk={:08X}, possuiPerk={}.",
                config.castEmergencyMagicIfNoItems,
                config.emergencyMagicPerk,
                EmergencyHeal::Settings::HasRequiredPerk(a_player, config.emergencyMagicPerk));
            return false;
        }

        auto* spell = FindStrongestKnownHealingSpell(a_player);
        if (!spell) {
            logger::debug("[EmergencyHeal][Magic] Nenhuma magia de cura conhecida foi encontrada.");
            return false;
        }

        const float cost = std::max(0.0F, spell->CalculateMagickaCost(a_player));
        auto* owner = a_player->AsActorValueOwner();
        const float magicka = owner ? owner->GetActorValue(RE::ActorValue::kMagicka) : 0.0F;
        const bool forceCast = config.castEvenWithoutMana &&
            EmergencyHeal::Settings::HasRequiredPerk(a_player, config.noManaCastPerk);

        if (magicka + 0.001F < cost && !forceCast) {
            logger::debug(
                "[EmergencyHeal][Magic] Cast bloqueado: spell={:08X}, magicka={}, custo={}, forceCast={}.",
                spell->GetFormID(), magicka, cost, forceCast);
            logger::info("Emergency Heal nao castou {:08X}: magicka {} < custo {}.", spell->GetFormID(), magicka, cost);
            return false;
        }

        if (owner) {
            owner->DamageActorValue(RE::ActorValue::kMagicka, std::min(magicka, cost));
        }
        const bool cast = CastImmediate(a_player, spell);
        logger::debug(
            "[EmergencyHeal][Magic] CastSpellImmediate {:08X} retornou {}; magicka antes={}, custo={}, forceCast={}.",
            spell->GetFormID(), cast, magicka, cost, forceCast);
        if (cast) {
            logger::info("Emergency Heal castou a magia de cura {:08X}, custo {}.", spell->GetFormID(), cost);
        }
        return cast;
    }

    void CastAdditionalSpells(RE::PlayerCharacter* a_player)
    {
        for (const auto& entry : EmergencyHeal::Settings::Get().additionalSpells) {
            if (!entry.enabled || entry.spell == 0 ||
                !EmergencyHeal::Settings::HasRequiredPerk(a_player, entry.requiredPerk) ||
                EmergencyHeal::Settings::IsDisabledByPerk(a_player, entry.disablingPerk)) {
                logger::debug(
                    "[EmergencyHeal][Effects] Entrada ignorada: enabled={}, spell={:08X}, requiredPerk={:08X}, possuiRequired={}, disablingPerk={:08X}, possuiDisabling={}.",
                    entry.enabled,
                    entry.spell,
                    entry.requiredPerk,
                    EmergencyHeal::Settings::HasRequiredPerk(a_player, entry.requiredPerk),
                    entry.disablingPerk,
                    EmergencyHeal::Settings::IsDisabledByPerk(a_player, entry.disablingPerk));
                continue;
            }
            if (auto* spell = RE::TESForm::LookupByID<RE::SpellItem>(entry.spell)) {
                const bool cast = CastImmediate(a_player, spell);
                logger::debug("[EmergencyHeal][Effects] Cast {:08X} '{}' retornou {}.",
                    spell->GetFormID(), spell->GetName(), cast);
            } else {
                logger::debug("[EmergencyHeal][Effects] Spell {:08X} nao foi resolvida.", entry.spell);
            }
        }
    }

    void PerformEmergencyAction(RE::PlayerCharacter* a_player)
    {
        logger::debug("[EmergencyHeal] Executando acao de emergencia.");
        const bool usedItem = ConsumeBestHealingItem(a_player);
        if (!usedItem) {
            const bool castMagic = CastEmergencyHealingSpell(a_player);
            logger::debug("[EmergencyHeal] Nenhum item consumido; resultado do fallback magico={}", castMagic);
        }
        CastAdditionalSpells(a_player);
        logger::debug("[EmergencyHeal] Acao de emergencia concluida.");
    }

    struct HandleHealthDamageHook
    {
        static void thunk(RE::Actor* a_actor, RE::Actor* a_attacker, float a_damage)
        {
            logger::debug(
                "[EmergencyHeal][Damage] Hook chamado: actor={:08X}, attacker={:08X}, rawDamage={}, reentrant={}, isPlayer={}.",
                a_actor ? a_actor->GetFormID() : 0,
                a_attacker ? a_attacker->GetFormID() : 0,
                a_damage,
                g_handlingEmergency,
                a_actor && a_actor->IsPlayerRef());

            if (g_handlingEmergency || !a_actor || !a_actor->IsPlayerRef() || a_damage == 0.0F) {
                logger::debug("[EmergencyHeal][Damage] Ignorado antes da avaliacao.");
                func(a_actor, a_attacker, a_damage);
                return;
            }

            auto* player = static_cast<RE::PlayerCharacter*>(a_actor);
            const auto& config = EmergencyHeal::Settings::Get();
            auto* owner = player->AsActorValueOwner();
            if (!config.enabled || !owner ||
                !EmergencyHeal::Settings::HasRequiredPerk(player, config.requiredPerk) ||
                EmergencyHeal::Settings::IsDisabledByPerk(player, config.disablingPerk)) {
                logger::debug(
                    "[EmergencyHeal][Damage] Sistema bloqueado: enabled={}, owner={}, requiredPerk={:08X}, possuiRequired={}, disablingPerk={:08X}, possuiDisabling={}.",
                    config.enabled,
                    static_cast<const void*>(owner),
                    config.requiredPerk,
                    EmergencyHeal::Settings::HasRequiredPerk(player, config.requiredPerk),
                    config.disablingPerk,
                    EmergencyHeal::Settings::IsDisabledByPerk(player, config.disablingPerk));
                func(a_actor, a_attacker, a_damage);
                return;
            }

            const float health = owner->GetActorValue(RE::ActorValue::kHealth);
            const float maxHealth = std::max(1.0F, player->GetActorValueMax(RE::ActorValue::kHealth));
            const float damageMagnitude = std::abs(a_damage);
            const float projectedHealth = health - damageMagnitude;
            float threshold = EmergencyHeal::Settings::ResolveValue(player, config.healthThreshold);
            if (config.thresholdMode == EmergencyHeal::Settings::ThresholdMode::kPercent) {
                threshold = maxHealth * std::clamp(threshold, 0.0F, 100.0F) / 100.0F;
            } else {
                threshold = std::max(0.0F, threshold);
            }

            logger::debug(
                "[EmergencyHeal][Damage] Avaliacao: health={}, maxHealth={}, rawDamage={}, magnitude={}, projected={}, threshold={}, mode={}, source={}.",
                health,
                maxHealth,
                a_damage,
                damageMagnitude,
                projectedHealth,
                threshold,
                static_cast<int>(config.thresholdMode),
                static_cast<int>(config.healthThreshold.source));

            if (projectedHealth > threshold) {
                logger::debug("[EmergencyHeal][Damage] Nao ativou: vida projetada {} > threshold {}.",
                    projectedHealth, threshold);
                func(a_actor, a_attacker, a_damage);
                return;
            }

            if (!EmergencyHeal::Usage::TryConsumeActivation(player)) {
                logger::debug("[EmergencyHeal][Damage] Nao ativou: cota da janela indisponivel.");
                func(a_actor, a_attacker, a_damage);
                return;
            }

            logger::debug("[EmergencyHeal][Damage] GATILHO ATIVADO: projected={} <= threshold={}.",
                projectedHealth, threshold);

            const bool zeroThreshold = threshold <= 0.001F;
            const bool fatal = projectedHealth <= 0.0F;
            const bool protectFatal = fatal && zeroThreshold && config.protectFatalDamageAtZeroThreshold &&
                EmergencyHeal::Settings::HasRequiredPerk(player, config.fatalProtectionPerk);

            g_handlingEmergency = true;
            if (protectFatal) {
                const float safeDamage = std::max(0.0F, health - 1.0F);
                const float signedSafeDamage = std::copysign(safeDamage, a_damage);
                logger::debug(
                    "[EmergencyHeal][Damage] Protecao fatal: rawDamage={} substituido por {}.",
                    a_damage, signedSafeDamage);
                func(a_actor, a_attacker, signedSafeDamage);
                const float afterDamage = owner->GetActorValue(RE::ActorValue::kHealth);
                if (afterDamage < 1.0F) {
                    owner->RestoreActorValue(RE::ActorValue::kHealth, 1.0F - afterDamage);
                }
                logger::info("Emergency Heal bloqueou dano fatal e preservou 1 de vida.");
            } else {
                logger::debug(
                    "[EmergencyHeal][Damage] Aplicando dano original; fatal={}, zeroThreshold={}, fatalEnabled={}, possuiPerk={}.",
                    fatal,
                    zeroThreshold,
                    config.protectFatalDamageAtZeroThreshold,
                    EmergencyHeal::Settings::HasRequiredPerk(player, config.fatalProtectionPerk));
                func(a_actor, a_attacker, a_damage);
            }

            PerformEmergencyAction(player);
            g_handlingEmergency = false;
        }

        static inline REL::Relocation<decltype(thunk)> func;
    };
}

namespace EmergencyHeal::Hooks
{
    void Install()
    {
        static bool installed = false;
        if (installed) {
            return;
        }
        installed = true;

        REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
        HandleHealthDamageHook::func = vtable.write_vfunc(
            REL::Relocate(0x104, 0x104, 0x106), HandleHealthDamageHook::thunk);
        logger::info("Emergency Heal: hook de HandleHealthDamage instalado.");
    }
}
