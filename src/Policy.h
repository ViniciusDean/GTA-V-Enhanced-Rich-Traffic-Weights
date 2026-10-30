#pragma once

#include <cstdint>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace richtraffic {

using ModelHash = std::uint32_t;

// Probability (0..1) that each model of a tier is allowed to spawn in a zone.
struct ZoneProfile {
    float daily = 1.0f;
    float luxury = 1.0f;
    float sport = 1.0f;
    float exotic = 1.0f;
};

using ZoneTable = std::unordered_map<std::string, ZoneProfile>;

// Per-model multiplier on top of the tier probability (0 = never, 1 = neutral).
using ModelWeights = std::unordered_map<ModelHash, float>;

struct Catalog {
    std::vector<ModelHash> daily;
    std::vector<ModelHash> luxury;
    std::vector<ModelHash> sport;
    std::vector<ModelHash> exotic;
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
bool ValidateCatalog(const Catalog& catalog);
float ClampProbability(float value);
// Scales the sport and exotic tiers (night boost, rain penalty), capped at 1.
ZoneProfile ScaleFastTiers(ZoneProfile profile, float multiplier);
ZoneTable BuildDefaultZones();
const ZoneProfile* FindZoneProfile(const ZoneTable& zones, std::string_view zone);
Selection BuildWeightedSelection(const Catalog& catalog, const ZoneProfile& profile,
                                 const ModelWeights& weights, std::mt19937& random);
// Models that are suppressed everywhere: drift variants (optional) plus every
// model weighted to 0 in [Models], catalog or not.
std::unordered_set<ModelHash> BuildBanList(bool banDriftVariants, const ModelWeights& weights);
std::vector<RepeatObservation> SelectRepeatCandidates(
    std::vector<RepeatObservation> observations, std::size_t repeatLimit,
    std::size_t maxModels);
bool IsPassengerClass(int vehicleClass);

} // namespace richtraffic
