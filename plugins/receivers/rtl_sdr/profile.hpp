#pragma once
#include "signalscope/adapters/receivers/configuration.hpp"
namespace signalscope {
ReceiverCapabilities rtlReceiverCapabilities(int,const std::vector<int>&,std::string_view={},std::string_view={});
}
