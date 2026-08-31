#pragma once

#include <array>
#include <string_view>

namespace richtraffic {

inline constexpr std::array<std::string_view, 109> kDailyModels = {
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
    "yosemite1500","monstrociti","tahoma","dynasty","tulip2"
};

inline constexpr std::array<std::string_view, 35> kLuxuryModels = {
    "windsor","windsor2","paragon","deity","jubilee","champion","paragon3",
    "superd","schafter2","felon","felon2","f620","exemplar","jackal","cavalcade3",
    "huntley","merula","bjxl","rocoto","serrano","sentinel","sentinel2","zion",
    "revolter","cypher","jugular","buffalo5","carbonizzare","alpha","bestiagts",
    "specter","italigtb","pariah","seven70","neo"
};

inline constexpr std::array<std::string_view, 98> kSportModels = {
    "osiris","coquette3","feltzer3","t20","nero","lynx","tempesta","fmj",
    "penetrator","hash_560dc4fc","reaper","cheetah2","cyclone","viseris","infernus2",
    "taipan","ruston","gp1","sc1","vagner","turismo2","xa21","gt500",
    "hash_ea048886","hash_62d3c810","imorgon","zorrusso","italirsx","thrax","furia",
    "italigto","emerus","coquette4","tigon","krieger","tenf2","ignus","virtue",
    "hash_494d98bf","corsita","sm722","entity3","stingergt","tenf","zeno","torero2",
    "cyclone2","niobe","envisage","turismo3","coquette5","banshee3","hash_02e3e08a",
    "banshee","bullet","coquette","feltzer2","infernus","ninef","ninef2","comet2",
    "rapidgt","rapidgt2","surano","schwarzer","voltic","tornado","jester","massacro",
    "furoregt","rapidgt4","sentinel5","suzume","cheetah3","gt750","luiva","xtreme",
    "astrale","itali2","cartuccia","lrcgt","velenogt","deveste","entity2","tezeract",
    "tyrant","autarch","prototipo","mamba","verlierer","locust","swinger","rapidgt3",
    "torero","casco","banshee2","specter2","comet3"
};

inline constexpr std::array<std::string_view, 12> kControlledZones = {
    "ROCKF","RICHM","RGLEN","BURTON","GOLF","CHIL",
    "DOWNT","PBOX","LEGSQU","SKID","TEXTI","ALTA"
};

} // namespace richtraffic
