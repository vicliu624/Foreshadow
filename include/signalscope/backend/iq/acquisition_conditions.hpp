#pragma once
#include "signalscope/backend/iq/iq.hpp"

namespace signalscope {
inline constexpr unsigned acquisitionConditionsProtocolVersion=1;
// Provider facts at startup, before the first delivered sample. In particular,
// a startup ADC observation is not an observation covering a later window.
// Calibration claims do not by themselves define a dBFS-to-dBm conversion.
class AcquisitionConditions final {
public:
    AcquisitionConditions(); // Legacy/no declaration: every condition unknown.
    explicit AcquisitionConditions(Json);
    const Json& json() const noexcept {return report_;}
    bool operator==(const AcquisitionConditions&) const=default;
private:
    Json report_;
};
void to_json(Json&,const AcquisitionConditions&);
void from_json(const Json&,AcquisitionConditions&);
}
