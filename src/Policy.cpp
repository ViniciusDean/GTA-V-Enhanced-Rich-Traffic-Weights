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

bool Contains(const std::vector<ModelHash>& values, ModelHash model) {
    return std::find(values.begin(), values.end(), model) != values.end();
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
    return {HashRange(kDailyModels), HashRange(kLuxuryModels), HashRange(kSportModels)};
}

bool ValidateCatalog(const Catalog& catalog, const SlotConfig& slots) {
    if (catalog.daily.size() != 109 || catalog.luxury.size() != 35 || catalog.sport.size() != 98) return false;
    if (slots.daily > catalog.daily.size() || slots.luxury > catalog.luxury.size() || slots.sport > catalog.sport.size()) return false;
    if (slots.daily == 0 || slots.luxury == 0 || slots.sport == 0) return false;

    std::unordered_set<ModelHash> unique;
    for (const auto* group : {&catalog.daily, &catalog.luxury, &catalog.sport}) {
        for (const auto model : *group) {
            if (model == 0 || !unique.insert(model).second) return false;
        }
    }
    return unique.size() == 242;
}

SportRotation::SportRotation(std::vector<ModelHash> source, std::uint32_t seed)
    : source_(std::move(source)), random_(seed) {
    if (source_.empty()) throw std::invalid_argument("sport rotation source cannot be empty");
    const std::unordered_set<ModelHash> unique(source_.begin(), source_.end());
    if (unique.size() != source_.size()) throw std::invalid_argument("sport rotation source must be unique");
    Refill();
}

void SportRotation::Refill() {
    bag_ = source_;
    std::shuffle(bag_.begin(), bag_.end(), random_);
    cursor_ = 0;
}

std::vector<ModelHash> SportRotation::Next(std::size_t count) {
    if (count == 0 || count > source_.size()) throw std::invalid_argument("invalid sport rotation count");
    std::vector<ModelHash> result;
    result.reserve(count);
    std::unordered_set<ModelHash> selected;
    while (result.size() < count) {
        if (cursor_ >= bag_.size()) Refill();
        const ModelHash model = bag_[cursor_++];
        if (selected.insert(model).second) result.push_back(model);
    }
    return result;
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

Selection BuildSelection(const Catalog& catalog, const SlotConfig& slots,
                         const std::vector<ModelHash>& activeSports) {
    if (!ValidateCatalog(catalog, slots)) throw std::invalid_argument("invalid catalog or slot configuration");
    if (activeSports.size() != slots.sport) throw std::invalid_argument("sport selection has wrong size");

    Selection result;
    result.active.reserve(slots.daily + slots.luxury + slots.sport);
    result.suppressed.reserve(242 - result.active.size());
    result.active.insert(catalog.daily.begin(), catalog.daily.begin() + static_cast<std::ptrdiff_t>(slots.daily));
    result.active.insert(catalog.luxury.begin(), catalog.luxury.begin() + static_cast<std::ptrdiff_t>(slots.luxury));
    for (const ModelHash model : activeSports) {
        if (!Contains(catalog.sport, model) || !result.active.insert(model).second) {
            throw std::invalid_argument("sport selection contains invalid or duplicate model");
        }
    }
    for (const auto* group : {&catalog.daily, &catalog.luxury, &catalog.sport}) {
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

bool IsControlledZone(std::string_view zone) {
    return std::find(kControlledZones.begin(), kControlledZones.end(), zone) != kControlledZones.end();
}

} // namespace richtraffic
