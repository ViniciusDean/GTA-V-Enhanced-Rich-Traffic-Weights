#pragma once

#include <cstdint>
#include <random>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace richtraffic {

using ModelHash = std::uint32_t;

struct SlotConfig {
    std::size_t daily = 109;
    std::size_t luxury = 35;
    std::size_t sport = 28;
};

struct Catalog {
    std::vector<ModelHash> daily;
    std::vector<ModelHash> luxury;
    std::vector<ModelHash> sport;
};

struct Selection {
    std::unordered_set<ModelHash> active;
    std::unordered_set<ModelHash> suppressed;
};

struct RepeatObservation {
    ModelHash model = 0;
    std::size_t count = 0;
    float nearestDistanceSquared = 0.0f;
};

class SportRotation {
public:
    SportRotation(std::vector<ModelHash> source, std::uint32_t seed);
    std::vector<ModelHash> Next(std::size_t count);

private:
    void Refill();
    std::vector<ModelHash> source_;
    std::vector<ModelHash> bag_;
    std::size_t cursor_ = 0;
    std::mt19937 random_;
};

enum class CooldownEventType { Started, Renewed, Expired, Evicted };

struct CooldownEvent {
    CooldownEventType type;
    ModelHash model;
};

class RepeatCooldowns {
public:
    explicit RepeatCooldowns(std::size_t maxModels);
    std::vector<CooldownEvent> Update(const std::vector<RepeatObservation>& candidates,
                                      std::uint64_t now, std::uint64_t cooldownMs,
                                      std::uint64_t renewWindowMs);
    bool IsActive(ModelHash model, std::uint64_t now) const;
    std::size_t Size() const;
    const std::unordered_map<ModelHash, std::uint64_t>& Entries() const;
    void Clear();

private:
    std::size_t maxModels_;
    std::unordered_map<ModelHash, std::uint64_t> entries_;
};

ModelHash HashModelName(std::string_view name);
Catalog BuildCatalog();
bool ValidateCatalog(const Catalog& catalog, const SlotConfig& slots);
Selection BuildSelection(const Catalog& catalog, const SlotConfig& slots,
                         const std::vector<ModelHash>& activeSports);
std::vector<RepeatObservation> SelectRepeatCandidates(
    std::vector<RepeatObservation> observations, std::size_t repeatLimit,
    std::size_t maxModels);
bool IsPassengerClass(int vehicleClass);
bool IsControlledZone(std::string_view zone);

} // namespace richtraffic
