#include "signalscope/adapters/television/dtmb_receiver.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/transport/mpeg_ts.hpp"
#include <QElapsedTimer>
#include <QProcess>
#include <array>
#include <cmath>
#include <numbers>
#include <optional>

namespace signalscope::television {
namespace {
QString qtPath(const std::filesystem::path& value){return QString::fromUtf8(path_to_utf8(value));}
QString number(unsigned value){return QString::number(value);}
constexpr std::size_t pipeBudget=1024*1024,diagnosticBudget=16384;
Json facts(const QByteArray& diagnostics) {
    Json result=Json::object();
    for(const auto& line:diagnostics.split('\n')) {
        const auto equals=line.indexOf('=');
        if(equals>0 && equals<128)result[line.first(equals).toStdString()]=line.mid(equals+1).trimmed().toStdString();
    }
    return result;
}
QStringList frontendArguments(const DtmbMode& mode,unsigned workers,bool probe,unsigned iterations=32) {
    QStringList args{"--input-format","cf32","--auto-sync","--sync-frames","300","--acquisition-frames","16",
        "--timing-search-radius","2","--timing-search-threshold","0.45","--workers",number(workers),
        "--batch-frames",number(std::min(64u,std::max(8u,workers*2))),"--pn-mode","pn"+number(mode.pn_symbols),
        "--qam",QString::fromStdString(mode.qam),"--frame-body-mode",QString::fromStdString(mode.frame_body),
        "--normalization",mode.qam=="64qam"?"qam64":"qam","--equalizer","pn","--pn-estimator","wideband",
        "--pn-wideband-block-frames","2","--pn-wideband-header-observation",mode.pn_symbols==595?"direct":"core-postfix",
        "--pn-wideband-scale-estimator","masked-frame-taps","--pn-mmse","auto","--remove-dc",
        "--system-info-index",probe?QString("auto"):number(mode.system_info)};
    if(mode.pn_symbols!=595)args<<"--pn-schedule-tracking"<<"--pn-current-header-tracking";
    if(probe)args<<"--max-frames"<<number(iterations);
    args<<"-"<<"-";return args;
}
}
DtmbMode dtmbMode(unsigned pn,unsigned si,const std::string& body) {
    if(pn!=420 && pn!=595 && pn!=945)throw std::invalid_argument("dtmb.invalid_pn");
    if(si<3 || si>24)throw std::invalid_argument("dtmb.invalid_si");
    if(body!="auto" && body!="c1" && body!="c3780")throw std::invalid_argument("dtmb.invalid_body");
    if(body=="c1" && (pn!=595 || si<5 || si>10))throw std::invalid_argument("dtmb.unsupported_c1_profile");
    DtmbMode result;result.pn_symbols=pn;result.system_info=si;result.frame_body=body;
    result.interleaver=(si%2?"mode1":"mode2");
    if(si<=4){result.qam="4qam-nr";result.fec_rate=3;result.codewords=1;}
    else if(si<=10){result.qam="4qam";result.fec_rate=(si-5)/2+1;result.codewords=1;}
    else if(si<=16){result.qam="16qam";result.fec_rate=(si-11)/2+1;result.codewords=2;}
    else if(si<=18){result.qam="32qam";result.fec_rate=3;result.codewords=5;}
    else {result.qam="64qam";result.fec_rate=(si-19)/2+1;result.codewords=3;}
    return result;
}
std::filesystem::path dtmbHelperDirectory() {
    if(const auto path=environment_utf8("SIGNALSCOPE_DTMB_HELPER_PATH"))return path_from_utf8(*path);
    const auto exe=application_directory();
    for(const auto& path:{exe/"libexec/signalscope/dtmb",exe/"../libexec/signalscope/dtmb"})
        if(std::filesystem::is_directory(path))return std::filesystem::weakly_canonical(path);
    throw std::runtime_error("dtmb.helper_missing");
}
class DtmbStreamDecoder::Impl {
public:
    Impl(DtmbReceiveOptions value,TransportSink output,StatusSink status,std::function<bool()> cancelled)
        :options(std::move(value)),sink(std::move(output)),publishStatus(std::move(status)),cancel(std::move(cancelled)) {
        if(!sink)throw std::invalid_argument("dtmb.invalid_input");
        validateDtmbReceiveOptions(options);
        frequencyOffset=options.frequency_hz?options.frequency_hz-options.input.center_frequency_hz:0;
        probeTarget=static_cast<std::size_t>(std::ceil(options.input.sample_rate*.30))*8;
        if(options.helper_directory.empty())options.helper_directory=dtmbHelperDirectory();
        for(const auto* name:{"resample","frontend","demap","fec"})
            if(!std::filesystem::is_regular_file(executable(name)))throw std::runtime_error("dtmb.helper_missing");
        for(unsigned rate=1;rate<=3;++rate)
            if(!std::filesystem::is_regular_file(options.helper_directory/"data"/("dtmb_ldpc_rate"+std::to_string(rate)+".alist")))
                throw std::runtime_error("dtmb.helper_data_missing");
        if(options.pn_symbols && options.system_info)selected=dtmbMode(options.pn_symbols,options.system_info,options.frame_body);
        progress.start();state="searching";
    }
    ~Impl(){closeProcesses();}
    std::filesystem::path executable(const std::string& name) const {
#ifdef _WIN32
        return options.helper_directory/("amc-dtmb-"+name+".exe");
#else
        return options.helper_directory/("amc-dtmb-"+name);
#endif
    }
    void checkpoint() const {if(cancel && cancel())throw std::runtime_error("dtmb.cancelled");}
    void report(bool force=false) {
        checkpoint();if(publishStatus && (force || progress.elapsed()>=250)){publishStatus(snapshot());progress.restart();}
    }
    void tail(QByteArray& destination,const QByteArray& bytes) {
        destination+=bytes;if(destination.size()>diagnosticBudget){diagnosticDiscarded+=destination.size()-diagnosticBudget;destination=destination.last(diagnosticBudget);}
    }
    void drain(QProcess& process,QByteArray& diagnostic,bool transport) {
        process.setReadChannel(QProcess::StandardError);
        while(process.bytesAvailable()) {checkpoint();tail(diagnostic,process.read(diagnosticBudget));}
        process.setReadChannel(QProcess::StandardOutput);
        if(transport)while(process.bytesAvailable()) {
            checkpoint();const auto data=process.read(65536);
            const auto bytes=std::span(reinterpret_cast<const std::uint8_t*>(data.constData()),static_cast<std::size_t>(data.size()));
            transportBytes+=bytes.size();lastTransportSubmittedBytes=submittedBytes;catalog.feed(bytes);sink(bytes);
        }
        else while(process.bytesAvailable()){checkpoint();process.read(65536);}
    }
    void setup(QProcess& process,const std::string& name,const QStringList& args) {
        // Input/output are pipes. All helper arguments that name files are
        // ASCII paths relative to this native working directory, including
        // when the installed package itself contains non-BMP characters.
        process.setWorkingDirectory(qtPath(options.helper_directory));
        process.setProgram(qtPath(executable(name)));process.setArguments(args);
    }
    QByteArray finite(const std::string& name,const QStringList& args,std::span<const std::uint8_t> input,
                      QByteArray& diagnostic,std::size_t maximumOutput) {
        QProcess process;setup(process,name,args);process.start();
        if(!process.waitForStarted(3000))throw std::runtime_error("dtmb.helper_start");
        QElapsedTimer timer;timer.start();QByteArray output;std::size_t written=0;bool closed=false;
        try {
            while(process.state()!=QProcess::NotRunning) {
                checkpoint();
                if(timer.elapsed()>30000)throw std::runtime_error("dtmb.probe_timeout");
                bool progressed=false;
                if(written<input.size() && process.bytesToWrite()<qint64(pipeBudget)) {
                    const auto size=std::min<std::size_t>(65536,input.size()-written);
                    const auto accepted=process.write(reinterpret_cast<const char*>(input.data()+written),static_cast<qint64>(size));
                    if(accepted<0)throw std::runtime_error("dtmb.helper_input");written+=static_cast<std::size_t>(accepted);progressed=accepted>0;
                }
                if(written==input.size() && !closed){process.closeWriteChannel();closed=true;}
                if(process.bytesToWrite())progressed=process.waitForBytesWritten(progressed?0:2) || progressed;
                process.waitForReadyRead(0);
                process.setReadChannel(QProcess::StandardError);
                while(process.bytesAvailable()){tail(diagnostic,process.read(diagnosticBudget));progressed=true;}
                process.setReadChannel(QProcess::StandardOutput);
                while(process.bytesAvailable()) {
                    const auto bytes=process.read(65536);
                    progressed=true;
                    if(maximumOutput) {
                        if(output.size()+std::size_t(bytes.size())>maximumOutput)throw std::runtime_error("dtmb.probe_output_budget");
                        output+=bytes;
                    }
                }
                if(!progressed)process.waitForReadyRead(2);
            }
            tail(diagnostic,process.readAllStandardError());
            if(maximumOutput) {
                const auto remaining=process.readAllStandardOutput();
                if(output.size()+std::size_t(remaining.size())>maximumOutput)throw std::runtime_error("dtmb.probe_output_budget");
                output+=remaining;
            }
            if(name=="resample" && (process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0))
                throw std::runtime_error("dtmb.resample_failed");
            return output;
        }catch(...){process.kill();process.waitForFinished(1000);throw;}
    }
    bool acquire() {
        state="acquiring";report(true);QByteArray diagnostic;
        const QStringList args{"--input-rate",number(options.input.sample_rate),"--output-rate","7560000",
            "--input-format","cf32","--output-format","cf32","--workers",number(options.workers),"--auto-timing",
            "--pn-symbols",number(options.frame_body=="c1"?595:options.pn_symbols),"--input-budget-bytes",number(static_cast<unsigned>(probe.size())),"-","-"};
        // Only one finite input prefix and its rate-converted copy are kept.
        // The two budgets are explicit and do not depend on USB block counts.
        const auto maximum=static_cast<std::size_t>(std::ceil(double(probe.size())*7560000/(options.input.sample_rate*.999)))+1024;
        auto symbols=finite("resample",args,probe,diagnostic,maximum);
        timing=facts(diagnostic);
        if(timing.value("resample_protocol",std::string())!="3")throw std::runtime_error("dtmb.helper_protocol");
        try {
            const auto integer=[&](const char* key) {
                const auto value=timing.at(key).get<std::string>();std::size_t end=0;
                if(value.empty() || value.front()<'0' || value.front()>'9')throw std::runtime_error("dtmb.helper_protocol");
                const auto number=std::stoull(value,&end);
                if(end!=value.size())throw std::runtime_error("dtmb.helper_protocol");return number;
            };
            const auto phase=integer("timing_phase"),count=integer("timing_phase_count");
            if(count!=16 || phase>=count)throw std::runtime_error("dtmb.helper_protocol");
            timingPhase=static_cast<unsigned>(phase);clockMilliHz=integer("clock_rate_millihz");
            const auto nominal=std::uint64_t(options.input.sample_rate)*1000;
            if(clockMilliHz<nominal-nominal/1000 || clockMilliHz>nominal+nominal/1000)throw std::runtime_error("dtmb.helper_protocol");
        }catch(...){throw std::runtime_error("dtmb.helper_protocol");}
        timingReady=true;
        if(selected){state="configured";report(true);return true;}
        for(const auto pn:{945u,420u,595u}) {
            if(options.pn_symbols && pn!=options.pn_symbols)continue;
            if(options.frame_body=="c1" && pn!=595)continue;
            auto mode=dtmbMode(pn,options.system_info?options.system_info:(options.frame_body=="c1"?6:22),options.frame_body);
            diagnostic.clear();finite("frontend",frontendArguments(mode,options.workers,true),
                {reinterpret_cast<const std::uint8_t*>(symbols.constData()),static_cast<std::size_t>(symbols.size())},diagnostic,0);
            auto result=facts(diagnostic);++probeAttempts;
            observations.push_back({{"pn_symbols",pn},{"facts",result}});if(observations.size()>3)observations.erase(observations.begin());
            if(result.value("pn_schedule_locked","")=="true")state="pn_locked";
            if(result.value("system_info_auto_locked","")!="true"){report(true);continue;}
            const auto si=static_cast<unsigned>(std::stoul(result.at("system_info_index").get<std::string>()));
            if(options.system_info && si!=options.system_info)continue;
            const auto body=result.value("frame_body_mode",std::string(""));
            selected=dtmbMode(pn,si,body);modeOrigin="detected";state="system_info_locked";report(true);return true;
        }
        discardedProbeBytes+=probe.size();probe.clear();timingReady=false;++unlockedPrefixes;report(true);return false;
    }
    void startProcesses() {
        const auto& mode=selected.value();
        for(std::size_t i=0;i<4;++i){diagnostics[i].clear();stageFacts[i]=Json::object();}
        for(auto& process:processes)process=std::make_unique<QProcess>();
        for(std::size_t i=0;i<3;++i)processes[i]->setStandardOutputProcess(processes[i+1].get());
        setup(*processes[0],"resample",{"--input-rate",number(options.input.sample_rate),"--output-rate","7560000",
            "--input-format","cf32","--output-format","cf32","--workers",number(options.workers),"--timing-phase",number(timingPhase),
            "--clock-rate-millihz",QString::number(static_cast<qulonglong>(clockMilliHz)),"--chunk-samples","65536","-","-"});
        setup(*processes[1],"frontend",frontendArguments(mode,options.workers,false));
        setup(*processes[2],"demap",{"--qam",QString::fromStdString(mode.qam),"--mode",QString::fromStdString(mode.interleaver),
            "--phase","0","--workers",number(options.workers),"--chunk-symbols","65536","-","-"});
        setup(*processes[3],"fec",{"--fec-rate",number(mode.fec_rate),"--alist","data/dtmb_ldpc_rate"+number(mode.fec_rate)+".alist",
            "--qam",QString::fromStdString(mode.qam),"--codewords-per-frame",number(mode.codewords),"--workers",number(options.workers),
            "--decode-batch-frames","8","--max-iterations",number(options.maximum_iterations),"--retry-max-iterations","50",
            "--attenuation","0.65","--clean-frames-only","--require-output","--insert-discontinuity-packets","-","-"});
        for(int i=3;i>=0;--i){checkpoint();processes[i]->start();if(!processes[i]->waitForStarted(3000))throw std::runtime_error("dtmb.helper_start");}
        state="correcting";report(true);
    }
    void pump() {
        checkpoint();
        // This adapter runs on a worker without a Qt event loop. Poll each
        // process, especially the final output, before inspecting its buffer;
        // polling only the producer would deadlock the bounded OS pipes.
        for(std::size_t i=0;i<4;++i) {
            if(processes[i]->state()!=QProcess::NotRunning)processes[i]->waitForReadyRead(0);
            drain(*processes[i],diagnostics[i],i==3);
        }
        if(transportBytes>epochTransportBytes)state="transport";report();
    }
    void send(std::span<const std::uint8_t> bytes) {
        if(!processes[0])startProcesses();
        auto& input=*processes[0];std::size_t sent=0;QElapsedTimer timer;timer.start();
        while(sent<bytes.size()) {
            pump();
            if(input.state()==QProcess::NotRunning)throw std::runtime_error("dtmb.helper_failed");
            if(input.bytesToWrite()<qint64(pipeBudget)) {
                const auto size=std::min<std::size_t>(65536,bytes.size()-sent);
                const auto count=input.write(reinterpret_cast<const char*>(bytes.data()+sent),static_cast<qint64>(size));
                if(count<0)throw std::runtime_error("dtmb.helper_input");
                sent+=static_cast<std::size_t>(count);submittedBytes+=static_cast<std::size_t>(count);timer.restart();
            }else input.waitForBytesWritten(2);
            if(timer.elapsed()>15000)throw std::runtime_error("dtmb.pipeline_stalled");
        }
    }
    void accept(std::span<const std::uint8_t> input) {
        if(selected && timingReady){send(input);return;}
        while(!input.empty()) {
            const auto count=std::min(probeTarget-probe.size(),input.size());
            probe.insert(probe.end(),input.begin(),input.begin()+static_cast<std::ptrdiff_t>(count));input=input.subspan(count);
            if(probe.size()<probeTarget)continue;
            if(acquire()){send(probe);probe.clear();if(!input.empty())send(input);return;}
        }
    }
    void reacquire(bool sourceGap) {
        closeProcesses();probe.clear();selected.reset();timingReady=false;timing=Json::object();catalog.discontinuity();++epoch;
        if(sourceGap)++inputGaps;else ++lockLosses;
        lastTransportSubmittedBytes=submittedBytes;epochTransportBytes=transportBytes;state="recovering";modeOrigin="";
        if(options.pn_symbols && options.system_info){selected=dtmbMode(options.pn_symbols,options.system_info,options.frame_body);modeOrigin="manual";}
        report(true);
    }
    void feed(const IqBlockView& block) {
        checkpoint();if(ended)throw std::logic_error("dtmb.input_ended");
        if(block.stream.sample_rate!=options.input.sample_rate || block.stream.format!=options.input.format ||
           block.stream.center_frequency_hz!=options.input.center_frequency_hz)throw std::invalid_argument("dtmb.source_changed");
        const auto& codec=sample_codec(block.stream.format);
        if(block.bytes.size()%codec.pair_bytes)throw std::invalid_argument("dtmb.partial_sample");
        const auto count=block.bytes.size()/codec.pair_bytes;
        if(block.first_sample>UINT64_MAX-count)throw std::invalid_argument("dtmb.sample_overflow");
        if(next && (block.discontinuous || *next!=block.first_sample)) {
            reacquire(true);
        }
        next=block.first_sample+count;inputPairs+=count;
        Bytes normalized;
        for(std::size_t first=0;first<count;first+=32768) {
            checkpoint();const auto size=std::min<std::size_t>(32768,count-first);normalized.resize(size*8);
            const auto phase=-2*std::numbers::pi_v<long double>*frequencyOffset/options.input.sample_rate;
            auto carrier=std::polar(1.,static_cast<double>(std::remainder(phase*(block.first_sample+first),2*std::numbers::pi_v<long double>)));
            const auto step=std::polar(1.,static_cast<double>(phase));
            bool nonzero=false;
            for(std::size_t i=0;i<size;++i) {
                auto value=codec.decode(block.bytes.data()+(first+i)*codec.pair_bytes);
                nonzero=nonzero || value.real()!=0 || value.imag()!=0;
                if(frequencyOffset){value*=static_cast<std::complex<float>>(carrier);carrier*=step;}
                write_iq_pair(normalized.data()+i*8,SampleFormat::cf32_le,value);
            }
            if(!nonzero) {
                silentInputPairs+=size;
                if(!signalAbsent){if(selected)reacquire(false);else probe.clear();state="signal_absent";report(true);}
                signalAbsent=true;continue;
            }
            signalAbsent=false;accept(normalized);
            // Count source time actually accepted by the bounded pipeline.
            // Wall time alone would mistake a paused file or slow computer for
            // RF lock loss. Two seconds without corrected TS triggers a fresh
            // acquisition even when the USB source itself has no sample gap.
            if(selected && timingReady && submittedBytes-lastTransportSubmittedBytes>std::uint64_t(options.input.sample_rate)*8*2)reacquire(false);
        }
        report();
    }
    void finish() {
        checkpoint();if(ended)return;ended=true;
        if(!timingReady && !probe.empty() && acquire()){send(probe);probe.clear();}
        if(!processes[0])throw std::runtime_error(inputPairs?"dtmb.no_lock":"dtmb.no_input");
        processes[0]->closeWriteChannel();QElapsedTimer timer;timer.start();
        while(true) {
            pump();bool running=false;
            for(auto& process:processes)if(process->state()!=QProcess::NotRunning){process->waitForFinished(2);running|=process->state()!=QProcess::NotRunning;}
            if(!running)break;
            if(timer.elapsed()>30000)throw std::runtime_error("dtmb.drain_timeout");
        }
        pump();catalog.finish();
        for(std::size_t i=0;i<4;++i)stageFacts[i]=facts(diagnostics[i]);
        for(std::size_t i=0;i<4;++i) {
            if(processes[i]->exitStatus()!=QProcess::NormalExit || processes[i]->exitCode()!=0)
                throw std::runtime_error(i==3 && transportBytes==epochTransportBytes?"dtmb.no_transport":"dtmb.helper_failed");
        }
        if(transportBytes==epochTransportBytes)throw std::runtime_error("dtmb.no_transport");
        state="finished";report(true);
    }
    void closeProcesses() {
        for(auto& process:processes)if(process && process->state()!=QProcess::NotRunning)process->kill();
        for(std::size_t i=0;i<4;++i)if(processes[i]) {
            processes[i]->waitForFinished(1000);
            tail(diagnostics[i],processes[i]->readAllStandardError());stageFacts[i]=facts(diagnostics[i]);processes[i].reset();
        }
    }
    Json snapshot() const {
        Json mode=Json::object();if(selected)mode={{"pn_symbols",selected->pn_symbols},{"system_info",selected->system_info},
            {"qam",selected->qam},{"fec_rate",selected->fec_rate},{"interleaver",selected->interleaver},
            {"frame_body",selected->frame_body},{"origin",modeOrigin.empty()?"manual":modeOrigin}};
        Json stages=Json::array();
        for(std::size_t i=0;i<4;++i)stages.push_back({{"index",i},{"facts",stageFacts[i]},
            {"diagnostics",diagnostics[i].toStdString()},{"pending_write_bytes",processes[i]?processes[i]->bytesToWrite():0}});
        return {{"state",state},{"error_key",errorKey},{"epoch",epoch},{"input_pairs",inputPairs},{"input_gaps",inputGaps},{"lock_losses",lockLosses},{"transport_bytes",transportBytes},
            {"input_pairs_since_transport",(submittedBytes-lastTransportSubmittedBytes)/8},{"no_transport_reacquisition_seconds",2},{"silent_input_pairs",silentInputPairs},
            {"probe_pending_bytes",probe.size()},{"probe_budget_bytes",options.probe_bytes},{"probe_target_bytes",probeTarget},
            {"discarded_probe_bytes",discardedProbeBytes},{"probe_attempts",probeAttempts},{"unlocked_prefixes",unlockedPrefixes},
            {"diagnostic_discarded_bytes",diagnosticDiscarded},{"mode",std::move(mode)},{"observations",observations},
            {"symbol_timing",timing},{"transport",catalog.snapshot()},{"stages",std::move(stages)}};
    }
    void failed(const std::exception& error) noexcept {
        try {
            errorKey=error.what();state=errorKey=="dtmb.cancelled"?"stopped":"failed";closeProcesses();
            if(publishStatus)publishStatus(snapshot());
        }catch(...){}
    }
    DtmbReceiveOptions options;TransportSink sink;StatusSink publishStatus;std::function<bool()> cancel;
    std::array<std::unique_ptr<QProcess>,4> processes;
    std::array<QByteArray,4> diagnostics;std::array<Json,4> stageFacts;
    QElapsedTimer progress;
    Bytes probe;std::size_t probeTarget=0;
    std::optional<DtmbMode> selected;std::optional<std::uint64_t> next;
    transport::MpegTsCatalog catalog;
    Json observations=Json::array(),timing=Json::object();unsigned timingPhase=0;std::uint64_t clockMilliHz=0;
    std::string state,modeOrigin,errorKey;double frequencyOffset=0;bool ended=false,signalAbsent=false,timingReady=false;
    std::uint64_t inputPairs=0,inputGaps=0,epoch=0,transportBytes=0,epochTransportBytes=0,
        discardedProbeBytes=0,probeAttempts=0,unlockedPrefixes=0,diagnosticDiscarded=0,submittedBytes=0,lastTransportSubmittedBytes=0,lockLosses=0,silentInputPairs=0;
};
DtmbStreamDecoder::DtmbStreamDecoder(DtmbReceiveOptions options,TransportSink sink,StatusSink status,std::function<bool()> cancel)
    :impl_(std::make_unique<Impl>(std::move(options),std::move(sink),std::move(status),std::move(cancel))){}
DtmbStreamDecoder::~DtmbStreamDecoder()=default;
void DtmbStreamDecoder::feed(const IqBlockView& block){try{impl_->feed(block);}catch(const std::exception& error){impl_->failed(error);throw;}}
void DtmbStreamDecoder::finish(){try{impl_->finish();}catch(const std::exception& error){impl_->failed(error);throw;}}
Json DtmbStreamDecoder::snapshot() const{return impl_->snapshot();}
}
