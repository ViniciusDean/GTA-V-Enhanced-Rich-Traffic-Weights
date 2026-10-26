#include "Policy.h"
#include "ModelCatalog.h"

#include <cstdlib>
#include <iostream>
#include <random>
#include <unordered_set>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

} // namespace

int main() {
    using namespace richtraffic;
    Require(HashModelName("adder") == 0xB779A091u, "JOAAT conhecido para adder");
    Require(HashModelName("hash_560dc4fc") == 0x560DC4FCu, "hash bruto do CodeWalker");

    const Catalog catalog = BuildCatalog();
    Require(ValidateCatalog(catalog), "catalogo valido e sem sobreposicao");
    Require(catalog.daily.size() == 116 && catalog.luxury.size() == 38 && catalog.sport.size() == 68 &&
            catalog.exotic.size() == 44, "catalogo 116/38/68/44");

    const ModelWeights neutral;
    std::mt19937 random(12345);
    const Selection full = BuildWeightedSelection(catalog, {1.0f, 1.0f, 1.0f, 1.0f}, neutral, random);
    Require(full.suppressed.empty() && full.active.size() == 266, "probabilidade 1 libera tudo");

    const Selection none = BuildWeightedSelection(catalog, {0.0f, 0.0f, 0.0f, 0.0f}, neutral, random);
    Require(none.active.empty() && none.suppressed.size() == 266, "probabilidade 0 suprime tudo");

    const Selection poorish = BuildWeightedSelection(catalog, {1.0f, 0.05f, 0.001f, 0.001f}, neutral, random);
    std::size_t luxuryActive = 0, sportActive = 0, exoticActive = 0;
    for (const auto m : catalog.luxury) luxuryActive += poorish.active.count(m);
    for (const auto m : catalog.sport) sportActive += poorish.active.count(m);
    for (const auto m : catalog.exotic) exoticActive += poorish.active.count(m);
    Require(luxuryActive >= 1 && sportActive >= 1 && exoticActive >= 1, "ao menos um modelo por tier");
    for (const auto m : catalog.daily) Require(poorish.active.count(m) == 1, "cotidianos todos ativos");
    for (const auto model : poorish.active) Require(poorish.suppressed.count(model) == 0, "mascaras disjuntas");

    std::size_t activeTotal = 0;
    for (int roll = 0; roll < 200; ++roll) {
        const Selection s = BuildWeightedSelection(catalog, {0.0f, 0.0f, 0.5f, 0.0f}, neutral, random);
        activeTotal += s.active.size();
    }
    const double averageSport = static_cast<double>(activeTotal) / 200.0;
    Require(averageSport > 30.0 && averageSport < 38.0, "peso 0.5 libera cerca de metade dos 68 esportivos");

    // Per-model weights: 0 bans even at probability 1, and the tier fallback skips banned models.
    ModelWeights weights;
    weights[HashModelName("pipistrello")] = 0.0f;
    const Selection banned = BuildWeightedSelection(catalog, {1.0f, 1.0f, 1.0f, 1.0f}, weights, random);
    Require(banned.active.count(HashModelName("pipistrello")) == 0 &&
            banned.suppressed.count(HashModelName("pipistrello")) == 1, "peso 0 bane o modelo");
    ModelWeights onlyOne;
    for (const auto m : catalog.exotic) onlyOne[m] = 0.0f;
    onlyOne[HashModelName("t20")] = 1.0f;
    for (int roll = 0; roll < 20; ++roll) {
        const Selection s = BuildWeightedSelection(catalog, {0.0f, 0.0f, 0.0f, 0.001f}, onlyOne, random);
        Require(s.active.size() == 1 && s.active.count(HashModelName("t20")) == 1,
                "fallback do tier escolhe so modelos nao banidos");
    }
    std::size_t boosted = 0;
    ModelWeights doubled;
    doubled[HashModelName("t20")] = 2.0f;
    for (int roll = 0; roll < 2000; ++roll) {
        const Selection s = BuildWeightedSelection(catalog, {1.0f, 1.0f, 1.0f, 0.25f}, doubled, random);
        boosted += s.active.count(HashModelName("t20"));
    }
    Require(boosted > 900 && boosted < 1100, "peso 2 dobra a chance (0.25 -> 0.5)");

    Require(ClampProbability(2.0f) == 1.0f && ClampProbability(-1.0f) == 0.0f, "clamp de probabilidade");
    const ZoneProfile night = ScaleFastTiers({1.0f, 1.0f, 0.4f, 0.1f}, 1.5f);
    Require(night.sport > 0.59f && night.exotic > 0.149f && night.daily == 1.0f, "reforco noturno so nos tiers rapidos");
    Require(ScaleFastTiers({1.0f, 1.0f, 0.9f, 0.9f}, 2.0f).sport == 1.0f, "reforco com teto 1");
    Require(ScaleFastTiers({1.0f, 1.0f, 0.4f, 0.2f}, 0.5f).exotic < 0.101f, "chuva reduz exoticos");

    const ZoneTable zones = BuildDefaultZones();
    Require(zones.size() == kDefaultZones.size(), "tabela de zonas padrao");
    for (const auto& zone : kDefaultZones) Require(FindZoneProfile(zones, zone.name) != nullptr, "zona reconhecida");
    Require(FindZoneProfile(zones, "SANDY") == nullptr, "Sandy sem perfil, mas com anti-repeticao global");
    Require(FindZoneProfile(zones, "DAVIS")->luxury < FindZoneProfile(zones, "ROCKF")->luxury,
            "zona pobre tem menos luxo que zona rica");
    for (const auto& zone : kDefaultZones) Require(zone.exotic <= zone.sport, "exotico nunca mais comum que esportivo");

    std::vector<RepeatObservation> observations = {
        {10, 2, 400.0f}, {20, 4, 900.0f}, {30, 4, 100.0f}, {40, 1, 1.0f}
    };
    const auto candidates = SelectRepeatCandidates(observations, 2, 2);
    Require(candidates.size() == 2, "limite adaptativo respeitado");
    Require(candidates[0].model == 30 && candidates[1].model == 20,
            "prioridade por quantidade e proximidade");
    Require(SelectRepeatCandidates(observations, 5, 32).empty(), "um exemplar nao ativa cooldown");

    RepeatCooldowns cooldowns(2);
    auto events = cooldowns.Update({{10,2,1.0f}}, 1000, 90000, 5000);
    Require(events.size() == 1 && events[0].type == CooldownEventType::Started, "cooldown iniciado");
    Require(cooldowns.IsActive(10, 1001), "modelo bloqueado durante cooldown");
    events = cooldowns.Update({{10,2,1.0f}}, 86000, 90000, 5000);
    Require(events.size() == 1 && events[0].type == CooldownEventType::Renewed, "cooldown renovado perto do fim");
    cooldowns.Update({{20,3,2.0f}}, 87000, 90000, 5000);
    events = cooldowns.Update({{30,4,1.0f},{20,3,2.0f}}, 88000, 90000, 5000);
    Require(cooldowns.Size() == 2 && cooldowns.IsActive(30, 88001), "capacidade respeitada com substituicao prioritaria");
    bool evicted = false;
    for (const auto& event : events) if (event.type == CooldownEventType::Evicted && event.model == 10) evicted = true;
    Require(evicted, "cooldown antigo fora dos candidatos foi substituido");
    events = cooldowns.Update({}, 178001, 90000, 5000);
    Require(cooldowns.Size() == 0, "cooldowns expiraram");
    cooldowns.Clear();
    Require(cooldowns.Size() == 0, "fail-open limpa todos os cooldowns");

    for (int vehicleClass = 0; vehicleClass <= 7; ++vehicleClass) Require(IsPassengerClass(vehicleClass), "classe de passeio aceita");
    Require(IsPassengerClass(9), "off-road e picapes aceitos");
    for (const int vehicleClass : {8,10,11,12,13,14,15,16,17,18,19,20,21,22}) {
        Require(!IsPassengerClass(vehicleClass), "classe de frota ou nao rodoviaria excluida");
    }

    std::cout << "PASS: catalogo=116/38/68/44 pesos por zona e por modelo, reforco noturno, anti-repeat=classes0-7,9\n";
    return 0;
}
