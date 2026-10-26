#include "Policy.h"
#include "ModelCatalog.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace richtraffic {
namespace {

template <typename Range>
std::vector<ModelHash> HashRange(const Range& names) {
    std::vector<ModelHash> result;
    result.reserve(names.size());
    for (const auto name : names) result.push_back(HashModelName(name));
    return result;
}

} // namespace

ModelHash HashModelName(std::string_view name) {
    constexpr std::string_view prefix = "hash_";
    if (name.size() == 13 && name.substr(0, prefix.size()) == prefix) {
        ModelHash value = 0;
        for (const char character : name.substr(prefix.size())) {
            value <<= 4;
            if (character >= '0' && character <= '9') value |= static_cast<ModelHash>(character - '0');
            else if (character >= 'a' && character <= 'f') value |= static_cast<ModelHash>(character - 'a' + 10);
            else if (character >= 'A' && character <= 'F') value |= static_cast<ModelHash>(character - 'A' + 10);
            else throw std::invalid_argument("invalid raw model hash");
        }
        return value;
    }

    ModelHash hash = 0;
    for (const unsigned char raw : name) {
        const auto character = static_cast<unsigned char>(std::tolower(raw));
        hash += character;
        hash += hash << 10;
        hash ^= hash >> 6;
    }
    hash += hash << 3;
    hash ^= hash >> 11;
    hash += hash << 15;
    return hash;
}

Catalog BuildCatalog() {
    return {HashRange(kDailyModels), HashRange(kLuxuryModels), HashRange(kSportModels),
            HashRange(kExoticModels)};
}

bool ValidateCatalog(const Catalog& catalog) {
    if (catalog.daily.empty() || catalog.luxury.empty() || catalog.sport.empty() ||
        catalog.exotic.empty()) return false;
    std::unordered_set<ModelHash> unique;
    for (const auto* group : {&catalog.daily, &catalog.luxury, &catalog.sport, &catalog.exotic}) {
        for (const auto model : *group) {
            if (model == 0 || !unique.insert(model).second) return false;
        }
    }
    return true;
}

float ClampProbability(float value) {
    if (!(value > 0.0f)) return 0.0f;
    return value > 1.0f ? 1.0f : value;
}

ZoneProfile ScaleFastTiers(ZoneProfile profile, float multiplier) {
    profile.sport = ClampProbability(profile.sport * multiplier);
    profile.exotic = ClampProbability(profile.exotic * multiplier);
    return profile;
}

ZoneTable BuildDefaultZones() {
    ZoneTable zones;
    for (const auto& zone : kDefaultZones) {
        zones[std::string(zone.name)] = {zone.daily, zone.luxury, zone.sport, zone.exotic};
    }
    return zones;
}

const ZoneProfile* FindZoneProfile(const ZoneTable& zones, std::string_view zone) {
    const auto found = zones.find(std::string(zone));
    return found == zones.end() ? nullptr : &found->second;
}

RepeatCooldowns::RepeatCooldowns(std::size_t maxModels) : maxModels_(maxModels) {
    if (maxModels_ == 0) throw std::invalid_argument("cooldown capacity cannot be zero");
}

std::vector<CooldownEvent> RepeatCooldowns::Update(
    const std::vector<RepeatObservation>& candidates, std::uint64_t now,
    std::uint64_t cooldownMs, std::uint64_t renewWindowMs) {
    if (cooldownMs == 0) throw std::invalid_argument("cooldown duration cannot be zero");
    std::vector<CooldownEvent> events;
    for (auto iterator = entries_.begin(); iterator != entries_.end();) {
        if (iterator->second > now) { ++iterator; continue; }
        events.push_back({CooldownEventType::Expired, iterator->first});
        iterator = entries_.erase(iterator);
    }

    std::unordered_set<ModelHash> candidateModels;
    for (const auto& candidate : candidates) candidateModels.insert(candidate.model);
    for (const auto& candidate : candidates) {
        auto cooldown = entries_.find(candidate.model);
        if (cooldown == entries_.end()) {
            if (entries_.size() >= maxModels_) {
                auto eviction = entries_.end();
                for (auto iterator = entries_.begin(); iterator != entries_.end(); ++iterator) {
                    if (candidateModels.count(iterator->first) != 0) continue;
                    if (eviction == entries_.end() || iterator->second < eviction->second) eviction = iterator;
                }
                if (eviction == entries_.end()) continue;
                events.push_back({CooldownEventType::Evicted, eviction->first});
                entries_.erase(eviction);
            }
            entries_[candidate.model] = now + cooldownMs;
            events.push_back({CooldownEventType::Started, candidate.model});
        } else if (cooldown->second <= now + renewWindowMs) {
            cooldown->second = now + cooldownMs;
            events.push_back({CooldownEventType::Renewed, candidate.model});
        }
    }
    return events;
}

