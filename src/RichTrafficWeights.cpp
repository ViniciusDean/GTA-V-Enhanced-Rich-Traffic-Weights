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
    SlotConfig slots{};
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
std::filesystem::path g_directory;
std::filesystem::path g_logPath;
Catalog g_catalog;
std::unique_ptr<SportRotation> g_sportRotation;
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

DWORD ParseToggleKey(const std::filesystem::path& iniPath) {
    char value[32]{};
    GetPrivateProfileStringA("RichTrafficWeights", "ToggleKey", "F10", value,
                             static_cast<DWORD>(std::size(value)), iniPath.string().c_str());
    std::string key(value);
    std::transform(key.begin(), key.end(), key.begin(),
                   [](unsigned char character) { return static_cast<char>(std::toupper(character)); });
    if (key.size() >= 2 && key[0] == 'F') {
        try {
            const int number = std::stoi(key.substr(1));
            if (number >= 1 && number <= 12) return static_cast<DWORD>(VK_F1 + number - 1);
        } catch (...) {}
    }
    throw std::invalid_argument("ToggleKey must be F1 through F12");
}

RuntimeConfig LoadConfig(const std::filesystem::path& iniPath) {
    RuntimeConfig config;
    config.enabled = ReadBool(iniPath, "Enabled", true);
    config.zonePollMs = static_cast<DWORD>(ReadInt(iniPath, "ZonePollMs", 1000));
    config.sportCycleMs = static_cast<DWORD>(ReadInt(iniPath, "SportCycleSeconds", 30)) * 1000;
    config.slots.daily = static_cast<std::size_t>(ReadInt(iniPath, "DailySlots", 109));
    config.slots.luxury = static_cast<std::size_t>(ReadInt(iniPath, "LuxurySlots", 35));
    config.slots.sport = static_cast<std::size_t>(ReadInt(iniPath, "SportSlots", 28));
    config.antiRepeatEnabled = ReadBool(iniPath, "AntiRepeatEnabled", true);
    config.repeatScanMs = static_cast<DWORD>(ReadInt(iniPath, "RepeatScanMs", 5000));
    config.repeatRadiusMeters = ReadFloat(iniPath, "RepeatRadiusMeters", 180.0f);
    config.repeatLimit = static_cast<std::size_t>(ReadInt(iniPath, "RepeatLimit", 2));
    config.repeatCooldownMs = static_cast<DWORD>(ReadInt(iniPath, "RepeatCooldownSeconds", 90)) * 1000;
    config.maxRepeatCooldownModels = static_cast<std::size_t>(ReadInt(iniPath, "MaxRepeatCooldownModels", 32));
    config.toggleKey = ParseToggleKey(iniPath);
    config.debugOverlay = ReadBool(iniPath, "DebugOverlay", false);
    config.debugTelemetry = ReadBool(iniPath, "DebugTelemetry", false);
    config.logAntiRepeat = ReadBool(iniPath, "LogAntiRepeat", true);

    if (config.zonePollMs < 250 || config.zonePollMs > 10000) throw std::invalid_argument("ZonePollMs must be 250..10000");
    if (config.sportCycleMs < 10000 || config.sportCycleMs > 600000) throw std::invalid_argument("SportCycleSeconds must be 10..600");
    if (config.repeatScanMs < 1000 || config.repeatScanMs > 60000) throw std::invalid_argument("RepeatScanMs must be 1000..60000");
    if (config.repeatRadiusMeters < 50.0f || config.repeatRadiusMeters > 500.0f) throw std::invalid_argument("RepeatRadiusMeters must be 50..500");
    if (config.repeatLimit < 2 || config.repeatLimit > 10) throw std::invalid_argument("RepeatLimit must be 2..10");
    if (config.repeatCooldownMs < 10000 || config.repeatCooldownMs > 600000) throw std::invalid_argument("RepeatCooldownSeconds must be 10..600");
    if (config.maxRepeatCooldownModels < 1 || config.maxRepeatCooldownModels > 100) throw std::invalid_argument("MaxRepeatCooldownModels must be 1..100");
    return config;
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
    return contains(g_catalog.daily) || contains(g_catalog.luxury) || contains(g_catalog.sport);
}

