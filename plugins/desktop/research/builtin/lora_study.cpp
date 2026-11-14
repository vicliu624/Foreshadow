#include "plugins/desktop/research/builtin/lora_study.hpp"
#include "plugins/desktop/research/research_provider.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QSettings>
#include "plugins/desktop/research/research_canvas.hpp"
#include "signalscope/adapters/modulation/wideband_lora.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"
#include <QCoreApplication>
#include <QFile>
#include "signalscope/desktop/number_format.hpp"
#include <kiss_fft.h>
#include <bit>
#include <numbers>

namespace signalscope::desktop {
namespace {
using Sample=std::complex<float>;
ResearchSampleRange searchWindow(const ReplayInfo& info) {
    const auto& global=info.metadata.at("global");
    const auto stored=global.value("signalscope:result",Json::object());
    const auto origin=stored.value("stream_first_sample",std::uint64_t{});
    for(const auto& event:stored.value("events",Json::array())) {
        if(event.value("evidence_level","")!="lora_structure_match")continue;
        const auto sfd=event.value("sfd_start_sample",std::uint64_t{});
        const auto bw=event.value("bandwidth_hz",0.);
        const auto sf=event.value("sf_candidate",0);
        if(sfd<origin || sfd-origin>=info.pairs || !std::isfinite(bw) || bw<=0 || sf<5 || sf>12)continue;
        const double period=std::ldexp(double(info.rate),sf)/bw;
        if(period<16 || period>262144)continue;
        const auto n=static_cast<std::uint64_t>(std::llround(period));
        const auto relative=sfd-origin;
        const auto first=relative>12*n?relative-12*n:0;
        return {first,std::min(info.pairs-first,relative-first+3*n)};
    }
    if(info.pairs>64ULL*1024*1024/bytes_per_pair(info.format))
        throw std::invalid_argument("Trim recordings without LoRa event metadata to 64 MiB or less");
    return {0,info.pairs};
}

Sample decode(const std::uint8_t* bytes,SampleFormat format) {
    return read_iq_pair(bytes,format);
}

std::vector<float> dechirp(std::span<const Sample> symbol,double rate,double bandwidth,bool inverted) {
    const auto n=symbol.size();
    const auto length=std::bit_ceil(n);
    std::unique_ptr<kiss_fft_state,decltype(&kiss_fft_free)> fft(kiss_fft_alloc(static_cast<int>(length),0,nullptr,nullptr),kiss_fft_free);
    if(!fft)throw std::bad_alloc();
    std::vector<kiss_fft_cpx> in(length),out(length);
    for(std::size_t k=0;k<n;++k) {
        const auto t=double(k)/n;
        const auto phase=std::numbers::pi*bandwidth*n/rate*(t-t*t);
        const auto value=(inverted?std::conj(symbol[k]):symbol[k])*std::polar(1.f,static_cast<float>(phase));
        in[k]={value.real(),value.imag()};
    }
    kiss_fft(fft.get(),in.data(),out.data());
    std::vector<float> power(length);
    for(std::size_t k=0;k<length;++k) {
        const auto raw=(k+length/2)%length;
        const auto& bin=out[inverted?(length-raw)%length:raw];
        power[k]=10*std::log10(std::max(1e-16,(double(bin.r)*bin.r+double(bin.i)*bin.i)/(double(n)*n)));
    }
    return power;
}
}

ResearchControls loRaStudyControls(QWidget* parent) {
    auto* widget=new QWidget(parent);auto* form=new QFormLayout(widget);form->setContentsMargins(0,0,0,0);
    auto* bandwidth=new QComboBox;bandwidth->setObjectName("lora-study-bandwidth");
    bandwidth->addItem(QCoreApplication::translate("LoRaStudy","Automatic bandwidth"),0);
    for(int value:{125000,250000,500000})bandwidth->addItem(QString::number(value/1000)+" kHz",value);
    auto* sf=new QComboBox;sf->setObjectName("lora-study-spreading-factor");sf->addItem(QCoreApplication::translate("LoRaStudy","Automatic spreading factor"),0);
    for(int value=7;value<=12;++value)sf->addItem("SF"+QString::number(value),value);
    form->addRow(QCoreApplication::translate("LoRaStudy","LoRa bandwidth"),bandwidth);
    form->addRow(QCoreApplication::translate("LoRaStudy","Spreading factor"),sf);
    QSettings settings;
    bandwidth->setCurrentIndex(std::max(0,bandwidth->findData(settings.value("research/lora-structure/bandwidth",0).toInt())));
    sf->setCurrentIndex(std::max(0,sf->findData(settings.value("research/lora-structure/sf",0).toInt())));
    return {widget,[bandwidth,sf]{
        const auto bw=bandwidth->currentData().toInt(),spreading=sf->currentData().toInt();
        QSettings settings;settings.setValue("research/lora-structure/bandwidth",bw);settings.setValue("research/lora-structure/sf",spreading);
        return Json{{"bandwidth_hz",bw},{"spreading_factor",spreading}};
    },{}};
}
QImage renderLoRaStudy(const QString& metadata,double threshold,std::stop_token stop,
                       std::optional<ResearchSampleRange> range,const Json& parameters) {
    if(stop.stop_requested())return {};
    const auto info=inspect_recording(QFile(metadata).filesystemFileName());
    const auto stride=bytes_per_pair(info.format);
    if(range && (range->first>=info.pairs || !range->count || range->count>info.pairs-range->first ||
                 range->count>64ULL*1024*1024/stride))
        throw std::invalid_argument("LoRa research range must fit the recording and contain at most 64 MiB");
    const auto search=range?*range:searchWindow(info);
    LoRaConfig cfg;
    cfg.rate=info.rate;cfg.receiver_frequency=info.frequency;cfg.bandwidth_candidates=defaultLiveLoRaBandwidths();
    const auto selectedBandwidth=parameters.value("bandwidth_hz",0);
    const auto selectedSf=parameters.value("spreading_factor",0);
    if(selectedBandwidth && std::find(cfg.bandwidth_candidates.begin(),cfg.bandwidth_candidates.end(),selectedBandwidth)==cfg.bandwidth_candidates.end())
        throw std::invalid_argument("Unsupported LoRa study bandwidth");
    if(selectedSf && (selectedSf<7 || selectedSf>12))throw std::invalid_argument("Unsupported LoRa study spreading factor");
    if(selectedBandwidth)cfg.bandwidth_candidates={double(selectedBandwidth)};
    if(selectedSf)cfg.sf_min=cfg.sf_max=selectedSf;
    cfg.peak_to_mean_db=threshold;cfg.workers=1;cfg.search_width=info.rate;
    WidebandLoRa detector(cfg,info.format);
    Json found;
    replay(info,search.first,search.count,0,65536,[&](auto bytes,auto first){
        detector.feed(bytes,first-search.first);
        for(const auto& event:detector.take_events())
            if(event.value("evidence_level","")=="lora_structure_match"){found=event;break;}
    },[&]{return stop.stop_requested() || !found.is_null();});
    if(found.is_null() && !stop.stop_requested()) {
        detector.finish();for(const auto& event:detector.take_events())
            if(event.value("evidence_level","")=="lora_structure_match"){found=event;break;}
    }
    if(stop.stop_requested())return {};
    if(found.is_null())throw std::runtime_error("No confirmed LoRa preamble structure was found");
    const auto bandwidth=found.at("bandwidth_hz").get<double>();
    const auto sf=found.at("sf_candidate").get<int>();
    const auto n=static_cast<std::size_t>(std::llround(std::ldexp(double(info.rate),sf)/bandwidth));
    const auto sfd=found.at("sfd_start_sample").get<std::uint64_t>()+search.first;
    const auto lowerBound=range?range->first:0;
    const auto upperBound=range?range->first+range->count:info.pairs;
    if(n<16 || sfd<lowerBound || sfd-lowerBound<3*n || sfd>=upperBound)
        throw std::runtime_error("LoRa event has no complete preamble symbol in the analysis range");
    const auto first=std::max<std::uint64_t>(lowerBound,sfd>12*n?sfd-12*n:0);
    const auto count=std::min<std::uint64_t>(upperBound-first,sfd-first+8*n);
    Bytes raw;
    raw.reserve(count*stride);
    replay(info,first,count,0,65536,[&](auto bytes,auto){raw.insert(raw.end(),bytes.begin(),bytes.end());},
           [&]{return stop.stop_requested();});
    if(stop.stop_requested())return {};
    const auto localSfd=sfd-first;
    const auto symbolFirst=localSfd-3*n;
    std::vector<Sample> symbol(n);
    for(std::size_t k=0;k<n;++k)symbol[k]=decode(raw.data()+(symbolFirst+k)*stride,info.format);
    const auto spectrum=dechirp(symbol,info.rate,bandwidth,found.value("iq_inverted",false));
    const auto center=found.at("center_frequency_hz").get<double>();
    constexpr int bins=1024;
    const auto frameSize=std::clamp<std::size_t>(std::bit_floor(n/4),16,bins);
    const auto frames=count/frameSize;
    const auto columns=std::min<std::uint64_t>(frames,560);
    const auto lowBin=std::clamp(int(std::floor(bins*(.5+(center-info.frequency-bandwidth)/info.rate))),0,bins-2);
    const auto highBin=std::clamp(int(std::ceil(bins*(.5+(center-info.frequency+bandwidth)/info.rate))),lowBin+1,bins-1);
    QImage waterfall(static_cast<int>(columns),highBin-lowBin+1,QImage::Format_RGB32);
    std::vector<double> sums(highBin-lowBin+1);
    unsigned preambleFrames=0;
    const auto enbw=1.5*info.rate/(frameSize-1);
    Spectrometer meter(info.format,1024);
    for(std::uint64_t col=0;col<columns;++col) {
        if(stop.stop_requested())return {};
        const auto start=col*frames/columns*frameSize;
        const auto frame=meter.measure(std::span(raw).subspan(start*stride,frameSize*stride),first+start,info.rate,info.frequency);
        const bool preamble=start>=symbolFirst && start+frameSize<=localSfd-n;
        preambleFrames+=preamble;
        for(int bin=lowBin;bin<=highBin;++bin) {
            const auto db=frame.bins_dbfs[bin]-10*std::log10(enbw);
            if(preamble)sums[bin-lowBin]+=std::pow(10.,db/10);
            const auto level=std::clamp((db+135)/90,0.,1.);
            waterfall.setPixelColor(static_cast<int>(col),highBin-bin,
                QColor::fromHsvF(static_cast<float>(.66*(1-level)),.85f,static_cast<float>(.15+.85*level)));
        }
    }
    std::vector<float> psd;
    for(const auto value:sums)psd.push_back(10*std::log10(std::max(1e-18,value/std::max(1u,preambleFrames))));
    const auto lower=info.frequency+(double(lowBin)/bins-.5)*info.rate;
    const auto upper=info.frequency+(double(highBin)/bins-.5)*info.rate;
    const auto binHz=double(info.rate)/bins;
    auto background=sums;
    auto median=background.begin()+background.size()/2;
    std::nth_element(background.begin(),median,background.end());
    std::vector<double> excess(sums.size());
    double total=0;
    for(std::size_t k=0;k<sums.size();++k) {
        if(std::abs(lower+k*binHz-center)>bandwidth*.6)continue;
        excess[k]=std::max(0.,sums[k]-*median);
        total+=excess[k];
    }
    std::size_t lowPower=0,highPower=0;
    if(total>0) {
        double cumulative=0;
        bool lowFound=false;
        for(std::size_t k=0;k<excess.size();++k) {
            cumulative+=excess[k];
            if(!lowFound && cumulative>=total*.01){lowPower=k;lowFound=true;}
            if(cumulative>=total*.99){highPower=k;break;}
        }
    }
    // Compare consecutive preamble symbols at the candidate period.
    Sample correlation{};
    double firstEnergy=0,secondEnergy=0;
    for(std::size_t k=0;k<n;++k) {
        const auto next=decode(raw.data()+(symbolFirst+n+k)*stride,info.format);
        correlation+=std::conj(symbol[k])*next;
        firstEnergy+=std::norm(symbol[k]);secondEnergy+=std::norm(next);
    }
    const auto coherence=firstEnergy>0 && secondEnergy>0
        ?std::min(1.,std::abs(correlation)/std::sqrt(firstEnergy*secondEnergy)):0.;
    const auto locale=measurementLocale();
    const auto number=[&locale](double value,int precision) {return locale.toString(value,'f',precision);};
    const auto band=QString("%1–%2 MHz").arg(number(lower/1e6,4),number(upper/1e6,4));
    const auto fullPeak=std::max_element(spectrum.begin(),spectrum.end())-spectrum.begin();
    const auto peakHz=info.frequency+(double(fullPeak)-double(spectrum.size()/2))*info.rate/spectrum.size();
    QImage image(1420,710,QImage::Format_RGB32);
    image.fill(QColor("#101d30"));
    QPainter painter(&image);
    painter.setPen(QColor("#e2edf7"));
    painter.setFont(QFont(painter.font().family(),16));
    const auto title=range?QCoreApplication::translate("LoRaStudy","LoRa preamble and SFD · samples %1–%2")
                              .arg(qulonglong(range->first)).arg(qulonglong(upperBound-1))
                          :QCoreApplication::translate("LoRaStudy","LoRa preamble and SFD");
    painter.drawText(QRect(20,15,1380,45),Qt::AlignCenter,title);
    painter.setFont(QFont(painter.font().family(),10));
    const auto stft=panel(painter,0,QCoreApplication::translate("LoRaStudy","Spectrogram and SFD marker"),
        QString("%1–%2 ms").arg(number(1000.*first/info.rate,3),number(1000.*(first+count)/info.rate,3)),
        QString("%1\n↑\n%2 MHz").arg(number(upper/1e6,3),number(lower/1e6,3)));
    painter.drawImage(stft,waterfall);
    const auto marker=stft.left()+stft.width()*localSfd/count;
    painter.setPen(QColor("#5bdcc6"));painter.drawLine(QPointF(marker,stft.top()),QPointF(marker,stft.bottom()));
    const auto matchedTop=std::ceil(spectrum[fullPeak]/10.)*10;
    const auto matched=panel(painter,1,QCoreApplication::translate("LoRaStudy","Dechirp FFT of one preamble symbol"),
        QString("%1–%2 MHz").arg(number((info.frequency-info.rate/2.)/1e6,4),number((info.frequency+info.rate/2.)/1e6,4)),
        QString("%1–%2\ndBFS").arg(number(matchedTop-60,0),number(matchedTop,0)));
    trace(painter,matched,spectrum,matchedTop-60,matchedTop,QColor("#5bdcc6"));
    const auto floor=std::ceil(*std::max_element(psd.begin(),psd.end())/10.)*10;
    const auto powerPlot=panel(painter,2,QCoreApplication::translate("LoRaStudy","Mean preamble power spectral density"),
                              band,QString("%1–%2\ndBFS/Hz").arg(number(floor-70,0),number(floor,0)));
    if(total>0 && highPower>=lowPower) {
        const auto scale=powerPlot.width()/std::max<std::size_t>(1,sums.size()-1);
        painter.fillRect(QRectF(powerPlot.left()+lowPower*scale,powerPlot.top(),
                                 (highPower-lowPower)*scale,powerPlot.height()),QColor(40,130,115,65));
    }
    trace(painter,powerPlot,psd,floor-70,floor,QColor("#f1b761"));
    const auto details=panel(painter,3,QCoreApplication::translate("LoRaStudy","Confirmed structure and estimates"),{}, {});
    const auto text=QCoreApplication::translate("LoRaStudy",
        "Estimated center: %1 MHz\nBandwidth: %2 kHz    SF%3\nSymbol duration: %4 ms\nSFD starts at sample %5\nDechirp peak: %6 MHz\nPower is uncalibrated. Payload is not decoded.")
        .arg(number(center/1e6,6),locale.toString(bandwidth/1000),locale.toString(sf),number(1000.*n/info.rate,3),
             QString::number(static_cast<qulonglong>(sfd)),number(peakHz/1e6,6));
    auto report=text+"\n"+QCoreApplication::translate("LoRaStudy","Correlation at candidate symbol period: %1")
        .arg(number(coherence,3));
    if(total>0 && highPower>=lowPower)
        report+="\n"+QCoreApplication::translate("LoRaStudy","Local 98% excess-power width: %1 kHz")
            .arg(number((highPower-lowPower)*binHz/1000,1));
    painter.setPen(QColor("#e2edf7"));painter.drawText(details.adjusted(12,12,-12,-12),Qt::AlignTop|Qt::TextWordWrap,report);
    return image;
}
}
