#include "plugins/desktop/research/waveform/research_views.hpp"
#include <QApplication>
#include <QMouseEvent>
#include <iostream>
#include <limits>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F function){bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"Invalid research view contract was accepted");}
void independentSpectrum() {
    auto source=std::make_shared<SignalWindow>();source->stream={48000,433920000,SampleFormat::cf32_le};constexpr auto origin=(1ULL<<54)+11;
    source->segments={{origin,std::vector<std::complex<float>>(32,{.5f,0}),false},{origin+224,std::vector<std::complex<float>>(32,{.5f,0}),true}};
    auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));auto result=std::make_shared<WindowResearch>();result->analysis=analysis;result->range={origin,256};result->options.fft_size=16;result->options.hop_samples=16;
    result->method={{"id","independent.future-spectral-method"},{"unknown",17}};result->frame_first_samples={origin,origin+16,origin+224,origin+240};result->spectral_power.assign(64,.001f);
    ResearchSpectrogramView view;view.resize(740,360);view.setResult(result,"independent power unit");view.setColorRange(-100,-10);view.show();QApplication::processEvents();const auto image=view.grab().toImage();
    require(!image.isNull() && view.cachedPixels()<=512*1024,"A generic spectrogram exceeded its cache budget or required a known method ID");
    const auto background=view.palette().base().color();std::size_t invented=0,observed=0;
    for(int y=110;y<140;++y)for(int x=350;x<430;++x)if(image.pixelColor(x,y)!=background)++invented;
    for(int y=80;y<130;++y)for(int x=105;x<125;++x)if(image.pixelColor(x,y)!=background)++observed;
    require(invented<40 && observed>800,"The spectrogram filled a source gap or failed to display observed bins");
    std::optional<ResearchBandSelection> selected;view.timeFrequencySelected=[&](const auto& selection){selected=selection;};
    QMouseEvent press(QEvent::MouseButtonPress,QPointF(150,80),QPointF(150,80),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,QPointF(600,260),QPointF(600,260),Qt::LeftButton,Qt::NoButton,Qt::NoModifier);QApplication::sendEvent(&view,&release);
    require(selected && selected->range.first>=origin && selected->range.first+selected->range.count<=origin+256 && selected->low_offset_hz<0 && selected->high_offset_hz>0,"Time/frequency selection lost its exact source clock or signed frequency band");
    view.resize(3000,2000);view.grab();require(view.cachedPixels()<=512*1024,"Large desktop dimensions caused an unbounded raster allocation");
    auto malformed=std::make_shared<WindowResearch>(*result);malformed->spectral_power.pop_back();rejects([&]{view.setResult(malformed,{});});
    malformed=std::make_shared<WindowResearch>(*result);malformed->frame_first_samples[1]=origin;rejects([&]{view.setResult(malformed,{});});
    rejects([&]{view.setColorRange(0,-1);});
}
void genericCurves() {
    ResearchCurveView view;view.resize(620,280);std::vector<QPointF> values;for(std::size_t i=0;i<65536;++i)values.emplace_back(double(i),i==32767?1000:1);
    view.setSeries(values,"independent X unit","independent Y unit");view.show();QApplication::processEvents();require(!view.grab().isNull() && view.renderedPrimitives()<=4*620,"Curve aggregation exceeded the display pixel budget");
    values[100].setY(std::numeric_limits<double>::quiet_NaN());view.setSeries(values,"lag","pair mean",true);view.grab();require(view.renderedPrimitives()<=620,"Histogram rendering scaled with all source bins");
    rejects([&]{view.setSeries({{2,1},{1,0}},"x","y");});
}
}
int main(int argc,char** argv){QApplication app(argc,argv);try{independentSpectrum();genericCurves();std::cout<<"Independent generic spectra/curves, signed band selection, exact clock, missing support and raster budgets passed\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