bool RepeatCooldowns::IsActive(ModelHash model, std::uint64_t now) const {
    const auto found = entries_.find(model);
    return found != entries_.end() && found->second > now;
}

std::size_t RepeatCooldowns::Size() const { return entries_.size(); }

const std::unordered_map<ModelHash, std::uint64_t>& RepeatCooldowns::Entries() const {
    return entries_;
}

void RepeatCooldowns::Clear() { entries_.clear(); }

namespace {

// Each model is allowed with the tier probability; at least one model per tier
// always stays active so a zone never runs out of a whole tier.
float ModelWeight(const ModelWeights& weights, ModelHash model) {
    const auto found = weights.find(model);
    return found == weights.end() ? 1.0f : found->second;
}

void RollTier(const std::vector<ModelHash>& models, float probability, const ModelWeights& weights,
              std::mt19937& random, Selection& result) {
    std::uniform_real_distribution<float> roll(0.0f, 1.0f);
    std::vector<ModelHash> eligible;
    std::size_t activeBefore = result.active.size();
    for (const ModelHash model : models) {
        const float chance = ClampProbability(probability * ModelWeight(weights, model));
        if (chance <= 0.0f) continue;
        eligible.push_back(model);
        if (roll(random) < chance) result.active.insert(model);
    }
    // Models weighted to 0 are banned, so the fallback only picks from the rest.
    if (result.active.size() == activeBefore && probability > 0.0f && !eligible.empty()) {
        std::uniform_int_distribution<std::size_t> pick(0, eligible.size() - 1);
        result.active.insert(eligible[pick(random)]);
    }
}

} // namespace

Selection BuildWeightedSelection(const Catalog& catalog, const ZoneProfile& profile,
                                 const ModelWeights& weights, std::mt19937& random) {
    if (!ValidateCatalog(catalog)) throw std::invalid_argument("invalid catalog");
    Selection result;
    RollTier(catalog.daily, profile.daily, weights, random, result);
    RollTier(catalog.luxury, profile.luxury, weights, random, result);
    RollTier(catalog.sport, profile.sport, weights, random, result);
    RollTier(catalog.exotic, profile.exotic, weights, random, result);
    for (const auto* group : {&catalog.daily, &catalog.luxury, &catalog.sport, &catalog.exotic}) {
        for (const ModelHash model : *group) {
            if (result.active.count(model) == 0) result.suppressed.insert(model);
        }
    }
    return result;
}

std::vector<RepeatObservation> SelectRepeatCandidates(
    std::vector<RepeatObservation> observations, std::size_t repeatLimit,
    std::size_t maxModels) {
    observations.erase(
        std::remove_if(observations.begin(), observations.end(),
                       [repeatLimit](const RepeatObservation& value) {
                           return value.model == 0 || value.count < repeatLimit;
                       }),
        observations.end());
    std::sort(observations.begin(), observations.end(),
              [](const RepeatObservation& left, const RepeatObservation& right) {
                  if (left.count != right.count) return left.count > right.count;
                  if (left.nearestDistanceSquared != right.nearestDistanceSquared) {
                      return left.nearestDistanceSquared < right.nearestDistanceSquared;
                  }
                  return left.model < right.model;
              });
    if (observations.size() > maxModels) observations.resize(maxModels);
    return observations;
}

bool IsPassengerClass(int vehicleClass) {
    return (vehicleClass >= 0 && vehicleClass <= 7) || vehicleClass == 9;
}

} // namespace richtraffic
