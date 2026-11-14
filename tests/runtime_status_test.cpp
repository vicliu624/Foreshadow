#include "runtime_status.hpp"
#include "desktop_theme.hpp"
#include <QApplication>
#include <QMainWindow>
#include <QStatusBar>
#include <iostream>
using namespace signalscope;using namespace signalscope::desktop;
void require(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){QApplication app(argc,argv);applyDesktopTheme(app);try{
    QMainWindow window;auto* status=new RuntimeStatus("fr-CA",&window);window.statusBar()->addPermanentWidget(status,1);require(status->text("healthy")=="No current warning","Missing status translations did not fall back to the source resource");
    status->tasks->setText(status->text("no_tasks"));status->health->setText(status->text("healthy"));status->device->setText(status->text("connected",{{"device","Independent receiver"}}));status->load->setText(status->text("load",{{"cpu",12},{"cores",1.5},{"memory",120}}));
    for(const auto width:{1200,640,1600}){window.resize(width,480);window.show();QApplication::processEvents();
        Json geometry=Json::array();for(auto* button:{status->device,status->tasks,status->load,status->health}){const auto point=button->mapTo(&window,QPoint());geometry.push_back({{"name",button->objectName().toStdString()},{"visible",button->isVisible()},{"text",button->text().toStdString()},{"x",point.x()},{"width",button->width()},{"y",point.y()},{"height",button->height()}});
        }
        std::cout<<Json{{"width",width},{"status_width",status->width()},{"items",geometry}}.dump()<<'\n';
        if(argc>1 && width==1200)window.grab().save(QString::fromUtf8(argv[1]));
        for(auto* button:{status->tasks,status->health})require(button->isVisible() && window.rect().contains(QRect(button->mapTo(&window,QPoint()),button->size())),"A required bottom status control left the window");
        require(status->load->isVisible()==(width>=1200) && status->device->isVisible()==(width>=1200),"Responsive status summaries did not follow the available width");
    }
    std::cout<<"Persistent responsive runtime summaries passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
