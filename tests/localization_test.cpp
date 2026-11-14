#include "test_process.hpp"
#include "decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include "event_presentation.hpp"
#include "language_preference.hpp"
#include "receiver_probe.hpp"
#include "plugins/desktop/research/research_range.hpp"
#include "status_presentation.hpp"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QJsonArray>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTranslator>
#include <QXmlStreamReader>
#include <algorithm>

using namespace amc_test;

namespace {
QStringList placeholders(const QString& text) {
    static const QRegularExpression pattern(R"(%L?[0-9]+)");
    QStringList matches;
    auto it=pattern.globalMatch(text);
    while(it.hasNext())matches.push_back(it.next().captured());
    std::sort(matches.begin(),matches.end());
    return matches;
}

QString unescape(const QString& source) {
    QString result;
    for(qsizetype i=0;i<source.size();++i) {
        if(source[i]!=u'\\'){result+=source[i];continue;}
        require(++i<source.size(),"Truncated escape in translation source");
        switch(source[i].unicode()) {
        case 'n': result+=u'\n';break;
        case 'r': result+=u'\r';break;
        case 't': result+=u'\t';break;
        case '\\': result+=u'\\';break;
        case '"': result+=u'"';break;
        default: throw std::runtime_error("Unsupported escape in translation source");
        }
    }
    return result;
}

using Key=QPair<QString,QString>;
QMap<Key,QString> catalog(const QString& path,bool sourceOnly=false) {
    QXmlStreamReader xml(read(path));
    require(xml.readNextStartElement() && xml.name()==u"TS","Invalid Qt translation catalog");
    QMap<Key,QString> entries;
    while(xml.readNextStartElement()) {
        if(xml.name()!=u"context"){xml.skipCurrentElement();continue;}
        QString context;
        while(xml.readNextStartElement()) {
            if(xml.name()==u"name"){context=xml.readElementText();continue;}
            if(xml.name()!=u"message"){xml.skipCurrentElement();continue;}
            QString source,translation;
            bool unfinished=false;
            while(xml.readNextStartElement()) {
                if(xml.name()==u"source")source=xml.readElementText();
                else if(xml.name()==u"translation") {
                    unfinished=xml.attributes().value("type")==u"unfinished";
                    translation=xml.readElementText();
                } else xml.skipCurrentElement();
            }
            const Key key{context,source};
            if(context.isEmpty() || source.isEmpty() || entries.contains(key) ||
               (!sourceOnly && (translation.isEmpty() || unfinished || placeholders(source)!=placeholders(translation)))) {
                std::cerr<<context.toUtf8().constData()<<": "<<source.toUtf8().constData()<<'\n';
                throw std::runtime_error("Invalid, duplicate or incomplete translation");
            }
            entries.insert(key,translation);
        }
    }
    require(!xml.hasError() && !entries.isEmpty(),"Could not parse translation catalog");
    return entries;
}

void checkExtractedSources(const QDir& root,const QString& extractor,const QMap<Key,QString>& entries) {
    QTemporaryDir temporary;
    require(temporary.isValid(),"Could not create a temporary translation catalog");
    const auto extracted=temporary.filePath("desktop.ts");
    ChildRunner(temporary.path()).run(extractor,{"-no-obsolete","-locations","none",
        root.absoluteFilePath("src/frontend/desktop"),root.absoluteFilePath("plugins/desktop"),"-ts",extracted});
    const auto sources=catalog(extracted,true);
    for(auto entry=sources.cbegin();entry!=sources.cend();++entry) {
        if(!entries.contains(entry.key())) {
            std::cerr<<entry.key().first.toUtf8().constData()<<": "<<entry.key().second.toUtf8().constData()<<'\n';
            throw std::runtime_error("Qt extracted a source string missing from the Chinese catalog");
        }
    }
}

void checkSource(const QFileInfo& file,const QMap<Key,QString>& entries) {
    static const QRegularExpression han(R"([\x{3400}-\x{9fff}])");
    static const QRegularExpression literal(R"re("((?:[^"\\\r\n]|\\.)*)")re");
    static const QRegularExpression translate(R"re(QCoreApplication::translate\("([^"]+)",\s*"((?:[^"\\\r\n]|\\.)*)"\))re");
    const auto text=QString::fromUtf8(read(file.absoluteFilePath()));
    auto strings=literal.globalMatch(text);
    while(strings.hasNext()) {
        if(han.match(strings.next().captured(1)).hasMatch()) {
            std::cerr<<file.fileName().toUtf8().constData()<<'\n';
            throw std::runtime_error("Chinese text belongs in translation resources");
        }
    }
    auto calls=translate.globalMatch(text);
    while(calls.hasNext()) {
        const auto match=calls.next();
        const Key key{match.captured(1),unescape(match.captured(2))};
        if(!entries.contains(key)) {
            std::cerr<<file.fileName().toUtf8().constData()<<": "<<key.second.toUtf8().constData()<<'\n';
            throw std::runtime_error("Missing desktop translation");
        }
    }
}

void checkPresentation(const QMap<Key,QString>& entries) {
    using namespace signalscope::desktop;
    const signalscope::Json fm{
        {"evidence_level","broadcast_fm_stereo"},{"center_frequency_hz",100800000},
        {"first_observed_sample",9007199254740993ULL},{"pilot_contrast_db",22.0}
    };
    const auto previousLocale=QLocale();
    QLocale::setDefault(QLocale::c());
    const auto english=eventCells(fm);
    QLocale::setDefault(QLocale(QLocale::German,QLocale::Germany));
    const auto regionalCells=eventCells(fm);
    const auto regionalEvidence=eventEvidence(fm);
    QLocale::setDefault(previousLocale);
    require(english.front()=="Broadcast FM (stereo)" && english[1]=="100.800000" &&
            english.back()=="9007199254740993","FM fields or absolute sample index were lost");
    require(regionalCells[1]=="100,800000" && regionalCells[4]=="22,0" &&
            regionalCells.back()==english.back() &&
            regionalEvidence.endsWith(QString::fromStdString(fm.dump(2))),
            "Regional numeric presentation must preserve sample identifiers and raw JSON");
    const signalscope::Json css{{"evidence_level","css_candidate"},{"repeated_chirps",8}};
    require(eventCells(css).front()=="CSS candidate","CSS candidate was mislabeled");
    require(eventCells({{"evidence_level","fsk_candidate"}})[2]==QStringLiteral("\u2014"),
            "Legacy event with missing measurements could not be displayed");
    const auto meta=[](signalscope::Json status){
        return signalscope::Json{{"global",{{"signalscope:status",std::move(status)}}}};
    };
    require(recordingStatus({{"global",signalscope::Json::object()}}).label=="External recording",
            "Third-party SigMF without a status must be identified as external");
    require(recordingStatus(meta(42)).label=="Unknown recording status",
            "Malformed optional status must not prevent listing a recording");
    const QLocale timeLocale(QLocale::German,QLocale::Germany);
    const auto utcZone=QTimeZone::utc();
    const auto expectedLocal=timeLocale.toString(QDateTime::fromString("2026-11-06T20:30:00Z",Qt::ISODate),
                                                 QLocale::ShortFormat);
    const auto captureTime=recordingTimeTooltip(
        {{"captures",signalscope::Json::array({{{"core:datetime","2026-11-06T22:30:00+02:00"}}})}},timeLocale,utcZone);
    require(captureTime.startsWith("Capture metadata time: ") && captureTime.contains(expectedLocal) &&
            captureTime.endsWith("2026-11-06T22:30:00+02:00"),
            "Recording time lost the original offset or was not converted to the requested zone");
    const auto saved=recordingTimeTooltip(
        {{"global",{{"signalscope:saved_utc","2026-11-06T20:30:00Z"}}}},timeLocale,utcZone);
    require(saved.startsWith("File saved at: ") &&
            recordingTimeTooltip({{"captures",signalscope::Json::array({{{"core:datetime","2026-11-06T20:30:00"}}})}},
                                 timeLocale,utcZone).isEmpty(),
            "Saved time was mislabeled or a timezone-free timestamp was interpreted as local time");
    require(sessionStatus("future_state")=="Unknown session status","Unknown session code leaked into the UI");

    class CatalogTranslator final : public QTranslator {
    public:
        explicit CatalogTranslator(const QMap<Key,QString>& entries):entries_(entries){}
        bool isEmpty() const override{return false;}
        QString translate(const char* context,const char* source,const char*,int) const override{
            return entries_.value({QString::fromUtf8(context),QString::fromUtf8(source)});
        }
    private:
        const QMap<Key,QString>& entries_;
    } translator(entries);
    require(QCoreApplication::installTranslator(&translator),"Could not install test catalog");
    const auto localized=eventEvidence(fm);
    const auto future=recordingStatus(meta("future_status"));
    const auto interrupted=recordingStatus(meta("monitor_interrupted"));
    const auto failure=sessionStatus("source_error");
    const auto narrow=narrowbandStatus({{"fsk_analysis",{{"scanned_blocks",214},{"skipped_blocks",6},
        {"queue_capacity_blocks",8},{"queue_high_water_blocks",7},{"unavailable",false}}}});
    const auto byteBudget=narrowbandStatus({{"fsk_analysis",{{"scanned_blocks",214},{"skipped_blocks",6},
        {"queue_capacity_bytes",16777216},{"queue_high_water_bytes",8388608},{"queue_capacity_seconds",.4194304},
        {"skipped_pairs",4096},{"unavailable",false}}}});
    QLocale::setDefault(QLocale(QLocale::German,QLocale::Germany));
    const auto reads=receiverReadSummary({{"source_result",{{"read_stats",{{"reads",1234567},{"short_reads",3},
        {"timeouts",1},{"min_read_pairs",3616},{"max_read_pairs",4096},{"delivered_blocks",2}}}}}});
    QLocale::setDefault(previousLocale);
    const auto missingBackend=receiverUnavailableMessage({{"reason_code","backend_not_built"}});
    const auto missingDriver=receiverUnavailableMessage({{"reason_code","driver_missing"}});
    QCoreApplication::removeTranslator(&translator);
    require(reads.contains(entries.value({"MonitorWindow","Driver reads %L1 · Short reads %L2 · Timeouts %L3"})
                        .replace("%L1","1.234.567").replace("%L2","3").replace("%L3","1")) &&
            reads.contains(entries.value({"MonitorWindow","Read sizes %L1–%L2 I/Q pairs · Delivered %L3 blocks"})
                        .replace("%L1","3.616").replace("%L2","4.096").replace("%L3","2")) &&
            receiverReadSummary(signalscope::Json::object()).isEmpty(),
            "Receiver diagnostics must be localized and optional");
    require(narrow.degraded &&
            narrow.suffix==entries.value({"MonitorWindow"," · Narrowband blocks skipped %1"}).arg(6) &&
            narrow.detail.contains(entries.value({"MonitorWindow","Narrowband analysis scanned %1 blocks and skipped %2."}).arg(214).arg(6)) &&
            narrow.detail.contains(entries.value({"MonitorWindow","Narrowband queue peaked at %1 of %2 blocks."}).arg(7).arg(8)) &&
            !narrowbandStatus(signalscope::Json::object()).degraded,
            "Narrowband load must be distinct from main analysis loss and localized");
    require(byteBudget.degraded && byteBudget.detail.contains(entries.value({"MonitorWindow",
        "Analysis queue peak %1 / %2 MiB (%3 s). Software analysis discarded %4 I/Q pairs; this is separate from driver loss."})
        .arg(QLocale().toString(8.,'f',2)).arg(QLocale().toString(16.,'f',2)).arg(QLocale().toString(.4194304,'f',3)).arg(4096)),
        "Payload capacity, duration and software loss must be localized independently of USB block size");
    require(missingBackend==entries.value({"ReceiverDialog","This build does not include the selected receiver backend."}) &&
            missingDriver==entries.value({"ReceiverDialog","The receiver driver is missing or could not load. Check the vendor driver and SoapySDR module."}) &&
            !missingBackend.isEmpty() && missingBackend!=missingDriver &&
            receiverUnavailableMessage({{"reason_code","future_reason"}}).isEmpty(),
            "Receiver setup states lost their distinct localizations or fallback");
    require(localized.contains(entries.value({"MonitorWindow","Broadcast FM (stereo)"})) &&
            localized.contains(entries.value({"MonitorWindow","Raw event data (JSON)"})) &&
            localized.endsWith(QString::fromStdString(fm.dump(2))),
            "Localized evidence must preserve the raw event JSON");
    require(future.label==entries.value({"MonitorWindow","Unknown recording status"}) && future.code=="future_status" &&
            interrupted.label==entries.value({"MonitorWindow","Recording interrupted"}) &&
            failure==entries.value({"MonitorWindow","Input error"}),
            "Localized statuses must preserve diagnostic codes and distinguish failures");
}

void checkReceiverProbe() {
    using signalscope::desktop::probeGainPresets;
    require(!probeGainPresets({}),"A backend without probed gains must allow fallback");
    const auto empty=probeGainPresets({{"gain_tenths_db",QJsonArray{}}});
    require(empty && empty->empty(),"An empty probed list must keep auto as the only option");
    const auto gains=probeGainPresets({{"gain_tenths_db",QJsonArray{386,197,386,-10,1.5,1020,"280"}}});
    require(gains && *gains==std::vector<int>{-10,197,386},
            "Driver-reported gain options must be integral, bounded and unique");
    using signalscope::desktop::receiverMonitorRates;
    const std::uint32_t rates[]{2'000'000,2'048'000,3'000'000};
    require(receiverMonitorRates({},rates)==std::vector<std::uint32_t>{2'000'000,2'048'000,3'000'000},
            "A receiver without rate reporting must keep its backend presets");
    require(receiverMonitorRates({{"sample_rates_hz",QJsonArray{2048000,2000000,10000000,2048000,1750000}}},rates)
                ==std::vector<std::uint32_t>{1'750'000,2'000'000,2'048'000,10'000'000} &&
            receiverMonitorRates({{"sample_rates_hz",QJsonArray{1750000,"2000000"}}},rates)==std::vector<std::uint32_t>{1'750'000},
            "Receiver settings must preserve all numeric driver rates independently of recommended presets");
}
void checkResearchRange() {
    using signalscope::desktop::researchRangeFromSeconds;
    using signalscope::desktop::replayEventRange;
    const auto range=researchRangeFromSeconds("0.125","0.250",1750000);
    require(range && range->first==218750 && range->count==437500,"Time range ignored the recording sample rate");
    const QLocale german(QLocale::German,QLocale::Germany);
    require(signalscope::desktop::displayDecimal(1234.5,1,german)=="1234,5",
            "Measurement formatting introduced a grouping separator");
    using signalscope::desktop::canonicalDecimalInput;
    using signalscope::desktop::decimalInputPattern;
    const QRegularExpression seconds(QRegularExpression::anchoredPattern(decimalInputPattern(german,20,9).pattern()));
    const auto localized=researchRangeFromSeconds(canonicalDecimalInput("0,125",german),
                                                  canonicalDecimalInput("0.250",german),1750000);
    require(seconds.match("0,125").hasMatch() && seconds.match("0.250").hasMatch() &&
            !seconds.match("1.234,56").hasMatch() && !seconds.match("0,1234567890").hasMatch() &&
            localized && localized->first==218750 && localized->count==437500,
            "Localized decimal input changed the exact sample range or accepted mixed separators");
    const QRegularExpression frequency(QRegularExpression::anchoredPattern(decimalInputPattern(german,7,6).pattern()));
    require(frequency.match("100,800000").hasMatch() &&
            canonicalDecimalInput("100,800000",german).toDouble()*1e6==100800000 &&
            !frequency.match("1e2").hasMatch() && !frequency.match("100,8000001").hasMatch(),
            "FM input changed the tuned frequency or accepted unsupported notation");
    const QLocale arabic(QLocale::Arabic,QLocale::Egypt);
    for(const auto& language:{german,arabic})for(const auto value:{-2.718281e-12,3.141592e-8,6.283185e12}) {
        // Some supported Qt versions cannot parse their own localized scientific
        // notation. Check the rendered magnitude through an independent C-locale
        // parse, using locale metadata for signs, exponent, decimal and digits.
        auto rendered=signalscope::desktop::displayMeasurement(value,language);
        rendered.replace(language.negativeSign(),QStringLiteral("-"));
        rendered.replace(language.positiveSign(),QStringLiteral("+"));
        rendered.replace(language.exponential(),QStringLiteral("e"));
        bool valid=false;const auto read=canonicalDecimalInput(rendered,language).toDouble(&valid);
        require(valid && read!=0 && std::abs((read-value)/value)<1e-6,"A localized measurement lost its nonzero magnitude or sign");
    }
    const QRegularExpression arabicFrequency(QRegularExpression::anchoredPattern(decimalInputPattern(arabic,7,6).pattern()));
    const auto regional=signalscope::desktop::displayDecimal(100.8,6,arabic);
    require(regional==QStringLiteral("\u0661\u0660\u0660\u066b\u0668\u0660\u0660\u0660\u0660\u0660") &&
            arabicFrequency.match(regional).hasMatch() && arabicFrequency.match("100.800000").hasMatch() &&
            canonicalDecimalInput(regional,arabic)=="100.800000" &&
            !arabicFrequency.match(QStringLiteral("\u0661\u066c\u0660\u0660\u0660")).hasMatch(),
            "Arabic decimal input did not round-trip or accepted a grouping separator");
    const auto rounding=researchRangeFromSeconds("0.000000250","0.000000250",2000000);
    require(rounding && rounding->first==1 && rounding->count==1,"Time range half-sample rounding changed");
    const auto large=researchRangeFromSeconds("9007199254740993","1",1);
    require(large && large->first==9007199254740993ULL,"Time range lost precision at a large index");
    require(!researchRangeFromSeconds("18446744073709551615","1",2) &&
            !researchRangeFromSeconds("0","0.000000001",2000000) &&
            !researchRangeFromSeconds("-1","1",2000000) &&
            !researchRangeFromSeconds("1e3","1",2000000),"Time range accepted overflow or invalid values");
    const auto lora=replayEventRange({{"first_observed_sample",40000},{"confirmed_at_sample",46000}},1750000,120000);
    require(lora && lora->first==5000 && lora->count==76000,"Replay LoRa range lost the confirmation sample");
    const auto css=replayEventRange({{"first_observed_sample",5},{"last_observed_sample",90000}},1750000,100000);
    require(css && css->first==0 && css->count==100000,"Replay event range did not respect recording boundaries");
    require(!replayEventRange({{"first_observed_sample",-1}},1750000,100000) &&
            !replayEventRange({{"first_observed_sample",100000}},1750000,100000),
            "Replay event range accepted an invalid source index");
}
void checkLanguageSelection() {
    using signalscope::desktop::resolveLanguageSelection;
    const auto firstLaunch=resolveLanguageSelection({}, {}, {"zh-Hans-SG","en-US"});
    require(firstLaunch && firstLaunch->mode=="system" && firstLaunch->tag=="zh-CN",
            "First launch did not honor the system language");
    const auto savedEnglish=resolveLanguageSelection({},"en",{"zh-Hans-CN"});
    require(savedEnglish && savedEnglish->mode=="en" && savedEnglish->tag=="en",
            "An explicit saved preference did not survive the default change");
    const auto chinese=resolveLanguageSelection({},"system",{"fr-FR","zh-Hans-CN","en-US"});
    require(chinese && chinese->mode=="system" && chinese->tag=="zh-CN","System language preference ignored Simplified Chinese");
    const auto english=resolveLanguageSelection("system","zh-CN",{"en-GB","zh-CN"});
    require(english && english->tag=="en","System language preference ignored list order");
    const auto unsupported=resolveLanguageSelection({},"system",{"zh-Hant-TW","ja-JP"});
    require(unsupported && unsupported->tag=="en","Unsupported locale did not use English fallback");
    const auto explicitEnglish=resolveLanguageSelection("en","system",{"zh-CN"});
    require(explicitEnglish && explicitEnglish->tag=="en","Explicit language did not override the saved mode");
    const auto badSetting=resolveLanguageSelection({},"bad",{"zh-CN"});
    require(badSetting && badSetting->mode=="system" && badSetting->tag=="zh-CN" &&
            !resolveLanguageSelection("bad","en",{}),
            "Invalid settings and explicit language requests were treated the same");
}
}

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(app.arguments().size()!=2 && app.arguments().size()!=3)return 2;
    try {
        const QDir root(app.arguments()[1]);
        const auto entries=catalog(root.filePath("translations/ams_zh_CN.ts"));
        if(app.arguments().size()==3)checkExtractedSources(root,app.arguments()[2],entries);
        checkPresentation(entries);
        checkReceiverProbe();
        checkResearchRange();
        checkLanguageSelection();
        for(const auto& path:{QStringLiteral("src"),QStringLiteral("include/signalscope"),
                              QStringLiteral("plugins/desktop")}) {
            QDirIterator files(root.filePath(path),{"*.cpp","*.hpp"},QDir::Files,QDirIterator::Subdirectories);
            require(files.hasNext(),"Localization source directory is empty");
            while(files.hasNext()){files.next();checkSource(files.fileInfo(),entries);}
        }
        std::cout<<"Native localization contract passed.\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 1;
    }
}
