#include "plugins/desktop/research/builtin/iq_overview.hpp"
#include "plugins/desktop/research/research_canvas.hpp"
#include "signalscope/backend/dsp/spectrum.hpp"

#include <QCoreApplication>
#include <QFile>
#include "signalscope/desktop/number_format.hpp"
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace signalscope::desktop {
QImage renderIqOverview(const QString& metadata,std::stop_token stop,std::optional<ResearchSampleRange> range) {
    const auto info=inspect_recording(QFile(metadata).filesystemFileName());
    constexpr std::size_t fft_size=1024;
    constexpr std::uint64_t limit=2'000'000;
    const auto firstSample=range?range->first:0;
    const auto count=range?range->count:std::min(info.pairs,limit);
    if(firstSample>info.pairs || count>info.pairs-firstSample || count>limit)
        throw std::invalid_argument("I/Q overview range must fit the recording and contain at most 2,000,000 sample pairs");
    if(count<16)throw std::invalid_argument("I/Q overview requires at least 16 sample pairs");
    const auto stride=bytes_per_pair(info.format);
    const auto step=std::max<std::uint64_t>(1,(count+3999)/4000);
    const auto points=(count+step-1)/step;
    std::vector<float> i(points),q(points),envelope(points),deviation(points,std::numeric_limits<float>::quiet_NaN());
    std::vector<float> phaseAmplitude(points);
    const auto frames=(count+fft_size-1)/fft_size;
    const auto columns=std::min<std::uint64_t>(frames,500);
    QImage waterfall(static_cast<int>(columns),fft_size,QImage::Format_RGB32);
    waterfall.fill(QColor("#081322"));
    std::vector<double> peak(columns*fft_size),power(fft_size);
    std::uint64_t used_frames=0;
    std::complex<float> previous{};
    Spectrometer spectrum(info.format);
    replay(info,firstSample,count,0,fft_size*stride,[&](auto block,auto first){
        const auto offset=first-firstSample;
        const auto pairs=block.size()/stride;
        for(std::size_t n=0;n<pairs;++n) {
            const auto* sample=block.data()+n*stride;
            const auto index=offset+n;
            const auto bucket=index/step;
            const auto value=read_iq_pair(sample,info.format);
            const auto magnitude=std::abs(value);
            envelope[bucket]=std::max(envelope[bucket],magnitude);
            if(index%step==0) {
                i[bucket]=value.real();q[bucket]=value.imag();
                phaseAmplitude[bucket]=std::min(magnitude,std::abs(previous));
                if(index && phaseAmplitude[bucket]>1e-8f)
                    deviation[bucket]=std::arg(value*std::conj(previous))*info.rate/(2*std::numbers::pi*1000);
            }
            previous=value;
        }
        if(pairs<16)return;
        const auto frame=spectrum.measure(block,first,info.rate,info.frequency);
        const auto column=(offset/fft_size)*columns/frames;
        // The spectrometer uses a symmetric Hann window with ENBW = 1.5 Fs/(N-1).
        const auto enbw=1.5*info.rate/(pairs-1);
        for(std::size_t bin=0;bin<fft_size;++bin) {
            const auto density=std::pow(10.,frame.bins_dbfs[bin]/10.)/enbw;
            power[bin]+=density;
            auto& cell=peak[column*fft_size+bin];
            cell=std::max(cell,density);
        }
        ++used_frames;
    },[&]{return stop.stop_requested();});
    if(stop.stop_requested())return {};
    const auto maxAmplitude=std::max(1e-6f,*std::max_element(envelope.begin(),envelope.end()));
    for(std::size_t n=0;n<deviation.size();++n)
        if(phaseAmplitude[n]<maxAmplitude*.05f)deviation[n]=std::numeric_limits<float>::quiet_NaN();
    const auto highest=10*std::log10(std::max(*std::max_element(peak.begin(),peak.end()),1e-18));
    for(std::uint64_t col=0;col<columns;++col)for(std::size_t bin=0;bin<fft_size;++bin) {
        const auto db=10*std::log10(std::max(peak[col*fft_size+bin],1e-18));
        const auto level=std::clamp((db-highest+65)/65,0.,1.);
        waterfall.setPixelColor(static_cast<int>(col),static_cast<int>(fft_size-1-bin),
                               QColor::fromHsvF(static_cast<float>(.66*(1-level)),.85f,
                                                static_cast<float>(.15+.85*level)));
    }
    std::vector<float> density;
    for(const auto value:power)density.push_back(10*std::log10(std::max(value/used_frames,1e-18)));
    QImage image(1420,1010,QImage::Format_RGB32);
    image.fill(QColor("#101d30"));
    QPainter painter(&image);
    painter.setPen(QColor("#e2edf7"));
    painter.setFont(QFont(painter.font().family(),16));
    painter.drawText(QRect(20,15,1380,45),Qt::AlignCenter,
        QCoreApplication::translate("IqOverview","I/Q overview · samples %1–%2").arg(qulonglong(firstSample)).arg(qulonglong(firstSample+count-1)));
    painter.setFont(QFont(painter.font().family(),10));
    const auto locale=measurementLocale();
    const auto time=QCoreApplication::translate("IqOverview","Time from range start: 0–%1 ms")
        .arg(locale.toString(1000.*count/info.rate,'f',3));
    const auto lowFrequency=locale.toString((info.frequency-info.rate/2.)/1e6,'f',4);
    const auto highFrequency=locale.toString((info.frequency+info.rate/2.)/1e6,'f',4);
    const auto band=QCoreApplication::translate("IqOverview","Frequency: %1–%2 MHz").arg(lowFrequency,highFrequency);
    const auto maxOffset=info.rate/2000.;
    const auto amplitude=locale.toString(maxAmplitude,'g',3);
    const auto amplitudeLabel=QString("±%1").arg(amplitude);
    const auto wave=panel(painter,0,QCoreApplication::translate("IqOverview","I/Q waveform (subsampled)"),time,amplitudeLabel);
    trace(painter,wave,i,-maxAmplitude,maxAmplitude,QColor("#5bdcc6"));
    trace(painter,wave,q,-maxAmplitude,maxAmplitude,QColor("#f1b761"));
    trace(painter,panel(painter,1,QCoreApplication::translate("IqOverview","Peak amplitude envelope"),time,
          QString("0–%1").arg(amplitude)),envelope,0,maxAmplitude,QColor("#5bdcc6"));
    trace(painter,panel(painter,2,QCoreApplication::translate("IqOverview","Instantaneous frequency offset (low amplitude masked)"),time,
          QString("±%1 kHz").arg(locale.toString(maxOffset,'f',0))),deviation,-maxOffset,maxOffset,QColor("#f1b761"));
    const auto plane=panel(painter,3,QCoreApplication::translate("IqOverview","Raw I/Q plane (before synchronization)"),
                           "I "+amplitudeLabel,"Q "+amplitudeLabel);
    painter.setPen(QColor("#5bdcc6"));
    const auto radius=std::min(plane.width(),plane.height())/(2*maxAmplitude);
    for(std::size_t n=0;n<i.size();++n)
        painter.drawPoint(plane.center()+QPointF(i[n]*radius,-q[n]*radius));
    const auto spectrogram=panel(painter,4,QCoreApplication::translate("IqOverview","Spectrogram (peak-held display)"),
                                 time,highFrequency+"\n  ↑\n"+lowFrequency+"\nMHz");
    painter.drawImage(spectrogram,waterfall);
    const double top=std::ceil(*std::max_element(density.begin(),density.end())/10.)*10;
    trace(painter,panel(painter,5,QCoreApplication::translate("IqOverview","Mean power spectral density"),band,
          QString("%1\n  ↑\n%2\ndBFS/Hz").arg(locale.toString(top,'f',0),locale.toString(top-80,'f',0))),
          density,top-80,top,QColor("#5bdcc6"));
    return image;
}
}
