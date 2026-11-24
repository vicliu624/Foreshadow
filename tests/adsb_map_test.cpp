#include "plugins/adsb/map_view.hpp"
#include <QApplication>
#include <QImage>
#include <QTimer>
#include <iostream>
int main(int argc,char** argv) {
    QApplication app(argc,argv);
    signalscope::adsb::MapView view;
    signalscope::adsb::Aircraft aircraft;aircraft.latest.address=0x40621d;
    aircraft.callsign="TEST406";aircraft.position=signalscope::adsb::Position{52.257202,3.919373};aircraft.track_deg=65;
    aircraft.trail={{52.1,3.6},{52.18,3.75},*aircraft.position};
    view.setAircraft({aircraft});view.setCaption("Natural Earth · Offline overview");view.resize(640,400);view.fitAircraft();view.show();
    QTimer::singleShot(100,&app,[&]{const auto image=view.grab().toImage();if(image.isNull())app.exit(1);else {if(argc>1 && !image.save(QString::fromLocal8Bit(argv[1])))app.exit(2);else {std::cout<<"ADS-B map rendered\n";app.exit(0);}}});
    return app.exec();
}
