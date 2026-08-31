#include "Policy.h"
#include "ModelCatalog.h"

#include <cstdlib>
#include <iostream>
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
    const SlotConfig slots{109, 35, 28};
    Require(ValidateCatalog(catalog, slots), "catalogo 109/35/98 valido e sem sobreposicao");

    SportRotation rotation(catalog.sport, 12345);
    std::unordered_set<ModelHash> firstBagCoverage;
    std::vector<ModelHash> activeSports;
    for (int cycle = 0; cycle < 4; ++cycle) {
        activeSports = rotation.Next(slots.sport);
        Require(std::unordered_set<ModelHash>(activeSports.begin(), activeSports.end()).size() == 28,
                "ciclo esportivo sem duplicatas");
        firstBagCoverage.insert(activeSports.begin(), activeSports.end());
    }
    Require(firstBagCoverage.size() == 98, "quatro ciclos cobrem os 98 esportivos");

    const Selection selection = BuildSelection(catalog, slots, activeSports);
    Require(selection.active.size() == 172, "109+35+28 ativos");
    Require(selection.suppressed.size() == 70, "242-172 suprimidos por categoria");
    for (const auto model : selection.active) Require(selection.suppressed.count(model) == 0, "mascaras disjuntas");
    for (const auto model : catalog.daily) Require(selection.active.count(model) == 1, "todos os cotidianos ativos");
    for (const auto model : catalog.luxury) Require(selection.active.count(model) == 1, "todos os luxos ativos");

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

    for (const auto zone : kControlledZones) Require(IsControlledZone(zone), "zona configurada reconhecida");
    Require(!IsControlledZone("SANDY"), "Sandy sem peso de categoria, mas com anti-repeticao global");

    SlotConfig invalid = slots;
    invalid.sport = 99;
    Require(!ValidateCatalog(catalog, invalid), "slots fora do limite rejeitados");
    std::cout << "PASS: catalogo=109/35/98 ativos=109/35/28 zonas=12 vivanite2=removido anti-repeat=classes0-7,9\n";
    return 0;
}
