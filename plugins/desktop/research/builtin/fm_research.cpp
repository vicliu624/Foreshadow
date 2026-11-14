#include "plugins/desktop/research/builtin/fm_research.hpp"
#include "signalscope/adapters/modulation/fm_audio.hpp"
#include <QCoreApplication>
#include <QFile>
#include "signalscope/desktop/number_format.hpp"
#include <QTemporaryDir>
#include <algorithm>

namespace signalscope::desktop {
ResearchAudio renderFmResearch(const QString& metadata,std::stop_token stop,
                               std::optional<ResearchSampleRange> range,FmResearchOptions options) {
    if(stop.stop_requested())return {};
    const auto info=inspect_recording(QFile(metadata).filesystemFileName());
    const auto limit=std::uint64_t(info.rate)*30;
    const auto selected=range.value_or(ResearchSampleRange{0,std::min(info.pairs,limit)});
    if(selected.count>limit)throw std::invalid_argument("FM research can analyze at most 30 seconds at a time");
    QTemporaryDir temporary;
    if(!temporary.isValid())throw std::runtime_error("Could not create a temporary directory for FM audio");
    const auto output=temporary.filePath("output");
    const auto frequency=options.frequency.value_or(info.frequency);
    exportFmAudio(info,selected.first,selected.count,frequency,options.deemphasisUs,options.deviationHz,
                  QFile(output).filesystemFileName(),[&]{return stop.stop_requested();});
    if(stop.stop_requested())return {};
    QFile wav(output+"/audio.wav");
    if(!wav.open(QIODevice::ReadOnly))throw std::runtime_error("Could not read the demodulated audio");
    auto result=readResearchAudio(wav.readAll());
    const auto locale=measurementLocale();
    result.description=QCoreApplication::translate("FmResearch", "FM center %1 MHz · De-emphasis %2 µs · Deviation %3 kHz")
        .arg(locale.toString(frequency/1e6,'f',6),locale.toString(options.deemphasisUs),
             locale.toString(options.deviationHz/1000));
    return result;
}
}
