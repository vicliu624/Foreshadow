#include "plugins/desktop/research/waveform/trace_view.hpp"
#include "plugins/desktop/research/waveform/plane_view.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QApplication>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QFileInfo>
#include <iostream>
#include <numbers>

using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::shared_ptr<WaveformAnalysis> fixture() {
    auto source=std::make_shared<SignalWindow>();source->stream={2'000'000,433'000'000,SampleFormat::cf32_le};
    SignalSegment s;s.first_sample=(1ULL<<54)+123;
    for(std::size_t n=0;n<100000;++n) {const auto v=std::polar(.5,2*std::numbers::pi*.021*n);s.samples.emplace_back(v.real(),v.imag());}
    source->segments.push_back(std::move(s));return std::make_shared<WaveformAnalysis>(analyze_waveform(source));
}
void sharedAxesAndIndependentTrace() {
    const auto analysis=fixture();
    auto independent=std::make_shared<AnalysisTrace>();independent->id="independent.future.measurement";
    independent->label_key="independent.label";independent->unit="Hz";
    for(std::size_t n=0;n<100000;++n)independent->values.push_back(1200+std::sin(n*.001)*250);
    TraceView first,second;first.setAccessibleName("Independent measurement");second.setAccessibleName("Frequency");
    first.resize(800,180);second.resize(800,180);
    first.setAnalysis(analysis,{{independent,QColor(17,137,119),"Independent"}});
    second.setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("frequency")),QColor(139,92,190),"Frequency"}});
    first.rangeChanged=[&](auto range){second.setRange(range);};second.rangeChanged=[&](auto range){first.setRange(range);};
    first.show();second.show();QCoreApplication::processEvents();
    const auto origin=analysis->source->first_sample();first.setRange({origin+12000,64});
    require(second.range()==first.range(),"Independent tracks do not share their integer sample interval");
    std::uint64_t selected=0;bool a=false;
    first.cursorChanged=[&](auto value,bool cursorA){selected=value;a=cursorA;second.setCursors({},value);};
    first.setCursors({},origin+12010);
    QKeyEvent move(QEvent::KeyPress,Qt::Key_Right,Qt::NoModifier);QApplication::sendEvent(&first,&move);
    require(selected==origin+12011 && !a,"A keyboard cursor rounded the large source position");
    QKeyEvent mark(QEvent::KeyPress,Qt::Key_Right,Qt::ShiftModifier);QApplication::sendEvent(&first,&mark);
    require(selected==origin+12001 && a,"Keyboard A/B semantics were not retained");
    const auto before=first.range();first.resize(220,160);QCoreApplication::processEvents();
    require(first.range()==before,"Resizing reset the selected interval");
    require(!first.grab().toImage().isNull(),"An independent trace could not be rendered");
    first.resetRange();const auto image=first.grab().toImage();
    require(!image.isNull() && first.renderedPrimitives()<=static_cast<std::size_t>(first.width())*3,"A long trace exceeded the pixel rendering budget");
    first.zoom(.0001,origin+1000);require(first.range().count<=10,"Sample-scale zoom is unavailable");
}
void independentPackedTrace() {
    const auto analysis=fixture();const auto first=analysis->first_sample()+12000;
    auto mapping=std::make_shared<std::vector<AnalysisTraceSpan>>(std::initializer_list<AnalysisTraceSpan>{{first,0,16,false},{first+32,16,16,true}});
    auto x=std::make_shared<AnalysisTrace>(),y=std::make_shared<AnalysisTrace>();x->id="independent.packed.x";y->id="independent.packed.y";x->mapping=y->mapping=mapping;
    for(unsigned n=0;n<32;++n){x->values.push_back(std::cos(n*.1));y->values.push_back(std::sin(n*.1));}
    TraceView view;view.resize(700,200);view.setAnalysis(analysis,{{x,QColor(180,30,120),"Independent packed"}});view.setRange({first,48});view.show();QCoreApplication::processEvents();
    require(!view.grab().isNull() && view.renderedPrimitives()>0,"An independent compact trace could not use its parent clock");
    view.setRange({first+16,16});view.grab();require(view.renderedPrimitives()==0,"An omitted packed interval was drawn as measured data");
    TracePlaneView plane;plane.resize(340,300);plane.setAnalysis(analysis,x,y);plane.setRange({first,48});plane.setCursors(first+2,first+20);plane.show();QCoreApplication::processEvents();plane.grab();
    require(plane.renderedPrimitives()==64,"Paired compact traces used whole-preview indices or a missing cursor value");
    auto invalid=*x;invalid.values.push_back(0);bool refused=false;try{validate_analysis_trace(*analysis,invalid);}catch(const std::invalid_argument&){refused=true;}require(refused,"A compact mapping omitted an extra value");
    auto shifted=std::make_shared<AnalysisTrace>(*y);auto shiftedMapping=std::make_shared<std::vector<AnalysisTraceSpan>>(*mapping);(*shiftedMapping)[0].first_sample++;shifted->mapping=shiftedMapping;refused=false;
    try{plane.setAnalysis(analysis,x,shifted);}catch(const std::invalid_argument&){refused=true;}require(refused,"Paired traces with different compact axes were combined");
    auto source=std::make_shared<SignalWindow>();source->stream={1000000,433000000,SampleFormat::cf32_le};source->segments={{0,std::vector<std::complex<float>>(10,{.5f,0}),false},{10,std::vector<std::complex<float>>(10,{.5f,0}),true}};
    const auto interrupted=analyze_waveform(source);invalid.values.assign(20,1);invalid.mapping=std::make_shared<std::vector<AnalysisTraceSpan>>(std::initializer_list<AnalysisTraceSpan>{{0,0,20,false}});refused=false;
    try{validate_analysis_trace(interrupted,invalid);}catch(const std::invalid_argument&){refused=true;}require(refused,"A compact mapping erased an unknown source discontinuity");
}
void missingIntervalsAreEmpty() {
    auto source=std::make_shared<SignalWindow>();source->stream={1'000'000,433'000'000,SampleFormat::cf32_le};
    source->segments={{0,std::vector<std::complex<float>>(100,{.5,0}),false},{1000,std::vector<std::complex<float>>(100,{.5,0}),true}};
    auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));
    TraceView view;view.resize(700,160);const QColor signal(220,25,160);
    view.setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("amplitude")),signal,"Amplitude"}});
    view.show();QCoreApplication::processEvents();const auto image=view.grab().toImage();
    std::size_t pixels=0;for(int y=15;y<image.height()-25;++y)for(int x=240;x<450;++x)if(image.pixelColor(x,y)==signal)++pixels;
    require(pixels==0,"The view drew an invented waveform across a missing source interval");
}
void independentPlane() {
    const auto analysis=fixture();auto x=std::make_shared<AnalysisTrace>(),y=std::make_shared<AnalysisTrace>();
    x->id="independent.axis.x";y->id="independent.axis.y";
    for(std::size_t n=0;n<100000;++n){x->values.push_back(std::cos(n*.001));y->values.push_back(std::sin(n*.001));}
    TracePlaneView view;view.resize(340,300);view.setAnalysis(analysis,x,y);view.show();QCoreApplication::processEvents();
    require(!view.grab().isNull() && view.renderedPrimitives()<=256*256,"An independent paired trace exceeded the density pixel budget");
    const auto first=analysis->source->first_sample();view.setRange({first+33,64});view.setCursors(first+34,first+50);
    view.grab();require(view.renderedPrimitives()==128,"The paired trace did not preserve the selected real samples");
    auto source=std::make_shared<SignalWindow>();source->stream={1000000,433000000,SampleFormat::cf32_le};
    source->segments={{0,std::vector<std::complex<float>>(10,{-.5,0}),false},{20,std::vector<std::complex<float>>(10,{.5,0}),true}};
    auto gap=std::make_shared<WaveformAnalysis>(analyze_waveform(source));
    view.setAnalysis(gap,std::shared_ptr<const AnalysisTrace>(gap,&gap->trace("i")),std::shared_ptr<const AnalysisTrace>(gap,&gap->trace("q")));view.setCursors({},{});
    const auto image=view.grab().toImage();const auto color=view.palette().highlight().color();
    std::size_t invented=0;for(int yy=110;yy<160;++yy)for(int xx=170;xx<200;++xx)if(image.pixelColor(xx,yy)==color)++invented;
    require(!invented,"The paired trace invented a trajectory across a missing interval");
}
void sampleMarkersDoNotFillOtherSeries() {
    const auto source=fixture();auto first=std::make_shared<AnalysisTrace>(),second=std::make_shared<AnalysisTrace>();first->values.assign(100000,-1);second->values.assign(100000,0);
    for(std::size_t n=0;n<64;++n)second->values[n]=1-std::abs(2.*n/63.-1);
    const QColor firstColor(204,25,40),secondColor(15,90,180);TraceView view;view.resize(700,260);
    view.setAnalysis(source,{{first,firstColor,"Independent first"},{second,secondColor,"Independent second"}});view.setRange({source->source->first_sample(),64});view.show();QCoreApplication::processEvents();
    const auto image=view.grab().toImage();std::size_t fill=0;
    for(int y=90;y<110;++y)for(int x=300;x<400;++x)if(image.pixelColor(x,y)==firstColor)++fill;
    require(fill==0,"A later series inherited a marker brush and filled an invented waveform area");
}
void sparseUint64Clock() {
    auto source=std::make_shared<SignalWindow>();source->stream={1000000,433920000,SampleFormat::cf32_le};
    source->segments={{1,std::vector<std::complex<float>>(8,{.3f,0}),false},{UINT64_MAX-8,std::vector<std::complex<float>>(8,{.4f,0}),true}};
    auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(source));TraceView view;view.resize(700,200);
    view.setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("amplitude")),QColor(10,140,120),"Sparse"}});view.setCursors(1,UINT64_MAX-1);view.show();QCoreApplication::processEvents();
    require(!view.grab().isNull(),"A sparse source clock could not be rendered");view.zoom(2);require(view.range().first==1 && view.range().count==UINT64_MAX-1,"Zoom overflowed a large source-clock span");
    std::uint64_t selected=0;view.cursorChanged=[&](auto sample,bool){selected=sample;};
    QMouseEvent click(QEvent::MouseButtonPress,QPointF(699,90),QPointF(699,90),Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);QApplication::sendEvent(&view,&click);
    require(selected==UINT64_MAX-1,"The last source sample was rounded outside the uint64 clock");view.setRange({UINT64_MAX-8,8});require(!view.grab().isNull(),"An offscreen cursor created an unbounded painter coordinate");
}
void rationalProcessedAxis() {
    const auto raw=fixture();auto processed=std::make_shared<ProcessedSignal>(extract_signal_channel(raw->source,{42000,120000,60000,400001}));
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(processed));TraceView view;view.resize(700,210);
    view.setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("amplitude")),QColor(20,130,110),"Independent processed value"}});
    require(view.range().first==analysis->first_sample() && view.range().count==analysis->end_sample()-analysis->first_sample(),"Generic trace consumers treated rational output indices as raw positions");
    view.setRange({analysis->first_sample()+20,32});view.setCursors(analysis->first_sample()+21,analysis->first_sample()+30);view.show();QCoreApplication::processEvents();require(!view.grab().isNull(),"Processed trace could not render without flattening its clock");
    TracePlaneView plane;plane.setAnalysis(analysis,std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("i")),std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("q")));plane.setRange(view.range());plane.show();QCoreApplication::processEvents();plane.grab();require(plane.renderedPrimitives()==64,"Paired trace consumer used the raw observation count for processed values");
}
void recordedProcessedAxis() {
    QTemporaryDir directory;require(directory.isValid(),"Could not create recorded-view source");const auto raw=fixture();
    auto samples=select_signal_window(*raw->source,raw->first_sample(),10000);samples.source_id="independent.recorded-view";
    const auto metadata=write_signal_archive(QFileInfo(directory.path()).filesystemFilePath()/"capture",samples);
    auto info=std::make_shared<SignalSourceInfo>(inspect_signal_source(metadata));auto processed=std::make_shared<ProcessedSignal>(extract_signal_source_channel(info,{0,10000},{42000,120000,60000,400001}));
    const auto analysis=std::make_shared<WaveformAnalysis>(analyze_waveform(processed));require(!analysis->source,"A file-backed view invented a loaded raw source");
    std::filesystem::rename(info->storage.data_path,info->storage.data_path.parent_path()/"unavailable.cf32");
    TraceView view;view.resize(700,210);view.setAnalysis(analysis,{{std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("i")),QColor(20,130,110),"Independent cached file result"}});view.show();QCoreApplication::processEvents();require(!view.grab().isNull(),"Cached file-backed traces attempted to reload missing original observations");
    TracePlaneView plane;plane.setAnalysis(analysis,std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("i")),std::shared_ptr<const AnalysisTrace>(analysis,&analysis->trace("q")));plane.setRange({analysis->first_sample(),32});plane.show();QCoreApplication::processEvents();plane.grab();require(plane.renderedPrimitives()==64,"File reference was not supported by the generic paired trace protocol");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    try {sharedAxesAndIndependentTrace();independentPackedTrace();missingIntervalsAreEmpty();independentPlane();sampleMarkersDoNotFillOtherSeries();sparseUint64Clock();rationalProcessedAxis();recordedProcessedAxis();std::cout<<"Independent generic and paired traces, shared integer axes, A/B keyboard access, resize persistence, missing intervals and pixel budget passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
