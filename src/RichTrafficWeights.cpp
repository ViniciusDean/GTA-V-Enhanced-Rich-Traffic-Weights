#include "RichTrafficWeights.h"
#include "ModelCatalog.h"
#include "Policy.h"

#include "main.h"
#include "natives.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using namespace richtraffic;

struct RuntimeConfig {
    bool enabled = true;
    DWORD zonePollMs = 1000;
    DWORD sportCycleMs = 30000;
    float nightSportMultiplier = 1.5f;
    int nightStartHour = 21;
    int nightEndHour = 5;
    float rainFastMultiplier = 0.5f;
    DWORD reloadKey = VK_F11;
    bool antiRepeatEnabled = true;
    DWORD repeatScanMs = 5000;
    float repeatRadiusMeters = 180.0f;
    std::size_t repeatLimit = 2;
    DWORD repeatCooldownMs = 90000;
    std::size_t maxRepeatCooldownModels = 32;
    DWORD toggleKey = VK_F10;
    bool debugOverlay = false;
    bool debugTelemetry = false;
    bool logAntiRepeat = true;
};

HMODULE g_module = nullptr;
std::atomic<bool> g_toggleRequested{false};
DWORD g_toggleKey = VK_F10;
std::atomic<bool> g_reloadRequested{false};
DWORD g_reloadKey = VK_F11;
ModelWeights g_modelWeights;
ZoneProfile g_lastProfile{};
std::filesystem::path g_directory;
std::filesystem::path g_logPath;
Catalog g_catalog;
std::mt19937 g_random{std::random_device{}()};
ZoneTable g_zones;
std::string g_rolledZone;
constexpr const char* kVersion = "1.4.0";
Selection g_selection;
std::unordered_map<ModelHash, std::string> g_modelNames;
std::unordered_map<ModelHash, bool> g_suppressionState;
std::unique_ptr<RepeatCooldowns> g_repeatCooldowns;
bool g_insideZone = false;
bool g_runtimeEnabled = true;
bool g_configValid = true;
std::string g_currentZone;

std::string Timestamp() {
    SYSTEMTIME time{};
    GetLocalTime(&time);
    std::ostringstream stream;
    stream << std::setfill('0') << std::setw(4) << time.wYear << '-'
           << std::setw(2) << time.wMonth << '-' << std::setw(2) << time.wDay << ' '
           << std::setw(2) << time.wHour << ':' << std::setw(2) << time.wMinute << ':'
           << std::setw(2) << time.wSecond;
    return stream.str();
}

void Log(const std::string& message) {
    std::ofstream stream(g_logPath, std::ios::app);
    if (stream) stream << '[' << Timestamp() << "] " << message << '\n';
}

std::filesystem::path GetModuleDirectory() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(g_module, buffer, MAX_PATH);
    if (length == 0 || length == MAX_PATH) return std::filesystem::current_path();
    return std::filesystem::path(buffer).parent_path();
}

template <typename Range>
void AddModelNames(const Range& names) {
    for (const auto name : names) g_modelNames[HashModelName(name)] = std::string(name);
}

std::string ModelName(ModelHash model) {
    const auto found = g_modelNames.find(model);
    if (found != g_modelNames.end()) return found->second;
    // Vanilla traffic models are not in the catalog: ask the game for their name.
    const char* label = VEHICLE::GET_DISPLAY_NAME_FROM_VEHICLE_MODEL(model);
    if (label && *label && std::string_view(label) != "CARNOTFOUND") {
        std::string name(label);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
        g_modelNames[model] = name;
        return name;
    }
    std::ostringstream stream;
    stream << "hash_" << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << model;
    return stream.str();
}

int ReadInt(const std::filesystem::path& iniPath, const char* key, int defaultValue) {
    return GetPrivateProfileIntA("RichTrafficWeights", key, defaultValue, iniPath.string().c_str());
}

float ReadFloat(const std::filesystem::path& iniPath, const char* key, float defaultValue) {
    char defaultBuffer[32]{};
    char value[32]{};
    sprintf_s(defaultBuffer, "%.2f", defaultValue);
    GetPrivateProfileStringA("RichTrafficWeights", key, defaultBuffer, value,
                             static_cast<DWORD>(std::size(value)), iniPath.string().c_str());
    try { return std::stof(value); }
    catch (...) { throw std::invalid_argument(std::string(key) + " must be numeric"); }
}

