#pragma once
#include "demod.hpp"

namespace signalscope::recovery::lora {
struct PhyConfiguration {DemodOptions options;Json parameters=Json::object();RecoveryData failure;};
Json phy_descriptions(const RecoveryContext&,const Json&);
PhyConfiguration parse_phy(const RecoveryContext&,const Json&);
}
