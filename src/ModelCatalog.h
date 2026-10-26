#pragma once

#include <array>
#include <string_view>

namespace richtraffic {

inline constexpr std::array<std::string_view, 116> kDailyModels = {
    "kuruma","xls","brioso","neon","streiter","raiden","novak","landstalker2",
    "tailgater2","komoda","vstr","drafter","seminole2","rebla","sugoi","asbo",
    "club","astron","buffalo4","rhinehart","granger2","cinquemila","iwagen",
    "baller7","dorado","fr36","asterope2","chavosv6","baller8","vivanite",
    "vorschlaghammer","castigator","aleutian","asterope","baller","baller2",
    "buffalo","blista","cavalcade","cavalcade2","gresley","dilettante","fugitive",
    "habanero","issi2","granger","landstalker","oracle","oracle2","patriot",
    "penumbra","radi","seminole","stanier","surge","washington","tailgater",
    "buffalo2","everon3","hardy","minimus","woodlander","sentinel6","estride",
    "laufer","issi8","patriot3","warrener2","brioso2","penumbra2","asea",
    "intruder","ingot","minivan","premier","primo","regina","stratum","youga","panto",
    "weevil","kanjo","kanjosj","previon","nebula","michelli","retinue","retinue2",
    "sultan2","sultan3","calico","vectre","futo2","greenwood","eudora","postlude",
    "boor","freecrawler","rumpo3","journey2","surfer3","contender","keitora","l352",
    "yosemite1500","monstrociti","tahoma","dynasty","tulip2",
    "tampa4","clique2","impaler5","impaler6","vigero3","terminus","caracara3"
};

inline constexpr std::array<std::string_view, 38> kLuxuryModels = {
    "windsor","windsor2","paragon","deity","jubilee","champion","paragon3",
    "superd","schafter2","felon","felon2","f620","exemplar","jackal","cavalcade3",
    "huntley","merula","bjxl","rocoto","serrano","sentinel","sentinel2","zion",
    "revolter","cypher","jugular","buffalo5","carbonizzare","alpha","bestiagts",
    "specter","italigtb","pariah","seven70","neo",
    "warden","broadway","brigham"
};

inline constexpr std::array<std::string_view, 68> kSportModels = {
    "coquette3", "feltzer3", "lynx", "hash_560dc4fc", "cheetah2", "viseris",
    "infernus2", "ruston", "turismo2", "gt500", "hash_ea048886", "hash_62d3c810",
    "imorgon", "italirsx", "italigto", "coquette4", "tenf2", "hash_494d98bf",
    "corsita", "sm722", "stingergt", "tenf", "niobe", "envisage", "coquette5",
    "banshee3", "hash_02e3e08a", "banshee", "coquette", "feltzer2", "ninef",
    "ninef2", "comet2", "rapidgt", "rapidgt2", "surano", "schwarzer", "tornado",
    "jester", "massacro", "furoregt", "rapidgt4", "sentinel5", "cheetah3", "gt750",
    "astrale", "itali2", "cartuccia", "mamba", "verlierer", "locust", "swinger",
    "rapidgt3", "torero", "casco", "specter2", "comet3", "coquette6", "jester5",
    "eurosx32", "coureur", "stingertt", "panthere", "everon2", "gauntlet6",
    "uranus", "dominator9", "dominator10"
};

// Supercars and hypercars: own tier so they can be rarer than regular sports cars.
inline constexpr std::array<std::string_view, 44> kExoticModels = {
    "osiris", "t20", "nero", "tempesta", "fmj", "penetrator", "reaper", "cyclone",
    "taipan", "gp1", "sc1", "vagner", "xa21", "zorrusso", "thrax", "furia",
    "emerus", "tigon", "krieger", "ignus", "virtue", "entity3", "zeno", "torero2",
    "cyclone2", "turismo3", "bullet", "infernus", "voltic", "suzume", "luiva",
    "xtreme", "lrcgt", "velenogt", "deveste", "entity2", "tezeract", "tyrant",
    "autarch", "prototipo", "banshee2", "horus", "pipistrello", "fmj2"
};

// Default per-zone probability that each model of a tier is allowed to spawn
// (1.0 = every model, 0.0 = none). Overridable via [Zone.NAME] in the INI.
struct DefaultZone {
    std::string_view name;
    float daily;
    float luxury;
    float sport;
    float exotic;
};

inline constexpr std::array<DefaultZone, 28> kDefaultZones = {{
    // Very rich
    {"ROCKF", 1.0f, 1.0f, 0.35f, 0.15f}, {"RICHM", 1.0f, 1.0f, 0.35f, 0.15f},
    {"RGLEN", 1.0f, 1.0f, 0.35f, 0.15f}, {"BURTON", 1.0f, 1.0f, 0.35f, 0.15f},
    {"GOLF", 1.0f, 1.0f, 0.35f, 0.15f}, {"CHIL", 1.0f, 1.0f, 0.35f, 0.15f},
    {"PBLUFF", 1.0f, 0.9f, 0.30f, 0.12f},
    // Rich
    {"DOWNT", 0.9f, 0.8f, 0.30f, 0.12f}, {"PBOX", 0.9f, 0.7f, 0.25f, 0.08f},
    {"LEGSQU", 0.9f, 0.6f, 0.25f, 0.08f}, {"SKID", 0.9f, 0.6f, 0.25f, 0.08f},
    {"TEXTI", 0.9f, 0.6f, 0.25f, 0.08f}, {"ALTA", 0.9f, 0.6f, 0.20f, 0.08f},
    {"MORN", 0.9f, 0.8f, 0.25f, 0.08f}, {"DELBE", 0.9f, 0.8f, 0.25f, 0.08f},
    {"DELPE", 0.9f, 0.7f, 0.25f, 0.08f}, {"WVINE", 0.9f, 0.6f, 0.20f, 0.08f},
    {"HAWICK", 0.9f, 0.6f, 0.20f, 0.08f}, {"VCANA", 0.9f, 0.6f, 0.20f, 0.08f},
    // Poor: mostly ordinary cars
    {"DAVIS", 0.85f, 0.15f, 0.04f, 0.01f}, {"STRAW", 0.85f, 0.20f, 0.05f, 0.01f},
    {"CHAMH", 0.85f, 0.15f, 0.04f, 0.01f}, {"RANCHO", 0.85f, 0.15f, 0.04f, 0.01f},
    {"EBURO", 0.85f, 0.25f, 0.06f, 0.01f}, {"LMESA", 0.9f, 0.30f, 0.08f, 0.01f},
    {"CYPRE", 0.85f, 0.25f, 0.06f, 0.01f}, {"ELYSIAN", 0.85f, 0.20f, 0.05f, 0.01f},
    {"ZP_ORT", 0.85f, 0.20f, 0.05f, 0.01f}
}};

} // namespace richtraffic