bool ReadBool(const std::filesystem::path& iniPath, const char* key, bool defaultValue) {
    char value[16]{};
    GetPrivateProfileStringA("RichTrafficWeights", key, defaultValue ? "true" : "false", value,
                             static_cast<DWORD>(std::size(value)), iniPath.string().c_str());
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") return true;
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") return false;
    throw std::invalid_argument(std::string(key) + " must be true or false");
}

// Accepts F1..F12; "None" (only when allowNone) disables the key.
DWORD ParseKey(const std::filesystem::path& iniPath, const char* name, const char* defaultValue,
               bool allowNone) {
    char value[32]{};
    GetPrivateProfileStringA("RichTrafficWeights", name, defaultValue, value,
                             static_cast<DWORD>(std::size(value)), iniPath.string().c_str());
    std::string key(value);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    if (allowNone && key == "NONE") return 0;
    if (key.size() >= 2 && key[0] == 'F') {
        try {
            const int number = std::stoi(key.substr(1));
            if (number >= 1 && number <= 12) return static_cast<DWORD>(VK_F1 + number - 1);
        } catch (...) {}
    }
    throw std::invalid_argument(std::string(name) + " must be F1 through F12");
}

RuntimeConfig LoadConfig(const std::filesystem::path& iniPath) {
    RuntimeConfig config;
    config.enabled = ReadBool(iniPath, "Enabled", true);
    config.zonePollMs = static_cast<DWORD>(ReadInt(iniPath, "ZonePollMs", 1000));
    config.sportCycleMs = static_cast<DWORD>(ReadInt(iniPath, "SportCycleSeconds", 30)) * 1000;
    config.nightSportMultiplier = ReadFloat(iniPath, "NightSportMultiplier", 1.5f);
    config.nightStartHour = ReadInt(iniPath, "NightStartHour", 21);
    config.nightEndHour = ReadInt(iniPath, "NightEndHour", 5);
    config.antiRepeatEnabled = ReadBool(iniPath, "AntiRepeatEnabled", true);
    config.repeatScanMs = static_cast<DWORD>(ReadInt(iniPath, "RepeatScanMs", 5000));
    config.repeatRadiusMeters = ReadFloat(iniPath, "RepeatRadiusMeters", 180.0f);
    config.repeatLimit = static_cast<std::size_t>(ReadInt(iniPath, "RepeatLimit", 2));
    config.repeatCooldownMs = static_cast<DWORD>(ReadInt(iniPath, "RepeatCooldownSeconds", 90)) * 1000;
    config.maxRepeatCooldownModels = static_cast<std::size_t>(ReadInt(iniPath, "MaxRepeatCooldownModels", 32));
    config.toggleKey = ParseKey(iniPath, "ToggleKey", "F10", false);
    config.reloadKey = ParseKey(iniPath, "ReloadKey", "F11", true);
    config.rainFastMultiplier = ReadFloat(iniPath, "RainFastMultiplier", 0.5f);
    config.debugOverlay = ReadBool(iniPath, "DebugOverlay", false);
    config.debugTelemetry = ReadBool(iniPath, "DebugTelemetry", false);
    config.logAntiRepeat = ReadBool(iniPath, "LogAntiRepeat", true);

    if (config.nightSportMultiplier < 0.0f || config.nightSportMultiplier > 10.0f) throw std::invalid_argument("NightSportMultiplier must be 0..10");
    if (config.rainFastMultiplier < 0.0f || config.rainFastMultiplier > 2.0f) throw std::invalid_argument("RainFastMultiplier must be 0..2");
    if (config.reloadKey == config.toggleKey) throw std::invalid_argument("ReloadKey must differ from ToggleKey");
    if (config.nightStartHour < 0 || config.nightStartHour > 23 || config.nightEndHour < 0 || config.nightEndHour > 23) throw std::invalid_argument("Night hours must be 0..23");
    if (config.zonePollMs < 250 || config.zonePollMs > 10000) throw std::invalid_argument("ZonePollMs must be 250..10000");
    if (config.sportCycleMs < 10000 || config.sportCycleMs > 600000) throw std::invalid_argument("SportCycleSeconds must be 10..600");
    if (config.repeatScanMs < 1000 || config.repeatScanMs > 60000) throw std::invalid_argument("RepeatScanMs must be 1000..60000");
    if (config.repeatRadiusMeters < 50.0f || config.repeatRadiusMeters > 500.0f) throw std::invalid_argument("RepeatRadiusMeters must be 50..500");
    if (config.repeatLimit < 2 || config.repeatLimit > 10) throw std::invalid_argument("RepeatLimit must be 2..10");
    if (config.repeatCooldownMs < 10000 || config.repeatCooldownMs > 600000) throw std::invalid_argument("RepeatCooldownSeconds must be 10..600");
    if (config.maxRepeatCooldownModels < 1 || config.maxRepeatCooldownModels > 100) throw std::invalid_argument("MaxRepeatCooldownModels must be 1..100");
    return config;
}

