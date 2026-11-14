#include "service.hpp"
#ifdef SIGNALSCOPE_HAS_SOAPY
#include "signalscope/backend/iq/path.hpp"
#include <SoapySDR/Registry.hpp>
#include <cctype>
namespace signalscope::sdrplay_service {
std::string endpoint(const Json& settings) {
    if(!settings.is_object())throw std::invalid_argument("Receiver settings must be an object");
    const auto fallback=environment_utf8("FORESHADOW_SDRPLAY_SERVICE").value_or("tcp://127.0.0.1:55132");
    auto value=settings.value("service_endpoint",fallback);
    if(value.empty())value=fallback;
    if(!value.starts_with("tcp://"))value="tcp://"+value;
    const auto authority=value.substr(6);
    const auto colon=authority.rfind(':');
    if(authority.empty() || authority.find_first_of("/@?#\\")!=std::string::npos ||
       std::any_of(authority.begin(),authority.end(),[](unsigned char c){return std::isspace(c) || c<32;}) ||
       colon==std::string::npos || colon==0 || colon+1==authority.size())
        throw std::invalid_argument("Invalid receiver service endpoint; expected tcp://host:port");
    const auto port=authority.substr(colon+1);
    if(port.size()>5 || !std::all_of(port.begin(),port.end(),[](unsigned char c){return c>='0' && c<='9';}) ||
       std::stoul(port)==0 || std::stoul(port)>65535)
        throw std::invalid_argument("Receiver service port must be in 1..65535");
    return value;
}
Json parameters() {
    return Json::array({{{"key","service_endpoint"},{"type","string"},{"extension",true},
        {"label_key","connection.endpoint.label"},{"description_key","connection.endpoint.description"},
        {"default",endpoint(Json::object())},{"requires_stop",true},{"connection",true}}});
}
SoapySDR::KwargsList enumerate(const Json& settings) {
    // Deliberately call only the statically assembled remote client. The
    // global Device factory auto-loads system drivers even with driver=remote.
    const auto functions=SoapySDR::Registry::listFindFunctions();
    const auto remote=functions.find("remote");
    if(remote==functions.end())throw std::runtime_error("Remote client registration is unavailable");
    return remote->second({{"driver","remote"},{"remote:driver","sdrplay"},
                           {"remote",endpoint(settings)},{"remote:timeout","1000000"}});
}
SoapySDR::Device* open(SoapySDR::Kwargs args) {
    args["remote:prot"]="tcp";args["remote:timeout"]="1000000";
    const auto functions=SoapySDR::Registry::listMakeFunctions();
    const auto remote=functions.find("remote");
    if(remote==functions.end())throw std::runtime_error("Remote client registration is unavailable");
    return remote->second(args);
}
}
#endif
