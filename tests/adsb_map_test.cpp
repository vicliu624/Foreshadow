#include "plugins/adsb/map_view.hpp"
#include <QApplication>
#include <QImage>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    signalscope::adsb::MapView view;
    signalscope::adsb::Aircraft aircraft;aircraft.latest.address=0x40621d;
    aircraft.callsign="MAP TEST";aircraft.position=signalscope::adsb::Position{24.9874,102.8465};aircraft.track_deg=65;
    aircraft.trail={{24.7,102.6},{24.85,102.75},*aircraft.position};
    view.setAircraft({aircraft});view.setCaption("Natural Earth · Offline overview");view.resize(640,400);view.fitAircraft();view.show();
    QTimer::singleShot(100,&app,[&]{const auto image=view.grab().toImage();if(image.isNull())app.exit(1);else {if(argc>1 && !image.save(app.arguments().at(1)))app.exit(2);else {std::cout<<"ADS-B map rendered\n";app.exit(0);}}});
    return app.exec();
}