void ReconcileSuppressions(bool controlled, ULONGLONG now) {
    std::unordered_set<ModelHash> models;
    for (const auto* group : {&g_catalog.daily, &g_catalog.luxury, &g_catalog.sport}) {
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
        Log(reason + ": " + std::to_string(released) + " modelos liberados.");
    }
    if (g_repeatCooldowns) g_repeatCooldowns->Clear();
    g_selection = {};
    g_insideZone = false;
    g_currentZone.clear();
}

void ApplySportRotation(const RuntimeConfig& config, const std::string& zone, ULONGLONG now) {
    const auto sports = g_sportRotation->Next(config.slots.sport);
    g_selection = BuildSelection(g_catalog, config.slots, sports);
    ReconcileSuppressions(true, now);

    std::ostringstream names;
    for (std::size_t index = 0; index < sports.size(); ++index) {
        if (index > 0) names << ',';
        names << ModelName(sports[index]);
    }
    Log("Rotacao v1.1 em " + zone + ": ativos=" + std::to_string(g_selection.active.size()) +
        " (daily=" + std::to_string(config.slots.daily) + ", luxury=" +
        std::to_string(config.slots.luxury) + ", sport=" + std::to_string(config.slots.sport) +
        "); esportivos=" + names.str());
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
                Log("Anti-repeat iniciado: " + ModelName(event.model) + " x" +
                    std::to_string(count) + " em " +
                    std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m");
                break;
            }
            case CooldownEventType::Renewed:
                Log("Anti-repeat renovado: " + ModelName(event.model));
                break;
            case CooldownEventType::Expired:
                Log("Anti-repeat finalizado: " + ModelName(event.model));
                break;
            case CooldownEventType::Evicted:
                Log("Anti-repeat substituido por prioridade: " + ModelName(event.model));
                break;
            }
        }
    }
    ReconcileSuppressions(controlled, now);
}

enum class Category { Daily, Luxury, Sport, Other };

Category Categorize(ModelHash model) {
    const auto contains = [model](const std::vector<ModelHash>& values) {
        return std::find(values.begin(), values.end(), model) != values.end();
    };
    if (contains(g_catalog.daily)) return Category::Daily;
    if (contains(g_catalog.luxury)) return Category::Luxury;
    if (contains(g_catalog.sport)) return Category::Sport;
    return Category::Other;
}

void RecordTelemetry(const RuntimeConfig& config, const Vector3& playerPosition) {
    std::vector<int> vehicles(2048);
    const int count = worldGetAllVehicles(vehicles.data(), static_cast<int>(vehicles.size()));
    int daily = 0, luxury = 0, sport = 0, other = 0, nearby = 0;
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
        default: ++other; break;
        }
    }
    Log("Telemetria " + std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m: total=" +
        std::to_string(nearby) + ", daily=" + std::to_string(daily) + ", luxury=" +
        std::to_string(luxury) + ", sport=" + std::to_string(sport) + ", other=" +
        std::to_string(other) + ", cooldowns=" +
        std::to_string(g_repeatCooldowns ? g_repeatCooldowns->Size() : 0));
}

void DrawOverlay(const RuntimeConfig& config) {
    if (!config.debugOverlay) return;
    std::ostringstream text;
    text << "RichTrafficWeights v1.1 " << (g_runtimeEnabled ? "ON" : "OFF")
         << " | zona: " << (g_currentZone.empty() ? "?" : g_currentZone)
         << " | rich: " << (g_insideZone ? g_selection.active.size() : 0)
         << " | cooldowns: " << (g_repeatCooldowns ? g_repeatCooldowns->Size() : 0);
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
    if (g_configValid && key == g_toggleKey && isUpNow && wasDownBefore) g_toggleRequested.store(true);
}