float ReadZoneFloat(const std::filesystem::path& iniPath, const std::string& section,
                    const char* key, float defaultValue) {
    char defaultBuffer[32]{};
    char value[32]{};
    sprintf_s(defaultBuffer, "%.3f", defaultValue);
    GetPrivateProfileStringA(section.c_str(), key, defaultBuffer, value,
                             static_cast<DWORD>(std::size(value)), iniPath.string().c_str());
    float parsed = 0.0f;
    try { parsed = std::stof(value); }
    catch (...) { throw std::invalid_argument(section + " " + key + " must be numeric"); }
    if (parsed < 0.0f || parsed > 1.0f) throw std::invalid_argument(section + " " + key + " must be 0..1");
    return parsed;
}

// Starts from the built-in zone table and overlays every [Zone.NAME] INI section,
// so zones can be retuned or added without rebuilding the plugin.
ZoneTable LoadZones(const std::filesystem::path& iniPath) {
    ZoneTable zones = BuildDefaultZones();
    std::vector<char> buffer(16384);
    const DWORD length = GetPrivateProfileSectionNamesA(
        buffer.data(), static_cast<DWORD>(buffer.size()), iniPath.string().c_str());
    for (DWORD offset = 0; offset < length;) {
        const std::string section(buffer.data() + offset);
        offset += static_cast<DWORD>(section.size()) + 1;
        if (section.size() <= 5 || section.compare(0, 5, "Zone.") != 0) continue;
        std::string name = section.substr(5);
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
        ZoneProfile profile = zones.count(name) ? zones[name] : ZoneProfile{};
        profile.daily = ReadZoneFloat(iniPath, section, "Daily", profile.daily);
        profile.luxury = ReadZoneFloat(iniPath, section, "Luxury", profile.luxury);
        profile.sport = ReadZoneFloat(iniPath, section, "Sport", profile.sport);
        profile.exotic = ReadZoneFloat(iniPath, section, "Exotic", profile.exotic);
        zones[name] = profile;
    }
    return zones;
}

// [Models] holds spawnname=multiplier lines (0 bans a model, 2 doubles its chance).
ModelWeights LoadModelWeights(const std::filesystem::path& iniPath) {
    ModelWeights weights;
    std::vector<char> buffer(32768);
    const DWORD length = GetPrivateProfileSectionA(
        "Models", buffer.data(), static_cast<DWORD>(buffer.size()), iniPath.string().c_str());
    for (DWORD offset = 0; offset < length;) {
        const std::string line(buffer.data() + offset);
        offset += static_cast<DWORD>(line.size()) + 1;
        if (line.empty() || line[0] == ';') continue;
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0) throw std::invalid_argument("[Models] line without '=': " + line);
        const std::string name = line.substr(0, equals);
        float value = 0.0f;
        try { value = std::stof(line.substr(equals + 1)); }
        catch (...) { throw std::invalid_argument("[Models] " + name + " must be numeric"); }
        if (value < 0.0f || value > 10.0f) throw std::invalid_argument("[Models] " + name + " must be 0..10");
        weights[HashModelName(name)] = value;
    }
    return weights;
}

bool IsWetWeather() {
    static const ModelHash wet[] = {
        HashModelName("RAIN"), HashModelName("THUNDER"), HashModelName("CLEARING"),
        HashModelName("SNOW"), HashModelName("BLIZZARD"), HashModelName("SNOWLIGHT"),
        HashModelName("XMAS")};
    const ModelHash weather = static_cast<ModelHash>(GAMEPLAY::_GET_CURRENT_WEATHER_TYPE());
    return std::find(std::begin(wet), std::end(wet), weather) != std::end(wet);
}

