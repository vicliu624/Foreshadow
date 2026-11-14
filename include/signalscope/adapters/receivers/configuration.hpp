#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include <optional>
#include <string_view>

namespace signalscope {
struct ReceiverRange {
    double minimum=0,maximum=0,step=0;
    bool contains(double value) const;
};
void to_json(Json&,const ReceiverRange&);
void from_json(const Json&,ReceiverRange&);

// Facts reported by one device, plus explicitly attributed hardware profiles.
// Recommended rates are shortcuts, never the set of permitted rates.
struct ReceiverCapabilities {
    std::string backend,model;
    SampleFormat format=SampleFormat::cu8;
    std::vector<ReceiverRange> frequency_ranges,sample_rate_ranges;
    std::vector<std::uint32_t> sample_rates,recommended_rates;
    std::vector<double> filter_bandwidths;
    std::vector<std::string> antennas;
    std::vector<int> manual_gains_tenths;
    bool manual_gains_known=false;
    std::string frequency_source="unknown",rate_source="unknown";
    // Empty means no profiled restriction beyond the driver frequency ranges.
    std::string limited_antenna;
    double limited_antenna_max_hz=0;
    Json extensions=Json::object(); // Owned by the plugin, preserved losslessly.
    bool direct_sampling_supported=false;
    std::vector<ReceiverRange> direct_sampling_ranges;
};
void to_json(Json&,const ReceiverCapabilities&);
void from_json(const Json&,ReceiverCapabilities&);
std::string receiverInputMode(const ReceiverCapabilities&,const Config&);
std::vector<std::uint32_t> receiverRateChoices(const ReceiverCapabilities&);

struct ConfigurationIssue {
    std::string code,field,scope; // scope: hardware, analysis, information
    bool blocking=true;
    Json values=Json::object();
};
void to_json(Json&,const ConfigurationIssue&);
void from_json(const Json&,ConfigurationIssue&);
struct ReceiverPlan {
    double filter_bandwidth_hz=0;
    double maximum_search_width_hz=0;
    double bytes_per_second=0;
    std::uint32_t lora_analysis_rate_hz=0;
    std::size_t lora_channels=0;
    double complete_signal_search_width_hz=0;
    // Specification-derived nominal ADC bits, not measured ENOB or I/Q format.
    std::optional<unsigned> nominal_adc_bits;
    Json effective_configuration=Json::object();
    std::vector<ConfigurationIssue> issues;
    bool hardwareValid() const;
    bool valid() const;
};
void to_json(Json&,const ReceiverPlan&);
void from_json(const Json&,ReceiverPlan&);
ReceiverPlan planReceiverConfiguration(const ReceiverCapabilities&,const Config&);
void requireReceiverConfiguration(const ReceiverPlan&);
Json requestedReceiverConfiguration(const Config&);
}
