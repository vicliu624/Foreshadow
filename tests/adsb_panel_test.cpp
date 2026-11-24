#include "plugins/adsb/live_panel.hpp"
#include "plugins/adsb/map_view.hpp"
#include <QApplication>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <iostream>
#include <thread>
using namespace signalscope;
namespace {
struct Lease final:ReceptionLease {std::jthread worker;void release() noexcept override {worker.request_stop();}};
Bytes fixture(const std::string& hex) {
    std::vector<std::complex<float>> iq(240);for(auto slot:{0,2,7,9})iq[slot]={.7f,.2f};
    for(unsigned bit=0;bit<112;++bit){const auto byte=std::stoul(hex.substr(bit/8*2,2),nullptr,16);iq[16+bit*2+((byte>>(7-bit%8))&1?0:1)]={.7f,.2f};}
    Bytes bytes(iq.size()*bytes_per_pair(SampleFormat::cf32_le));for(std::size_t n=0;n<iq.size();++n)write_iq_pair(bytes.data()+n*bytes_per_pair(SampleFormat::cf32_le),SampleFormat::cf32_le,iq[n]);return bytes;
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);auto provider=desktop::adsbLiveProvider();const auto language=argc>2?QString::fromUtf8(argv[2]):QStringLiteral("en");auto tool=provider.create(language,nullptr);
    if((language=="zh-CN" && tool.widget->windowTitle()!=QString::fromUtf8("ADS-B 航空交通")) || (language=="fr-CA" && tool.widget->windowTitle()!="ADS-B air traffic"))throw std::runtime_error("ADS-B language fallback contract failed");
    tool.attachReception([](const ReceptionDemand& demand,ReceptionConsumer consumer)->std::shared_ptr<ReceptionLease>{
        if(demand.frequency_hz!=1090000000.)throw std::runtime_error("Incorrect ADS-B demand");
        auto lease=std::make_shared<Lease>();lease->worker=std::jthread([consumer=std::move(consumer)]{
            const IqStreamParameters stream{2000000,1090000000,SampleFormat::cf32_le};ReceptionStatus status("receiving");status.stream=stream;consumer.changed(status);
            const auto even=fixture("8D40621D58C382D690C8AC2863A7"),odd=fixture("8D40621D58C386435CC412692AD6");
            consumer.offer({even,0,stream,false});consumer.offer({odd,240,stream,false});});return lease;
    });
    tool.widget->resize(640,480);tool.widget->show();const auto buttons=tool.widget->findChildren<QPushButton*>();buttons.front()->click();
    QTimer::singleShot(1500,&app,[&]{auto* table=tool.widget->findChild<QTableWidget*>();auto* map=dynamic_cast<adsb::MapView*>(tool.widget->findChild<QWidget*>("adsb-map"));
        if(!table || table->rowCount()!=1 || !map || !map->hasBackground() || !tool.summary || tool.summary().isEmpty()){app.exit(1);return;}map->fitAircraft();
        QTimer::singleShot(50,&app,[&]{if(argc>1 && argv[1][0] && !tool.widget->grab().save(QString::fromUtf8(argv[1]))){app.exit(2);return;}tool.stop();if(tool.busy() || !tool.summary().isEmpty()){app.exit(3);return;}std::cout<<"ADS-B panel reference reception passed\n";app.exit(0);});
    });
    const auto result=app.exec();delete tool.widget;return result;
}