bool IsNightHour(const RuntimeConfig& config, int hour) {
    if (config.nightStartHour <= config.nightEndHour) {
        return hour >= config.nightStartHour && hour < config.nightEndHour;
    }
    return hour >= config.nightStartHour || hour < config.nightEndHour;
}

void SetSuppressed(ModelHash model, bool suppressed) {
    const auto found = g_suppressionState.find(model);
    const bool current = found != g_suppressionState.end() && found->second;
    if (current == suppressed) return;
    VEHICLE::SET_VEHICLE_MODEL_IS_SUPPRESSED(model, suppressed ? TRUE : FALSE);
    g_suppressionState[model] = suppressed;
}

bool IsRichCatalogModel(ModelHash model) {
    const auto contains = [model](const std::vector<ModelHash>& values) {
        return std::find(values.begin(), values.end(), model) != values.end();
    };
    return contains(g_catalog.daily) || contains(g_catalog.luxury) || contains(g_catalog.sport) ||
           contains(g_catalog.exotic);
}

void ReconcileSuppressions(bool controlled, ULONGLONG now) {
    std::unordered_set<ModelHash> models;
    for (const auto* group : {&g_catalog.daily, &g_catalog.luxury, &g_catalog.sport, &g_catalog.exotic}) {
        models.insert(group->begin(), group->end());
    }
    for (const auto& state : g_suppressionState) models.insert(state.first);
    if (g_repeatCooldowns) {
        for (const auto& cooldown : g_repeatCooldowns->Entries()) models.insert(cooldown.first);
    }

    for (const ModelHash model : models) {
        const bool categorySuppressed = controlled && IsRichCatalogModel(model) && g_selection.active.count(model) == 0;
        const bool repeatSuppressed = g_repeatCooldowns && g_repeatCooldowns->IsActive(model, now);
        SetSuppressed(model, categorySuppressed || repeatSuppressed);
    }
}

void ReleaseAll(const std::string& reason) {
    std::size_t released = 0;
    for (auto& state : g_suppressionState) {
        if (!state.second) continue;
        VEHICLE::SET_VEHICLE_MODEL_IS_SUPPRESSED(state.first, FALSE);
        state.second = false;
        ++released;
    }
    if (released > 0 || (g_repeatCooldowns && g_repeatCooldowns->Size() > 0)) {
        Log(reason + ": " + std::to_string(released) + " models released.");
    }
    if (g_repeatCooldowns) g_repeatCooldowns->Clear();
    g_selection = {};
    g_insideZone = false;
    g_currentZone.clear();
}

void ApplyRoll(const RuntimeConfig& config, const ZoneProfile& base, const std::string& zone,
               ULONGLONG now) {
    const bool night = IsNightHour(config, TIME::GET_CLOCK_HOURS());
    const bool wet = IsWetWeather();
    float fastMultiplier = 1.0f;
    if (night) fastMultiplier *= config.nightSportMultiplier;
    if (wet) fastMultiplier *= config.rainFastMultiplier;
    const ZoneProfile profile = ScaleFastTiers(base, fastMultiplier);
    g_lastProfile = profile;
    g_selection = BuildWeightedSelection(g_catalog, profile, g_modelWeights, g_random);
    ReconcileSuppressions(true, now);
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << "Roll in " << zone << (night ? " (night)" : "")
         << (wet ? " (wet)" : "") << ": active=" << g_selection.active.size()
         << " suppressed=" << g_selection.suppressed.size()
         << " p(daily/luxury/sport/exotic)=" << profile.daily << '/' << profile.luxury << '/'
         << profile.sport << '/' << profile.exotic;
    Log(text.str());
}

