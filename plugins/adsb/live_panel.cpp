#include "live_panel.hpp"
#include "live_input.hpp"
#include "core/input_policy.hpp"
#include "map_view.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QTimer>
#include <QPointer>
#include <QMetaObject>
#include <QSignalBlocker>
namespace signalscope::desktop {
namespace {
class Panel final:public QWidget {
public:
    Panel(QString language,QWidget* parent):QWidget(parent),language_(std::move(language)),catalog_(TextCatalog::resource("adsb")) {
        setObjectName("adsb-live-panel");setWindowTitle(text("panel.title"));
        auto* layout=new QVBoxLayout(this);auto* buttons=new QHBoxLayout;
        auto* start=new QPushButton(text("start"));auto* stopButton=new QPushButton(text("stop"));auto* fit=new QPushButton(text("fit"));
        buttons->addWidget(start);buttons->addWidget(stopButton);buttons->addStretch();buttons->addWidget(fit);layout->addLayout(buttons);
        status_=new QLabel(text("idle"));status_->setWordWrap(true);layout->addWidget(status_);
        metrics_=new QLabel;metrics_->setWordWrap(true);layout->addWidget(metrics_);
        auto* split=new QSplitter(Qt::Vertical);map_=new adsb::MapView(nullptr,language_);map_->setCaption(text(map_->hasGeography()?"map.caption":map_->hasBackground()?"map.context_unavailable":"map.unavailable"));split->addWidget(map_);
        table_=new QTableWidget(0,6);table_->setHorizontalHeaderLabels({text("address"),text("callsign"),text("altitude"),text("speed"),text("position"),text("area")});
        table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);table_->horizontalHeader()->setStretchLastSection(true);table_->setEditTriggers(QAbstractItemView::NoEditTriggers);table_->setSelectionBehavior(QAbstractItemView::SelectRows);split->addWidget(table_);split->setStretchFactor(0,3);split->setStretchFactor(1,1);layout->addWidget(split,1);
        connect(start,&QPushButton::clicked,this,[this]{listen();});connect(stopButton,&QPushButton::clicked,this,[this]{stop();});connect(fit,&QPushButton::clicked,map_,&adsb::MapView::fitAircraft);
        connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{const auto rows=table_->selectionModel()->selectedRows();if(rows.size()==1){const auto row=rows.front().row();if(table_->item(row,0)){selected_=table_->item(row,0)->data(Qt::UserRole).toULongLong();map_->selectAircraft(*selected_);}}});
        map_->selected=[this](std::uint64_t key){selected_=key;for(int row=0;row<table_->rowCount();++row)if(table_->item(row,0)->data(Qt::UserRole).toULongLong()==key){table_->selectRow(row);break;}};
        timer_.setInterval(500);connect(&timer_,&QTimer::timeout,this,[this]{refresh();});timer_.start();
    }
    ~Panel() override {++generation_;lease_.reset();}
    LiveResearchTool tool(){LiveResearchTool result;result.widget=this;result.monitorBound=false;
        result.attachReception=[this](ReceptionAcquire acquire){acquire_=std::move(acquire);};result.stop=[this]{stop();};result.busy=[this]{return bool(lease_);};
        result.summary=[this]{return lease_?status_->text():QString();};
        result.snapshot=[this]{const auto s=input_->snapshot();return Json{{"processed_pairs",s.processed_pairs},{"rejected_pairs",s.rejected_pairs},{"discontinuities",s.discontinuities},{"queued_bytes",s.queued_bytes},{"queue_capacity_bytes",adsb::LiveInput::capacity_bytes},{"error",s.error}};};return result;}
private:
    QString text(const std::string& key,const Json& arguments=Json::object()) const {Json value={{"label_key",key},{"arguments",arguments}};catalog_.resolve(value);return localizedText(value.at("label"),language_,QString::fromStdString(key));}
    void stop(){++generation_;lease_.reset();input_=std::make_shared<adsb::LiveInput>();status_->setText(text("idle"));metrics_->clear();map_->setAircraft({});table_->setRowCount(0);}
    void listen(){stop();if(!acquire_){status_->setText(text("unavailable"));return;}auto input=input_;
        const auto generation=generation_;const QPointer<Panel> guard(this);
        try{lease_=acquire_({adsb::frequency_hz,adsb::passband_hz,adsb::minimum_rate,adsb::maximum_rate,{}},{[guard,generation](const ReceptionStatus& status){if(guard)QMetaObject::invokeMethod(guard,[guard,generation,state=status.state]{if(guard && guard->generation_==generation)guard->status_->setText(guard->text("reception."+state));},Qt::QueuedConnection);},[input](const IqBlockView& block){input->offer(block);}});}catch(const std::exception&){status_->setText(text("unavailable"));}}
    void refresh(){if(!lease_)return;const auto snapshot=input_->snapshot();map_->setAircraft(snapshot.aircraft);const QSignalBlocker block(table_);table_->setRowCount(int(snapshot.aircraft.size()));
        metrics_->setText(text("metrics",{{"targets",snapshot.aircraft.size()},{"processed",snapshot.processed_pairs},{"rejected",snapshot.rejected_pairs},{"gaps",snapshot.discontinuities},{"queue",snapshot.queued_bytes},{"capacity",adsb::LiveInput::capacity_bytes}}));
        for(std::size_t row=0;row<snapshot.aircraft.size();++row){const auto& a=snapshot.aircraft[row];QStringList fields{QString::number(a.latest.address,16).toUpper(),QString::fromStdString(a.callsign),a.altitude_ft?QString::number(*a.altitude_ft):text("unknown"),a.ground_speed_kt?QString::number(*a.ground_speed_kt,'f',0):text("unknown"),a.position?QString::number(a.position->latitude,'f',4)+", "+QString::number(a.position->longitude,'f',4):text("unknown"),a.position?map_->geographicContext(*a.position,false):text("unknown")};for(int column=0;column<fields.size();++column)table_->setItem(int(row),column,new QTableWidgetItem(fields[column]));}
        for(std::size_t row=0;row<snapshot.aircraft.size();++row){const auto key=adsb::aircraft_key(snapshot.aircraft[row].latest);table_->item(int(row),0)->setData(Qt::UserRole,QVariant::fromValue<qulonglong>(key));if(selected_==key)table_->selectRow(int(row));}
        if(!snapshot.error.empty())status_->setText(text("processing_error"));}
    QString language_;TextCatalog catalog_;QLabel* status_;QLabel* metrics_;adsb::MapView* map_;QTableWidget* table_;QTimer timer_;
    ReceptionAcquire acquire_;std::shared_ptr<ReceptionLease> lease_;std::shared_ptr<adsb::LiveInput> input_=std::make_shared<adsb::LiveInput>();
    std::uint64_t generation_=0;
    std::optional<std::uint64_t> selected_;
};
}
LiveResearchProvider adsbLiveProvider(){return {"adsb-live",[](const QString& language,QWidget* parent){return (new Panel(language,parent))->tool();}};}
}
