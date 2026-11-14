#include "signalscope/backend/dsp/recovery_export.hpp"
#include <array>
#include <ostream>
#include <streambuf>

namespace signalscope {
namespace {
class Buffer final:public std::streambuf {
public:
    Buffer(const RecoveryExportSink& sink,std::size_t limit,std::stop_token stop):sink_(sink),limit_(limit),stop_(stop) {
        if(!sink_ || !limit || limit>recoveryExportMaximumBytes)throw std::invalid_argument("Invalid recovery export limit or sink");
        setp(bytes_.data(),bytes_.data()+bytes_.size());
    }
    std::size_t total() const{return written_+static_cast<std::size_t>(pptr()-pbase());}
    void finish(){sync();}
protected:
    int sync() override {
        if(stop_.stop_requested())throw std::runtime_error("Recovery export canceled");
        const auto count=static_cast<std::size_t>(pptr()-pbase());
        if(count>limit_-written_)throw std::length_error("Recovery export exceeds its byte limit");
        if(count)sink_({pbase(),count});
        written_+=count;setp(bytes_.data(),bytes_.data()+bytes_.size());return 0;
    }
    int_type overflow(int_type character) override {
        sync();if(!traits_type::eq_int_type(character,traits_type::eof())){*pptr()=traits_type::to_char_type(character);pbump(1);}
        return traits_type::not_eof(character);
    }
private:
    const RecoveryExportSink& sink_;std::size_t limit_,written_=0;std::stop_token stop_;std::array<char,32768> bytes_;
};
Json range(SignalSampleRange value){return {{"first",std::to_string(value.first)},{"count",std::to_string(value.count)}};}
Json link(RecoveryLink value){return {{"sequence",value.sequence},{"first",value.first},{"count",value.count}};}
const char* kind(RecoveryKind value){return value==RecoveryKind::digital?"digital":value==RecoveryKind::measurement?"measurement":"unsupported";}
const char* kind(RecoveryValueKind value){constexpr std::array names{"symbol","bit","byte","complex"};return names.at(static_cast<unsigned>(value));}
const char* state(RecoveryFrameState value){constexpr std::array names{"candidate","validated","invalid","truncated"};return names.at(static_cast<unsigned>(value));}
const char* state(RecoveryCheckState value){constexpr std::array names{"unknown","not_applicable","absent","pass","fail"};return names.at(static_cast<unsigned>(value));}
}
bool recovery_bytes_exportable(const RecoveryResult& result,RecoveryLink selected) {
    if(result.kind!=RecoveryKind::digital || selected.sequence>=result.sequences.size() || !selected.count)return false;
    const auto& sequence=result.sequences[selected.sequence];
    if(sequence.kind!=RecoveryValueKind::byte || selected.first>sequence.values.size() || selected.count>sequence.values.size()-selected.first)return false;
    for(std::size_t i=selected.first;i<static_cast<std::size_t>(selected.first)+selected.count;++i)
        if(!sequence.values[i].known || sequence.values[i].value>255)return false;
    return true;
}
void export_recovery_json(const RecoveryResult& result,const Json& analysis,const RecoveryExportSink& sink,std::size_t limit,std::stop_token stop) {
    if(stop.stop_requested())throw std::runtime_error("Recovery export canceled");
    validate_recovery(result);
    Buffer buffer(sink,limit,stop);std::ostream out(&buffer);out.exceptions(std::ios::badbit|std::ios::failbit);
    out<<"{\"format\":\"org.amc.recovery-evidence\",\"version\":"<<result.protocol_version<<",\"source_analysis\":";
    const auto before=buffer.total();out<<analysis;
    if(buffer.total()-before>8*1024*1024)throw std::length_error("Recovery source envelope exceeds its limit");
    const auto rate=result.analysis->sample_rate();
    const auto clock=result.analysis->processed?result.analysis->processed->clock:SignalSampleClock{0,rate,rate};
    out<<",\"provider\":"<<Json(result.provider_id)<<",\"resources\":"<<Json(result.resource_namespace)
       <<",\"method_version\":"<<result.method_version<<",\"kind\":"<<Json(kind(result.kind))
       <<",\"range\":"<<range(result.range)<<",\"input_clock\":"<<Json{{"source_origin",std::to_string(clock.source_origin)},{"source_rate_hz",clock.source_rate},{"rate_hz",clock.rate}}
       <<",\"parameters\":"<<result.parameters<<",\"method\":"<<result.method<<",\"event\":"<<result.event<<",\"issues\":"<<result.issues
       <<",\"operations\":"<<std::to_string(result.operations)<<",\"structured_payload_bytes\":"<<result.payload_bytes()<<",\"stages\":[";
    bool comma=false;for(const auto& stage:result.stages){if(comma)out<<',';comma=true;out<<Json{{"id",stage.id},{"label_key",stage.label_key},{"inputs",stage.inputs},{"parameters",stage.parameters},{"features",stage.features},{"conclusion",stage.conclusion}};}
    out<<"],\"sequences\":[";comma=false;
    for(const auto& sequence:result.sequences) {
        if(comma)out<<',';
        comma=true;
        out<<"{\"id\":"<<Json(sequence.id)<<",\"label_key\":"<<Json(sequence.label_key)<<",\"stage\":"<<sequence.stage<<",\"kind\":"<<Json(kind(sequence.kind));
        if(result.protocol_version>=2)out<<",\"domain_key\":"<<Json(sequence.domain_key)<<",\"reference\":"<<(sequence.reference?"true":"false");
        out<<",\"values\":[";
        bool valueComma=false;for(const auto& value:sequence.values) {
            if(stop.stop_requested())throw std::runtime_error("Recovery export canceled");
            if(valueComma)out<<',';
            valueComma=true;
            // Unknown storage values are not recovered bytes. Preserve them
            // explicitly as candidates while making the exported value null.
            const auto actual=sequence.kind==RecoveryValueKind::complex?Json{{"real",value.complex_value.real()},{"imag",value.complex_value.imag()}}:Json(std::to_string(value.value));
            out<<"{\"known\":"<<(value.known?"true":"false")<<",\"value\":"<<(value.known?actual:Json(nullptr));
            if(!value.known)out<<",\"candidate_value\":"<<actual;
            if(result.protocol_version>=2)out<<",\"group\":"<<value.group<<",\"coordinate\":"<<value.coordinate;
            out<<",\"support\":"<<range(value.support)<<",\"metric\":"<<Json(value.metric)<<",\"issue_key\":"<<Json(value.issue_key)<<",\"inputs\":[";
            bool linkComma=false;for(const auto& input:value.inputs){if(linkComma)out<<',';linkComma=true;out<<link(input);}out<<"]}";
        }
        out<<"]}";
    }
    out<<"]";
    if(result.protocol_version>=2){out<<",\"metrics\":[";comma=false;for(const auto& m:result.metrics){if(comma)out<<',';comma=true;out<<Json{{"id",m.id},{"label_key",m.label_key},{"unit_key",m.unit_key},{"available",m.available},{"value",m.available?Json(m.value):Json(nullptr)},{"reason_key",m.reason_key}};}out<<"]";}
    out<<",\"frames\":[";comma=false;
    for(const auto& frame:result.frames) {
        if(comma)out<<',';
        comma=true;
        out<<"{\"id\":"<<Json(frame.id)<<",\"label_key\":"<<Json(frame.label_key)<<",\"validation_scope_key\":"<<Json(frame.validation_scope_key)
           <<",\"bytes\":"<<link({frame.sequence,frame.first,frame.count})<<",\"state\":"<<Json(state(frame.state))<<",\"fields\":"<<frame.fields<<",\"checks\":[";
        bool checkComma=false;for(const auto& check:frame.checks){if(checkComma)out<<',';checkComma=true;out<<Json{{"id",check.id},{"label_key",check.label_key},{"stage",check.stage},{"state",state(check.state)},{"required",check.required},{"evidence",check.evidence}};}
        out<<"]}";
    }
    out<<"]}\n";buffer.finish();
}
void export_recovery_bytes(const RecoveryResult& result,RecoveryLink selected,const RecoveryExportSink& sink,std::size_t limit,std::stop_token stop) {
    if(stop.stop_requested())throw std::runtime_error("Recovery export canceled");
    validate_recovery(result);
    if(!recovery_bytes_exportable(result,selected))throw std::invalid_argument("Binary recovery export requires a nonempty range of known bytes");
    Buffer buffer(sink,limit,stop);std::ostream out(&buffer);out.exceptions(std::ios::badbit|std::ios::failbit);
    const auto& values=result.sequences[selected.sequence].values;
    for(std::size_t i=selected.first;i<static_cast<std::size_t>(selected.first)+selected.count;++i)out.put(static_cast<char>(values[i].value));
    buffer.finish();
}
}