void ScanAntiRepeat(const RuntimeConfig& config, Ped player, const Vector3& playerPosition,
                    bool controlled, ULONGLONG now) {
    const Vehicle playerVehicle = PED::GET_VEHICLE_PED_IS_IN(player, FALSE);
    const float radiusSquared = config.repeatRadiusMeters * config.repeatRadiusMeters;
    std::vector<int> vehicles(2048);
    const int vehicleCount = worldGetAllVehicles(vehicles.data(), static_cast<int>(vehicles.size()));
    std::unordered_map<ModelHash, RepeatObservation> observed;

    for (int index = 0; index < vehicleCount; ++index) {
        const Vehicle vehicle = vehicles[static_cast<std::size_t>(index)];
        if (!ENTITY::DOES_ENTITY_EXIST(vehicle) || vehicle == playerVehicle) continue;
        if (ENTITY::IS_ENTITY_A_MISSION_ENTITY(vehicle)) continue;
        const Ped driver = VEHICLE::GET_PED_IN_VEHICLE_SEAT(vehicle, -1);
        if (!ENTITY::DOES_ENTITY_EXIST(driver)) continue;

        const ModelHash model = ENTITY::GET_ENTITY_MODEL(vehicle);
        if (!IsPassengerClass(VEHICLE::GET_VEHICLE_CLASS_FROM_NAME(model))) continue;
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(vehicle, TRUE);
        const float dx = position.x - playerPosition.x;
        const float dy = position.y - playerPosition.y;
        const float dz = position.z - playerPosition.z;
        const float distanceSquared = dx * dx + dy * dy + dz * dz;
        if (distanceSquared > radiusSquared) continue;

        auto& observation = observed[model];
        if (observation.count == 0) {
            observation.model = model;
            observation.nearestDistanceSquared = distanceSquared;
        } else {
            observation.nearestDistanceSquared = std::min(observation.nearestDistanceSquared, distanceSquared);
        }
        ++observation.count;
    }

    std::vector<RepeatObservation> values;
    values.reserve(observed.size());
    for (const auto& observation : observed) values.push_back(observation.second);
    const auto candidates = SelectRepeatCandidates(values, config.repeatLimit, config.maxRepeatCooldownModels);
    const auto events = g_repeatCooldowns->Update(
        candidates, now, config.repeatCooldownMs, config.repeatScanMs);
    if (config.logAntiRepeat) {
        for (const auto& event : events) {
            switch (event.type) {
            case CooldownEventType::Started: {
                const auto found = observed.find(event.model);
                const std::size_t count = found == observed.end() ? 0 : found->second.count;
                Log("Anti-repeat started: " + ModelName(event.model) + " x" +
                    std::to_string(count) + " within " +
                    std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m");
                break;
            }
            case CooldownEventType::Renewed:
                Log("Anti-repeat renewed: " + ModelName(event.model));
                break;
            case CooldownEventType::Expired:
                Log("Anti-repeat ended: " + ModelName(event.model));
                break;
            case CooldownEventType::Evicted:
                Log("Anti-repeat evicted by priority: " + ModelName(event.model));
                break;
            }
        }
    }
    ReconcileSuppressions(controlled, now);
}

enum class Category { Daily, Luxury, Sport, Exotic, Other };

Category Categorize(ModelHash model) {
    const auto contains = [model](const std::vector<ModelHash>& values) {
        return std::find(values.begin(), values.end(), model) != values.end();
    };
    if (contains(g_catalog.daily)) return Category::Daily;
    if (contains(g_catalog.luxury)) return Category::Luxury;
    if (contains(g_catalog.sport)) return Category::Sport;
    if (contains(g_catalog.exotic)) return Category::Exotic;
    return Category::Other;
}

void RecordTelemetry(const RuntimeConfig& config, const Vector3& playerPosition) {
    std::vector<int> vehicles(2048);
    const int count = worldGetAllVehicles(vehicles.data(), static_cast<int>(vehicles.size()));
    int daily = 0, luxury = 0, sport = 0, exotic = 0, other = 0, nearby = 0;
    const float radiusSquared = config.repeatRadiusMeters * config.repeatRadiusMeters;
    for (int index = 0; index < count; ++index) {
        const Entity vehicle = vehicles[static_cast<std::size_t>(index)];
        if (!ENTITY::DOES_ENTITY_EXIST(vehicle)) continue;
        const Vector3 position = ENTITY::GET_ENTITY_COORDS(vehicle, TRUE);
        const float dx = position.x - playerPosition.x;
        const float dy = position.y - playerPosition.y;
        const float dz = position.z - playerPosition.z;
        if (dx * dx + dy * dy + dz * dz > radiusSquared) continue;
        ++nearby;
        switch (Categorize(ENTITY::GET_ENTITY_MODEL(vehicle))) {
        case Category::Daily: ++daily; break;
        case Category::Luxury: ++luxury; break;
        case Category::Sport: ++sport; break;
        case Category::Exotic: ++exotic; break;
        default: ++other; break;
        }
    }
    Log("Telemetry " + std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m: total=" +
        std::to_string(nearby) + ", daily=" + std::to_string(daily) + ", luxury=" +
        std::to_string(luxury) + ", sport=" + std::to_string(sport) + ", exotic=" +
        std::to_string(exotic) + ", other=" +
        std::to_string(other) + ", cooldowns=" +
        std::to_string(g_repeatCooldowns ? g_repeatCooldowns->Size() : 0));
}

void DrawOverlay(const RuntimeConfig& config) {
    if (!config.debugOverlay) return;
    std::ostringstream text;
    text << "RichTrafficWeights v" << kVersion << " " << (g_runtimeEnabled ? "ON" : "OFF")
         << " | zone: " << (g_currentZone.empty() ? "?" : g_currentZone)
         << " | rich: " << (g_insideZone ? g_selection.active.size() : 0)
         << " | cooldowns: " << (g_repeatCooldowns ? g_repeatCooldowns->Size() : 0);
    if (g_insideZone) {
        text << std::fixed << std::setprecision(2) << " | p " << g_lastProfile.daily << '/'
             << g_lastProfile.luxury << '/' << g_lastProfile.sport << '/' << g_lastProfile.exotic;
    }
    std::string value = text.str();
    UI::SET_TEXT_FONT(0);
    UI::SET_TEXT_SCALE(0.0f, 0.30f);
    UI::SET_TEXT_COLOUR(255, 255, 255, 220);
    UI::SET_TEXT_OUTLINE();
    UI::_SET_TEXT_ENTRY(const_cast<char*>("STRING"));
    UI::_ADD_TEXT_COMPONENT_STRING(const_cast<char*>(value.c_str()));
    UI::_DRAW_TEXT(0.015f, 0.72f);
}

} // namespace