void ScriptMain() {
    g_directory = GetModuleDirectory();
    g_logPath = g_directory / "RichTrafficWeights.log";
    const auto iniPath = g_directory / "RichTrafficWeights.ini";
    g_catalog = BuildCatalog();
    AddModelNames(kDailyModels);
    AddModelNames(kLuxuryModels);
    AddModelNames(kSportModels);

    RuntimeConfig config;
    try {
        config = LoadConfig(iniPath);
        if (!ValidateCatalog(g_catalog, config.slots)) throw std::invalid_argument("catalog or slot counts invalid");
        g_sportRotation = std::make_unique<SportRotation>(
            g_catalog.sport, static_cast<std::uint32_t>(GetTickCount64()));
        g_repeatCooldowns = std::make_unique<RepeatCooldowns>(config.maxRepeatCooldownModels);
        g_runtimeEnabled = config.enabled;
        g_toggleKey = config.toggleKey;
        Log("Inicializado v1.2.0; rich=109/35/98; ativos=109/35/28; vivanite2=REMOVIDO; anti-repeat global=" +
            std::string(config.antiRepeatEnabled ? "ON" : "OFF") + " limite=" +
            std::to_string(config.repeatLimit) + " raio=" +
            std::to_string(static_cast<int>(config.repeatRadiusMeters)) + "m cooldown=" +
            std::to_string(config.repeatCooldownMs / 1000) + "s.");
    } catch (const std::exception& error) {
        Log(std::string("Configuracao invalida; plugin desabilitado em fail-open: ") + error.what());
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
                Log(std::string("Alternado por tecla: ") + (g_runtimeEnabled ? "ON" : "OFF"));
                if (!g_runtimeEnabled) ReleaseAll("F10/fail-open");
                nextPoll = 0;
            }

            DrawOverlay(config);
            const ULONGLONG now = GetTickCount64();
            if (now < nextPoll) { WAIT(0); continue; }
            nextPoll = now + config.zonePollMs;

            if (!g_runtimeEnabled || NETWORK::NETWORK_IS_SESSION_ACTIVE()) {
                ReleaseAll(NETWORK::NETWORK_IS_SESSION_ACTIVE() ? "Online/fail-open" : "Desabilitado/fail-open");
                WAIT(0);
                continue;
            }

            const Ped player = PLAYER::PLAYER_PED_ID();
            if (!ENTITY::DOES_ENTITY_EXIST(player)) {
                ReleaseAll("Jogador indisponivel/fail-open");
                WAIT(0);
                continue;
            }

            const Vector3 position = ENTITY::GET_ENTITY_COORDS(player, TRUE);
            const char* rawZone = ZONE::GET_NAME_OF_ZONE(position.x, position.y, position.z);
            const std::string zone = rawZone ? rawZone : "";
            const bool controlled = IsControlledZone(zone);
            g_currentZone = zone;

            if (controlled) {
                if (!g_insideZone) {
                    g_insideZone = true;
                    Log("Entrada em zona 64/20/16: " + zone);
                    ApplySportRotation(config, zone, now);
                    nextSportRotation = now + config.sportCycleMs;
                } else if (now >= nextSportRotation) {
                    ApplySportRotation(config, zone, now);
                    nextSportRotation = now + config.sportCycleMs;
                }
            } else if (g_insideZone) {
                Log("Saida da zona 64/20/16: " + zone + "; mantendo somente anti-repeat global.");
                g_insideZone = false;
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
        Log(std::string("Excecao capturada; liberando em fail-open: ") + error.what());
        ReleaseAll("Excecao/fail-open");
    } catch (...) {
        Log("Excecao desconhecida; liberando em fail-open.");
        ReleaseAll("Excecao desconhecida/fail-open");
    }
    while (true) WAIT(1000);
}
