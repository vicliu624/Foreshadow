#include "detection.hpp"
#include "core/demod.hpp"
#include "core/input_policy.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <cmath>
namespace signalscope::adsb {
namespace {
const TextCatalog& catalog(){static const auto value=TextCatalog::resource("adsb");return value;}
Json text(const char* key){return {{"message_key",key}};}
void analyze(const SnapshotInput& input,Json& events) {
    if(!covers_band(input.center,input.rate,input.rate) || std::abs(input.center-frequency_hz)>input.searchWidth/2)return;
    Demodulator demod(input.rate);
    demod.feed(input.samples,input.first,false,[&](const Frame& frame){
        Json display={{"label_key","event"}};catalog().resolve(display);
        Json event={{"modulation_id","adsb"},{"modulation","ADS-B"},{"display_label",display.at("label")},
            {"evidence_level","adsb_crc_valid"},{"center_frequency_hz",1090000000.},
            {"first_observed_sample",frame.first_sample},{"last_observed_sample",frame.first_sample+std::uint64_t(std::ceil(input.rate*.000120))},
            {"payload_decoded",frame.fields.aircraft_fields},{"raw_message",frame.fields.hex},{"downlink_format",frame.fields.downlink_format},
            {"address",frame.fields.address},{"control",frame.fields.control},{"type_code",frame.fields.type_code},
            {"features",Json::array({{{"key","raw_message"},{"label_key","raw"},{"value",frame.fields.hex}},
                {{"key","type_code"},{"label_key","type"},{"value",frame.fields.type_code}}})}};
        if(!frame.fields.callsign.empty())event["features"].push_back({{"key","callsign"},{"label_key","callsign"},{"value",frame.fields.callsign}});
        catalog().resolve(event);events.push_back(std::move(event));
    });
}
}
SnapshotExtension detection_extension() {
    ModulationDescriptor descriptor{"adsb","adsb",DetectorStage::snapshot,
        [](DetectorSelection& selection)->bool&{return selection.snapshot.extensions["adsb"];},
        [](const DetectorContext& context){Json result={{"label_key","label"},
            {"covered",covers_band(context.receiver.frequency,context.maximum_span_hz,context.receiver.rate,context.receiver.if_bandwidth_hz)},
            {"coverage_reason",text("outside")},{"parsing",text("parsing")},{"effect",text("effect")},
            {"parsing_summary",text("summary")},{"impact_summary",text("impact")}};catalog().resolve(result);return result;}};
    return {std::move(descriptor),analyze,{SnapshotTrigger::periodic,.05,1.,2000000}};
}
}
