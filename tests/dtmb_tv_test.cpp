#include "plugins/desktop/research/builtin/dtmb_tv.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTableWidget>
#include <iostream>
using namespace signalscope;
using namespace signalscope::desktop;
namespace {
struct InputLease final:ReceptionLease {
    std::shared_ptr<bool> released;explicit InputLease(std::shared_ptr<bool> value):released(std::move(value)){}
    void release() noexcept override{*released=true;}~InputLease() override{release();}
};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void standalone(const LiveResearchProvider& provider,const QString& language) {
    auto tool=provider.create(language,nullptr);ReceptionDemand requested;ReceptionConsumer consumer;auto released=std::make_shared<bool>(false);unsigned requests=0;
    tool.attachReception([&](const auto& demand,auto input){requested=demand;consumer=std::move(input);++requests;return std::make_shared<InputLease>(released);});
    auto* start=tool.widget->findChild<QPushButton*>("dtmb-tv-start");auto* frequency=tool.widget->findChild<QLineEdit*>("dtmb-tv-frequency");frequency->setText("554");
    require(start->isEnabled(),"Standalone TV required wideband listening first");start->click();
    require(requests==1 && requested.passband_hz==8000000 && requested.minimum_rate==8000000 && tool.busy(),"TV did not declare its own whole-channel input requirement");
    tool.sourceChanged({false,{}});require(tool.busy() && !*released,"Stopping monitor stopped independent pending TV input");
    consumer.changed(ReceptionStatus{"conflict"});require(!tool.busy() && *released,"An input conflict left TV holding an unusable lease");
    auto actions=tool.actions({{"evidence_level","dtmb_frame_structure"},{"center_frequency_hz",554000000}});require(actions.size()==2,"DTMB event had no channel favorite action");actions.back().trigger();
    auto* presets=tool.widget->findChild<QComboBox*>("dtmb-tv-presets");require(presets->count()==1,"DTMB channel favorite was not visible");delete tool.widget;
    auto reopened=provider.create(language,nullptr);presets=reopened.widget->findChild<QComboBox*>("dtmb-tv-presets");require(presets->count()==1,"DTMB favorite did not persist");
    QSettings settings;const auto data=Json::parse(settings.value("live-research/dtmb-tv/favorites_v1").toString().toUtf8().toStdString());const auto& station=data.at("stations")[0].at("value");require(station.at("frequency_hz")==554000000 && station.at("program_id")==0 && !station.contains("audio_pid") && !station.contains("video_pid"),"Favorite identity was tied to transient media PIDs");delete reopened.widget;
}
Json await(LiveResearchTool& tool,int timeout) {
    QElapsedTimer timer;timer.start();Json state;
    while(timer.elapsed()<timeout) {
        QApplication::processEvents();QThread::msleep(5);state=tool.snapshot();
        const auto player=state.at("playback").value("state",std::string());
        if(player=="finished" || player=="error")return state;
    }
    return state;
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("SignalScopeTests");app.setApplicationName("DtmbTelevision");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    const auto language=app.arguments().size()>4?app.arguments()[4]:QString("en");app.setProperty("signalscope-language",language);QLocale::setDefault(QLocale(language));
    try {
        require(app.arguments().size()>=2,"Expected the helper executable");
        auto provider=dtmbTvResearchProvider(path_from_utf8(app.arguments()[1].toUtf8().toStdString()));standalone(provider,language);QWidget owner;auto tool=provider.create(language,&owner);
        auto* panel=tool.widget;panel->resize(app.arguments().contains("--compact")?QSize(640,480):QSize(1200,780));panel->show();QApplication::processEvents();
        auto* source=panel->findChild<QComboBox*>("dtmb-tv-source");auto* file=panel->findChild<QLineEdit*>("dtmb-tv-file");
        auto* start=panel->findChild<QPushButton*>("dtmb-tv-start");auto* frequency=panel->findChild<QLineEdit*>("dtmb-tv-frequency");
        auto* pn=panel->findChild<QComboBox*>("dtmb-tv-pn_symbols");auto* body=panel->findChild<QComboBox*>("dtmb-tv-frame_body");
        require(source && file && start && frequency && pn && body && !start->isEnabled(),"TV controls were missing or live reception could start without a source");
        tool.sourceChanged({true,IqStreamParameters{2000000,554000000,SampleFormat::ci16_le}});require(!start->isEnabled(),"A complete live TV channel was accepted at 2 MS/s");
        tool.sourceChanged({true,IqStreamParameters{10000000,554000000,SampleFormat::ci16_le}});require(start->isEnabled(),"A compatible source could not be selected");
        pn->setCurrentIndex(pn->findData("945"));body->setCurrentIndex(body->findData("\"c1\""));require(!start->isEnabled() && !tool.busy(),"Conflicting PN/C1 settings were applied");
        panel->findChild<QPushButton*>("dtmb-tv-recommend")->click();require(start->isEnabled(),"The recommended configuration did not repair the entire pending combination");
        frequency->setText("600");require(!start->isEnabled(),"A TV channel outside the input band was accepted");frequency->clear();
        QTemporaryDir data;QString path;
        if(app.arguments().size()>2)path=app.arguments()[2];
        else {
            path=data.filePath("input.ts");std::ofstream output(path_from_utf8(path.toUtf8().toStdString()),std::ios::binary);
            Bytes packets(564,0xff);for(unsigned i=0;i<3;++i)packets[i*188]=0x47;output.write(reinterpret_cast<const char*>(packets.data()),packets.size());
        }
        source->setCurrentIndex(source->findData(path.endsWith(".sigmf-meta")?"iq_file":"transport_file"));file->setText(path);
        panel->findChild<QCheckBox*>("dtmb-tv-mute")->setChecked(true);require(start->isEnabled(),"The selected recording or TS file was rejected");start->click();
        auto state=await(tool,30000);QThread::msleep(50);QApplication::processEvents();std::cout<<state.dump(2)<<'\n';
        require(state.at("playback").at("state")=="finished","The actual TV provider failed or did not finish playback");
        require(state.at("playback").at("presented_video_frames").get<std::uint64_t>()>0,"The television provider never presented video");
        auto* program=panel->findChild<QComboBox*>("dtmb-tv-program");require(program && program->count()>0,"The TV provider did not list decoded programs");
        QApplication::processEvents();const auto* tracks=panel->findChild<QTableWidget*>("dtmb-tv-tracks");std::size_t declared=0;
        for(const auto& row:state.at("playback").at("programs"))if(row.at("program_id")==program->currentData().toUInt())declared=row.value("streams",Json::array()).size();
        require(tracks && declared>0 && tracks->rowCount()==int(declared),"The provider hid a declared program track");
        if(app.arguments().contains("--compact"))require(panel->width()<=640 && panel->height()<=480,"The television controls forced the small-screen panel beyond its viewport");
        if(app.arguments().size()>3 && !app.arguments()[3].isEmpty()){QApplication::processEvents();panel->grab().save(app.arguments()[3]);}
        if(app.arguments().contains("--two-programs")) {
            require(program->count()==2 && program->findData(101)>=0 && program->findData(202)>=0,"The independently modulated synthetic programmes were not both listed");
            auto* audio=panel->findChild<QComboBox*>("dtmb-tv-audio-track");require(audio,"The actual audio-track selector was missing");
            program->setCurrentIndex(program->findData(101));
            for(const auto pid:{257,258}) {
                const auto index=audio->findData(pid);require(index>=0,"The two programme-101 audio tracks were not exposed");
                audio->setCurrentIndex(index);state=await(tool,30000);const auto& playback=state.at("playback");
                require(playback.at("state")=="finished" && playback.at("catalog").at("selected_program")==101 &&
                    playback.at("catalog").at("selected_audio_pid")==pid && playback.at("decoded_audio_frames").get<unsigned>()>0 &&
                    playback.at("presented_video_frames").get<unsigned>()>0 && playback.at("decode_errors")==0,
                    "Selecting a synthetic audio track did not actually decode that PID and present the programme");
                std::cout<<Json{{"interaction","audio-track"},{"program",101},{"audio_pid",pid},{"playback",playback}}.dump()<<'\n';
            }
        }
        if(program->count()>1) {
            const auto id=program->itemData(1).toUInt();program->setCurrentIndex(1);state=await(tool,30000);
            require(state.at("playback").at("state")=="finished" && state.at("playback").at("catalog").at("selected_program")==id &&
                    state.at("playback").at("decoded_audio_frames").get<unsigned>()>0,
                "The TV program selector changed its label without changing the actual decoded program and audio");
            std::cout<<Json{{"interaction","program"},{"program",id},{"playback",state.at("playback")}}.dump()<<'\n';
        }
        tool.stop();require(!tool.busy(),"Stopping TV left its source worker or media process active");
        std::cout<<"Television provider controls, whole-config validation, repair, actual playback and stop passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