void SetPluginModule(HMODULE module) { g_module = module; }

void OnKeyboard(DWORD key, WORD, BYTE, BOOL, BOOL, BOOL wasDownBefore, BOOL isUpNow) {
    if (!g_configValid || !isUpNow || !wasDownBefore) return;
    if (key == g_toggleKey) g_toggleRequested.store(true);
    else if (g_reloadKey != 0 && key == g_reloadKey) g_reloadRequested.store(true);
}

void ScriptMain() {
    g_directory = GetModuleDirectory();
    g_logPath = g_directory / "RichTrafficWeights.log";
    const auto iniPath = g_directory / "RichTrafficWeights.ini";
    g_catalog = BuildCatalog();
    AddModelNames(kDailyModels);
    AddModelNames(kLuxuryModels);
    AddModelNames(kSportModels);
    AddModelNames(kExoticModels);

    RuntimeConfig config;
    g_zones = BuildDefaultZones();
    try {
        config = LoadConfig(iniPath);
        g_zones = LoadZones(iniPath);
        g_modelWeights = LoadModelWeights(iniPath);
    } catch (const std::exception& error) {
        // A bad INI value must not kill the mod: fall back to the built-in defaults.
        Log(std::string("Invalid configuration, using built-in defaults: ") + error.what());
        config = RuntimeConfig{};
        g_zones = BuildDefaultZones();
        g_modelWeights.clear();
    }
    try {
        if (!ValidateCatalog(g_catalog)) throw std::invalid_argument("catalog invalid");
        g_repeatCooldowns = std::make_unique<RepeatCooldowns>(config.maxRepeatCooldownModels);
        g_runtimeEnabled = config.enabled;
        g_toggleKey = config.toggleKey;
        g_reloadKey = config.reloadKey;
        Log("Initialized v" + std::string(kVersion) + "; catalog=" +
            std::to_string(g_catalog.daily.size()) + "/" + std::to_string(g_catalog.luxury.size()) +
            "/" + std::to_string(g_catalog.sport.size()) + "/" + std::to_string(g_catalog.exotic.size()) +
            " zones=" + std::to_string(g_zones.size()) + " model-weights=" +
            std::to_string(g_modelWeights.size()) + " anti-repeat=" +
            std::string(config.antiRepeatEnabled ? "ON" : "OFF") + " limit=" +
            std::to_string(config.repeatLimit) + " radius=" +
            std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m cooldown=" +
            std::to_string(config.repeatCooldownMs / 1000) + "s.");
    } catch (const std::exception& error) {
        Log(std::string("Catalog error; plugin disabled in fail-open: ") + error.what());
        g_runtimeEnabled = false;
        g_configValid = false;
    }

    ULONGLONG nextPoll = 0;
    ULONGLONG nextSportRotation = 0;
    ULONGLONG nextRepeatScan = 0;
    ULONGLONG nextTelemetry = 0;
    try {
        while (true) {
            if (g_toggleRequested.exchange(false)) {
                g_runtimeEnabled = !g_runtimeEnabled;
                Log(std::string("Toggled by key: ") + (g_runtimeEnabled ? "ON" : "OFF"));
                if (!g_runtimeEnabled) ReleaseAll("F10/fail-open");
                nextPoll = 0;
            }

            if (g_reloadRequested.exchange(false)) {
                // Reload everything first; keep the running config if any part is invalid.
                try {
                    RuntimeConfig reloaded = LoadConfig(iniPath);
                    ZoneTable zones = LoadZones(iniPath);
                    ModelWeights weights = LoadModelWeights(iniPath);
                    if (reloaded.maxRepeatCooldownModels != config.maxRepeatCooldownModels) {
                        g_repeatCooldowns = std::make_unique<RepeatCooldowns>(reloaded.maxRepeatCooldownModels);
                    }
                    config = reloaded;
                    g_zones = std::move(zones);
                    g_modelWeights = std::move(weights);
                    g_toggleKey = config.toggleKey;
                    g_reloadKey = config.reloadKey;
                    g_rolledZone.clear();
                    Log("INI reloaded: zones=" + std::to_string(g_zones.size()) + " model-weights=" +
                        std::to_string(g_modelWeights.size()));
                } catch (const std::exception& error) {
                    Log(std::string("INI reload failed, keeping previous settings: ") + error.what());
                }
                nextPoll = 0;
            }

            DrawOverlay(config);
            const ULONGLONG now = GetTickCount64();
            if (now < nextPoll) { WAIT(0); continue; }
            nextPoll = now + config.zonePollMs;

            if (!g_runtimeEnabled || NETWORK::NETWORK_IS_SESSION_ACTIVE()) {
                ReleaseAll(NETWORK::NETWORK_IS_SESSION_ACTIVE() ? "Online/fail-open" : "Disabled/fail-open");
                WAIT(0);
                continue;
            }

            const Ped player = PLAYER::PLAYER_PED_ID();
            if (!ENTITY::DOES_ENTITY_EXIST(player)) {
                ReleaseAll("Player unavailable/fail-open");
                WAIT(0);
                continue;
            }

            const Vector3 position = ENTITY::GET_ENTITY_COORDS(player, TRUE);
            const char* rawZone = ZONE::GET_NAME_OF_ZONE(position.x, position.y, position.z);
            const std::string zone = rawZone ? rawZone : "";
            const ZoneProfile* profile = FindZoneProfile(g_zones, zone);
            const bool controlled = profile != nullptr;
            g_currentZone = zone;

            if (controlled) {
                if (!g_insideZone || zone != g_rolledZone || now >= nextSportRotation) {
                    g_insideZone = true;
                    g_rolledZone = zone;
                    ApplyRoll(config, *profile, zone, now);
                    nextSportRotation = now + config.sportCycleMs;
                }
            } else if (g_insideZone) {
                Log("Left controlled zone, now in " + zone + "; keeping anti-repeat only.");
                g_insideZone = false;
                g_rolledZone.clear();
                g_selection = {};
                ReconcileSuppressions(false, now);
            }

            if (config.antiRepeatEnabled && now >= nextRepeatScan) {
                ScanAntiRepeat(config, player, position, controlled, now);
                nextRepeatScan = now + config.repeatScanMs;
            }
            if (config.debugTelemetry && now >= nextTelemetry) {
                RecordTelemetry(config, position);
                nextTelemetry = now + 10000;
            }
            WAIT(0);
        }
    } catch (const std::exception& error) {
        Log(std::string("Exception caught; releasing in fail-open: ") + error.what());
        ReleaseAll("Exception/fail-open");
    } catch (...) {
        Log("Unknown exception; releasing in fail-open.");
        ReleaseAll("Unknown exception/fail-open");
    }
    while (true) WAIT(1000);
}
