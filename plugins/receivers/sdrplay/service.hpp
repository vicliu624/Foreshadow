#pragma once
#include "signalscope/adapters/receivers/receiver.hpp"
#ifdef SIGNALSCOPE_HAS_SOAPY
#include <SoapySDR/Device.hpp>
namespace signalscope::sdrplay_service {
Json parameters();
std::string endpoint(const Json&);
SoapySDR::KwargsList enumerate(const Json&);
SoapySDR::Device* open(SoapySDR::Kwargs);
}
#endif
