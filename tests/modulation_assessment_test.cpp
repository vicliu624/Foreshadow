#include "plugins/receivers/rtl_sdr/profile.hpp"
#include "modulation_assessment.hpp"
#include <QCoreApplication>
#include <QVariant>
#include <iostream>
#include <stdexcept>
using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
const ModulationAssessment& row(const QList<ModulationAssessment>& rows,const QString& id) {
    for(const auto& entry:rows)if(entry.id==id)return entry;
    throw std::runtime_error("Detector missing from modulation assessment");
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    // The assertions below check English provider wording. An operator's
    // system language must not select a different, valid translation here.
    app.setProperty("signalscope-language",QStringLiteral("en"));
    try {
        auto caps=rtlReceiverCapabilities(5,{});Config cfg;cfg.auto_gain=true;
        LoRaConfig lc;lc.bandwidth_candidates={125000,250000,500000};
        NarrowbandSelection narrow;SnapshotSelection snapshots;snapshots.ofdm=true;snapshots.css=true;
        auto rows=assessReceiverModulations(caps,cfg,lc,true,narrow,snapshots);
        require(rows.size()==1+int(narrowband_options.size())+int(snapshotDetectorOptions().size()),"Assessment does not cover the detector registry");
        require(row(rows,"lora").compatible && row(rows,"bpsk").enabled && !row(rows,"fm").covered,
            "Assessment ignored detector settings or broadcast FM frequency restrictions");
        require(row(rows,"lora").parsing.contains("not decoded") && row(rows,"ofdm").impact.contains("does not crop") &&
            row(rows,"css").impact.contains("40 kHz") && row(rows,"bpsk").impact.contains("38 kHz"),
            "Assessment fabricated decoding or omitted detector-specific limitations");
        cfg.rate=2048000;rows=assessReceiverModulations(caps,cfg,lc,true,narrow,snapshots);
        require(row(rows,"lora").compatible && row(rows,"fsk").compatible,"Fractional acquisition clocks are still rejected");
        snapshots.psk.qpsk=false;narrow.fsk=false;
        rows=assessReceiverModulations(caps,cfg,lc,false,narrow,snapshots);
        require(!row(rows,"lora").enabled && !row(rows,"fsk").enabled && !row(rows,"qpsk").enabled && row(rows,"bpsk").enabled,
            "Assessment conflated per-detector enable states");
        cfg.frequency=100800000;cfg.rate=2000000;
        require(row(assessReceiverModulations(caps,cfg,lc,false,narrow,snapshots),"fm").covered,"FM within its supported band was rejected");
        cfg.frequency=110400000;lc.search_width=1000000;
        require(row(assessReceiverModulations(caps,cfg,lc,false,narrow,snapshots),"fm").covered,"FM search overlap was mistaken for center frequency");
        cfg.rate=10000000;
        rows=assessReceiverModulations(caps,cfg,lc,true,narrow,snapshots);
        for(const auto& entry:rows)require(!entry.compatible,"Unsupported monitoring rate was shown as compatible");
        cfg.rate=2000000;lc.search_width=5000000;
        rows=assessReceiverModulations(caps,cfg,lc,true,narrow,snapshots);
        for(const auto& entry:rows)require(!entry.compatible,"Excessive search width was shown as compatible");
        std::cout<<"Per-detector enablement, timing, RF coverage and parsing limits passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
