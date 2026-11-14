#include "plugins/desktop/display/signal_display.hpp"
#include <QApplication>
#include <QImage>
#include <iostream>
#include <limits>
using namespace signalscope;
int main(int argc,char** argv) {
    QApplication app(argc,argv);auto displays=desktop::createSignalDisplays();
    try {
        auto* spectrum=displays.front();spectrum->resize(640,320);spectrum->show();
        SpectrumFrame frame;frame.rate=10000000;frame.search_width_hz=frame.rate;frame.frequency=100000000;frame.sequence=1;
        frame.bins_dbfs.assign(65536,-90);constexpr std::size_t spike=41001;frame.bins_dbfs[spike]=-25;
        frame.bins_dbfs[2000]=std::numeric_limits<float>::quiet_NaN();spectrum->setFrame(frame);
        const auto image=spectrum->grab().toImage();const auto color=QColor("#5bdcc6").rgb();
        const auto x=55+int(double(spike)*(640-75)/(frame.bins_dbfs.size()-1));bool peak=false,floor=false;
        for(int column=x-3;column<=x+3;++column)for(int row=24;row<275;++row)if(image.pixel(column,row)==color){peak|=row<50;floor|=row>235;}
        if(!peak || !floor)throw std::runtime_error("Dense spectrum lost a single-bin peak or its pixel's noise floor");
        for(std::size_t i=0;i<frame.bins_dbfs.size();++i)frame.bins_dbfs[i]=i%2?-100:-20;
        ++frame.sequence;spectrum->resize(200,320);spectrum->setFrame(frame);
        if(spectrum->grab().isNull())throw std::runtime_error("Dense spectrum did not render in a narrow dock");
        for(auto* display:displays)delete display;
        std::cout<<"Single-bin peaks, per-pixel noise floor, nonfinite bins and dense narrow-dock rendering passed.\n";return 0;
    }catch(const std::exception& error){for(auto* display:displays)delete display;std::cerr<<error.what()<<'\n';return 1;}
}
