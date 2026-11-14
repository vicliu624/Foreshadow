#include "receiver_dialog.hpp"
#include "plugins/desktop/common/parameter_editor.hpp"
#include "desktop_tools.hpp"
#include "receiver_probe.hpp"
#include "search_width_control.hpp"
#include "modulation_assessment.hpp"
#include "signalscope/adapters/receivers/receiver.hpp"

#include <QComboBox>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QScreen>
#include <QGuiApplication>
#include <QStandardItemModel>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QResizeEvent>
#include <QPlainTextEdit>

namespace signalscope::desktop {
namespace {
constexpr auto serialRole=Qt::UserRole+1;
QWidget* fieldWithProblem(QWidget* field,QLabel*& problem,QPushButton* repair=nullptr) {
    auto* container=new QWidget;container->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);
    auto* column=new QVBoxLayout(container);column->setContentsMargins(0,0,0,0);column->setSpacing(3);column->setAlignment(Qt::AlignTop);
    column->addWidget(field);problem=new QLabel;problem->setWordWrap(true);problem->setTextFormat(Qt::PlainText);
    problem->setStyleSheet("color:#e76c61");problem->hide();column->addWidget(problem);
    if(repair){column->addWidget(repair,0,Qt::AlignLeft);repair->hide();}
    return container;
}
}
ReceiverDialog::ReceiverDialog(const ReceiverSelection& current,QWidget* parent,std::function<QString(const ReceiverSelection&)> access)
    :QDialog(parent),initial_(current),access_(std::move(access)) {
    setWindowTitle(QCoreApplication::translate("ReceiverDialog","Receiver settings"));
    auto* root=new QVBoxLayout(this);
    auto* header=new QWidget;auto* headerLayout=new QVBoxLayout(header);headerLayout->setContentsMargins(0,0,0,0);
    auto* deviceRow=new QHBoxLayout;auto* stateRow=new QHBoxLayout;
    headerLayout->addLayout(deviceRow);headerLayout->addLayout(stateRow);root->addWidget(header);
    panels_=new QSplitter(Qt::Horizontal);panels_->setObjectName("receiver-settings-panels");panels_->setChildrenCollapsible(false);
    root->addWidget(panels_,1);
    auto* content=new QWidget;
    auto* layout=new QFormLayout(content);
    layout->setFormAlignment(Qt::AlignTop);
    auto* scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);
    panels_->addWidget(scroll);
    auto* parametersTitle=new QLabel(QCoreApplication::translate("ReceiverDialog","Receiver parameters"));
    parametersTitle->setStyleSheet("font-weight:600;font-size:15px");layout->addRow(parametersTitle);
    auto* modulationPage=new QWidget;auto* modulationLayout=new QVBoxLayout(modulationPage);
    modulationLayout->setContentsMargins(8,0,0,0);
    auto* modulationTitle=new QLabel(QCoreApplication::translate("ReceiverDialog","Modulation assessment"));
    modulationTitle->setStyleSheet("font-weight:600;font-size:15px");modulationLayout->addWidget(modulationTitle);
    modulationSummary_=new QLabel;modulationSummary_->setWordWrap(true);modulationSummary_->setTextFormat(Qt::PlainText);
    modulationSummary_->setObjectName("receiver-modulation-summary");modulationLayout->addWidget(modulationSummary_);
    auto* note=new QLabel(QCoreApplication::translate("ModulationAssessment","Compatibility requires a suitable signal to produce results. Parsing lists implemented capabilities, not a success probability. Select a row for detector details."));
    note->setWordWrap(true);modulationLayout->addWidget(note);
    modulationTable_=new QTableWidget(0,4);modulationTable_->setObjectName("receiver-modulation-table");
    modulationTable_->setHorizontalHeaderLabels({QCoreApplication::translate("ModulationAssessment","Modulation"),
        QCoreApplication::translate("ModulationAssessment","Detection status"),QCoreApplication::translate("ModulationAssessment","Parsing capability"),
        QCoreApplication::translate("ModulationAssessment","Impact and requirements")});
    modulationTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);modulationTable_->setWordWrap(true);
    modulationTable_->setTextElideMode(Qt::ElideNone);
    modulationTable_->verticalHeader()->hide();modulationTable_->horizontalHeader()->setStretchLastSection(true);
    auto* tableHeader=modulationTable_->horizontalHeader();tableHeader->ensurePolished();
    tableHeader->setDefaultAlignment(Qt::AlignLeft|Qt::AlignVCenter);
    const int minimumWidths[]{80,115,180};
    for(int column=0;column<3;++column)
        modulationTable_->setColumnWidth(column,std::max(minimumWidths[column],
            tableHeader->fontMetrics().horizontalAdvance(modulationTable_->horizontalHeaderItem(column)->text())+24));
    modulationTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    modulationTable_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);modulationTable_->setMinimumWidth(300);
    connect(modulationTable_->horizontalHeader(),&QHeaderView::sectionResized,this,[this]{
        QTimer::singleShot(0,modulationTable_,&QTableWidget::resizeRowsToContents);
    });
    modulationLayout->addWidget(modulationTable_,1);
    auto* detectorDetails=new QPushButton(QCoreApplication::translate("ModulationAssessment","Selected detector details"));
    detectorDetails->setCheckable(true);modulationLayout->addWidget(detectorDetails);
    modulationDetails_=new QPlainTextEdit;modulationDetails_->setReadOnly(true);modulationDetails_->setMaximumHeight(110);modulationDetails_->hide();
    modulationLayout->addWidget(modulationDetails_);
    connect(detectorDetails,&QPushButton::toggled,modulationDetails_,&QWidget::setVisible);
    connect(modulationTable_,&QTableWidget::currentCellChanged,this,[this](int row,int,int,int){
        if(row<0 || !modulationTable_->item(row,0))return;
        modulationDetails_->setPlainText(modulationTable_->item(row,0)->data(Qt::UserRole+1).toString());
    });
    panels_->addWidget(modulationPage);panels_->setStretchFactor(0,0);panels_->setStretchFactor(1,1);panels_->setSizes({400,760});
    resize(1200,650);
    if(const auto screen=QGuiApplication::primaryScreen()) {
        const auto available=screen->availableGeometry();
        setMinimumSize(std::min(760,available.width()-32),std::min(500,available.height()-40));
        resize(std::min(1200,available.width()-32),std::min(650,available.height()-40));
    }
    backend_=new QComboBox;
    for(const auto* receiver:receiver_backends())
        backend_->addItem(QString::fromLatin1(receiver->display_name),QString::fromLatin1(receiver->id));
    backend_->setCurrentIndex(backend_->findData(current.backend));
    device_=new QComboBox;
    device_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);device_->setMinimumContentsLength(10);
    antenna_=new QComboBox;
    antenna_->setObjectName("receiver-antenna");
    sampleRate_=new QComboBox;
    sampleRate_->setEditable(true);
    sampleRate_->setObjectName("receiver-sample-rate");
    frequency_=new QDoubleSpinBox;frequency_->setObjectName("receiver-frequency");
    frequency_->setRange(.000001,9007199254.740991);frequency_->setDecimals(6);frequency_->setSuffix(" MHz");
    frequency_->setValue(initial_.frequency/1e6);
    searchWidth_=new SearchWidthControl;searchWidth_->setObjectName("receiver-search-width");
    searchWidth_->setSelection(static_cast<int>(initial_.searchWidth/1000),initial_.searchWidthMode);
    gain_=new QComboBox;gain_->setObjectName("receiver-gain");
    inputMode_=new QComboBox;inputMode_->setObjectName("receiver-input-mode");
    inputMode_->addItem(QCoreApplication::translate("ReceiverDialog","Automatic input"),"auto");
    inputMode_->addItem(QCoreApplication::translate("ReceiverDialog","Tuner"),"tuner");
    inputMode_->addItem(QCoreApplication::translate("ReceiverDialog","Direct sampling I"),"direct_i");
    inputMode_->addItem(QCoreApplication::translate("ReceiverDialog","Direct sampling Q (HF)"),"direct_q");
    inputMode_->setCurrentIndex(std::max(0,inputMode_->findData(initial_.inputMode)));
    ifBandwidth_=new QComboBox;ifBandwidth_->setObjectName("receiver-if-bandwidth");
    ifBandwidth_->addItem(QCoreApplication::translate("ReceiverDialog","Automatic filter"),0.);
    inputModeLabel_=new QLabel(QCoreApplication::translate("ReceiverDialog","RF input mode"));
    ifBandwidthLabel_=new QLabel(QCoreApplication::translate("ReceiverDialog","IF filter (3 dB)"));
    gainLabel_=new QLabel;
    feedback_=new QLabel;feedback_->setObjectName("receiver-configuration-feedback");
    feedback_->setWordWrap(true);feedback_->setTextFormat(Qt::PlainText);feedback_->setMinimumWidth(300);
    feedback_->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Minimum);
    feedback_->hide();
    coverageSummary_=new QLabel;coverageSummary_->setObjectName("receiver-coverage-summary");
    coverageSummary_->setWordWrap(true);coverageSummary_->setTextFormat(Qt::PlainText);
    planStatus_=new QLabel;planStatus_->setObjectName("receiver-plan-status");planStatus_->setWordWrap(true);
    recommended_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Use recommended configuration"));
    recommended_->setObjectName("receiver-use-recommended");
    recommendedSummary_=new QLabel;recommendedSummary_->setWordWrap(true);recommendedSummary_->setTextFormat(Qt::PlainText);
    repairWidth_=new QPushButton;repairWidth_->setObjectName("receiver-repair-width");
    repairRate_=new QPushButton;repairRate_->setObjectName("receiver-repair-rate");
    repairAntenna_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Use a compatible antenna"));
    repairAntenna_->setObjectName("receiver-repair-antenna");
    autoGain_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Use automatic gain"));
    advancedRates_=new QCheckBox(QCoreApplication::translate("ReceiverDialog","Show all device rates and custom input (advanced)"));
    advancedRates_->setObjectName("receiver-advanced-rates");
    details_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Technical details"));details_->setCheckable(true);
    details_->setObjectName("receiver-technical-details");
    status_=new QLabel;
    status_->setObjectName("receiver-status");
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    refresh_=new QPushButton(QCoreApplication::translate("ReceiverDialog","Refresh devices"));
    refresh_->setObjectName("receiver-refresh");
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Cancel);
    accept_=buttons->button(QDialogButtonBox::Ok);
    accept_->setText(QCoreApplication::translate("ReceiverDialog","Apply"));
    buttons->button(QDialogButtonBox::Cancel)->setText(QCoreApplication::translate("ReceiverDialog","Cancel"));
    deviceRow->addWidget(new QLabel(QCoreApplication::translate("ReceiverDialog","Receiver backend")));deviceRow->addWidget(backend_,1);
    deviceRow->addWidget(new QLabel(QCoreApplication::translate("ReceiverDialog","Device")));deviceRow->addWidget(device_,3);
    deviceRow->addWidget(refresh_);
    stateRow->addWidget(planStatus_,1);stateRow->addWidget(recommended_);
    headerLayout->addWidget(recommendedSummary_);
    layout->addRow(QCoreApplication::translate("ReceiverDialog","Antenna port"),fieldWithProblem(antenna_,antennaProblem_,repairAntenna_));
    layout->addRow(QCoreApplication::translate("ReceiverDialog","Sample rate"),fieldWithProblem(sampleRate_,rateProblem_,repairRate_));
    layout->addRow(advancedRates_);
    layout->addRow(inputModeLabel_,fieldWithProblem(inputMode_,inputProblem_));
    layout->addRow(ifBandwidthLabel_,fieldWithProblem(ifBandwidth_,ifProblem_));
    layout->addRow(QCoreApplication::translate("MonitorWindow","Center frequency"),fieldWithProblem(frequency_,frequencyProblem_));
    layout->addRow(QCoreApplication::translate("MonitorWindow","Search width"),fieldWithProblem(searchWidth_,widthProblem_,repairWidth_));
    widthProblem_->setObjectName("receiver-width-problem");rateProblem_->setObjectName("receiver-rate-problem");
    layout->addRow(gainLabel_,fieldWithProblem(gain_,gainProblem_,autoGain_));
    pluginParameters_=new ParameterEditor(nullptr,"receiver-plugin-");layout->addRow(pluginParameters_);
    connectionParameters_=new ParameterEditor(nullptr,"receiver-connection-");layout->insertRow(0,connectionParameters_);
    connectionParameters_->changed=[this]{clearPlan();accept_->setEnabled(false);};
    populateConnection();
    pluginParameters_->changed=[this]{updatePlan();};
    layout->addRow(coverageSummary_);
    layout->addRow(status_);
    layout->addRow(details_);
    layout->addRow(feedback_);
    root->addWidget(buttons);
    connect(buttons,&QDialogButtonBox::accepted,this,&ReceiverDialog::apply);
    connect(buttons,&QDialogButtonBox::rejected,this,&QDialog::reject);
    connect(refresh_,&QPushButton::clicked,this,&ReceiverDialog::scan);
    connect(backend_,&QComboBox::currentIndexChanged,this,[this]{populateConnection();scan();});
    connect(device_,&QComboBox::currentIndexChanged,this,&ReceiverDialog::probe);
    connect(sampleRate_,&QComboBox::currentTextChanged,this,[this]{updatePlan();});
    connect(antenna_,&QComboBox::currentIndexChanged,this,[this]{updatePlan();});
    connect(gain_,&QComboBox::currentIndexChanged,this,[this]{updatePlan();});
    connect(inputMode_,&QComboBox::currentIndexChanged,this,[this]{updatePlan();});
    connect(ifBandwidth_,&QComboBox::currentIndexChanged,this,[this]{updatePlan();});
    connect(frequency_,&QDoubleSpinBox::valueChanged,this,[this]{updatePlan();});
    connect(searchWidth_,&SearchWidthControl::valueChanged,this,[this]{updatePlan();});
    connect(details_,&QPushButton::toggled,feedback_,&QLabel::setVisible);
    connect(recommended_,&QPushButton::clicked,this,[this]{useRecommended();});
    connect(repairWidth_,&QPushButton::clicked,this,[this]{searchWidth_->setSelection(searchWidth_->recommendedValue(),"maximum");});
    connect(repairRate_,&QPushButton::clicked,this,[this]{if(const auto r=recommendation()) {populateRates(r->receiver.rate);updatePlan();}});
    connect(repairAntenna_,&QPushButton::clicked,this,[this]{if(const auto r=recommendation())antenna_->setCurrentIndex(antenna_->findData(QString::fromStdString(r->receiver.antenna)));});
    connect(autoGain_,&QPushButton::clicked,this,[this]{gain_->setCurrentIndex(gain_->findData("auto"));});
    connect(advancedRates_,&QCheckBox::toggled,this,[this]{if(capabilities_){populateRates(configuration().rate);updatePlan();}});
    connect(&process_,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,&ReceiverDialog::finished);
    connect(&process_,&QProcess::errorOccurred,this,[this](QProcess::ProcessError error){
        if(error==QProcess::FailedToStart)
            fail(process_.errorString(),QCoreApplication::translate("ReceiverDialog","Could not start the receiver utility. Check the Foreshadow installation."));
    });
    deadline_.setSingleShot(true);
    connect(&deadline_,&QTimer::timeout,this,[this]{timed_out_=true;process_.kill();});
    scan();
}
void ReceiverDialog::resizeEvent(QResizeEvent* event) {
    QDialog::resizeEvent(event);
    if(!panels_)return;
    const auto orientation=width()<1000?Qt::Vertical:Qt::Horizontal;
    if(panels_->orientation()!=orientation) {
        panels_->setOrientation(orientation);
        panels_->setSizes(orientation==Qt::Horizontal?QList<int>{400,760}:QList<int>{260,320});
    }
}

ReceiverDialog::~ReceiverDialog() {
    deadline_.stop();
    process_.disconnect(this);
    if(process_.state()!=QProcess::NotRunning) {
        process_.kill();
        process_.waitForFinished(200);
    }
}

ReceiverSelection ReceiverDialog::selection() const {
    auto serial=device_->currentData(serialRole).toString();
    int matches=0;
    for(int i=0;i<device_->count();++i)
        if(device_->itemData(i,serialRole).toString()==serial)++matches;
    if(matches!=1)serial.clear();
    auto result=initial_;
    result.backend=backend_->currentData().toString();result.index=device_->currentData().toUInt();
    result.antenna=antenna_->currentData().toString();result.manualGains=manualGains_;
    result.sampleRate=configuration().rate;result.serial=serial;result.frequency=configuration().frequency;
    result.searchWidth=searchWidth_->value()*1000;
    result.searchWidthMode=searchWidth_->mode();
    result.inputMode=inputMode_->currentData().toString();result.ifBandwidth=ifBandwidth_->currentData().toDouble();
    result.gain=configuration().auto_gain?QStringLiteral("auto"):QString::number(configuration().gain_tenths/10.,'f',1);
    result.settings=configuration().receiver_settings;
    result.capabilities=capabilities_;result.applied=applied_;return result;
}

void ReceiverDialog::reject() {
    // Let hardware application release its device before closing the utility.
    if(busy_ && request_==Request::apply)return;
    QDialog::reject();
}

Config ReceiverDialog::configuration() const {
    Config c;
    // Item data is in S/s; custom input is also an integer in S/s.
    const auto current=sampleRate_->currentIndex();
    const auto rate=current>=0 && sampleRate_->currentText()==sampleRate_->itemText(current)
        ?sampleRate_->itemData(current).toUInt():sampleRate_->currentText().toUInt();
    c.rate=rate;c.frequency=static_cast<std::uint64_t>(std::llround(frequency_->value()*1e6));
    c.device=device_->currentData().toUInt();c.device_serial=device_->currentData(serialRole).toString().toStdString();
    c.receiver_settings=pluginParameters_->values();c.receiver_settings.update(connectionParameters_->values());
    c.antenna=antenna_->currentData().toString().toStdString();
    c.input_mode=inputMode_->currentData().toString().toStdString();if(c.input_mode.empty())c.input_mode="auto";
    c.if_bandwidth_hz=ifBandwidth_->currentData().toDouble();
    c.auto_gain=gain_->currentData().toString()=="auto";
    if(!c.auto_gain)c.gain_tenths=gain_->currentData().toString().toInt();
    return c;
}
void ReceiverDialog::updatePlan() try {
    if(busy_ || !capabilities_)return;
    const auto c=configuration();auto lc=initial_.lora;lc.search_width=searchWidth_->value()*1000;
    auto plan=planMonitorConfiguration(*capabilities_,c,lc,initial_.enableLora,initial_.narrowband,initial_.snapshots);
    {const QSignalBlocker blocked(searchWidth_);searchWidth_->setAvailableWidth(plan.maximum_search_width_hz);}
    lc.search_width=searchWidth_->value()*1000.;
    plan=planMonitorConfiguration(*capabilities_,c,lc,initial_.enableLora,initial_.narrowband,initial_.snapshots);
    feedback_->setText(receiverPlanSummary(*capabilities_,c,plan));
    auto overview=feedback_->text().split(u'\n').mid(0,plan.lora_channels?5:4);
    for(const auto& issue:plan.issues)
        if(issue.code=="filter_rolloff" || issue.code=="filter_above_sample_span" || issue.code=="rtl_usb_rate_risk" || issue.code=="throughput_unverified")overview<<configurationIssueText(issue);
    coverageSummary_->setText(overview.join(u'\n'));
    QStringList rates,antennas,frequencies,widths,gains,inputs,filters;int conflicts=0;
    for(const auto& issue:plan.issues)if(issue.blocking) {
        ++conflicts;const auto text=configurationIssueText(issue);
        if(issue.code=="antenna_frequency" || issue.field=="antenna")antennas<<text;
        else if(issue.field=="sample_rate_hz")rates<<text;
        else if(issue.field=="search_width_hz")widths<<text;
        else if(issue.field=="gain_tenths_db")gains<<text;
        else if(issue.field=="input_mode")inputs<<text;
        else if(issue.field=="if_bandwidth_hz")filters<<text;
        else frequencies<<text;
    }
    const auto show=[](QLabel* label,const QStringList& messages){label->setText(messages.join(u'\n'));label->setVisible(!messages.isEmpty());};
    show(rateProblem_,rates);show(antennaProblem_,antennas);show(frequencyProblem_,frequencies);show(widthProblem_,widths);show(gainProblem_,gains);
    show(inputProblem_,inputs);show(ifProblem_,filters);
    const auto descriptors=receiverParameters(*capabilities_,c);pluginParameters_->updateDescriptors(descriptors);
    std::vector<std::pair<std::string,QString>> pluginIssues;
    for(const auto& issue:plan.issues)if(issue.blocking)pluginIssues.emplace_back(issue.field,configurationIssueText(issue));
    pluginParameters_->showIssues(pluginIssues);
    for(const auto& d:descriptors) {
        const auto key=d.value("key",std::string());
        if(key=="auto_gain")gain_->setEnabled(d.value("enabled",true));
        if(key=="gain_tenths_db")gainLabel_->setText(parameterLabel(d));
        if(key=="input_mode") {
            inputMode_->setVisible(d.value("visible",true));inputModeLabel_->setVisible(d.value("visible",true));
            inputMode_->setEnabled(d.value("enabled",true));
        }
        if(key=="if_bandwidth_hz") {
            ifBandwidth_->setVisible(d.value("visible",true));ifBandwidthLabel_->setVisible(d.value("visible",true));
            ifBandwidth_->setEnabled(d.value("enabled",true));
        }
    }
    const auto r=recommendation();recommended_->setEnabled(r.has_value());
    const auto recommendedAntenna=r && !r->receiver.antenna.empty()?QString::fromStdString(r->receiver.antenna):
        QCoreApplication::translate("ReceiverDialog","Device default");
    recommendedSummary_->setText(r?QCoreApplication::translate("ReceiverDialog","Recommended: %1 MS/s · Search %2 kHz · Automatic gain · %3. Keeps the center frequency and detector selection.")
        .arg(r->receiver.rate/1e6,0,'f',3).arg(r->search_width_hz/1000,0,'f',0).arg(recommendedAntenna):
        QCoreApplication::translate("ReceiverDialog","No compatible recommendation for this frequency and detector selection. Correct the frequency or detector settings."));
    repairWidth_->setText(QCoreApplication::translate("ReceiverDialog","Use recommended width · %1 kHz").arg(searchWidth_->recommendedValue()));
    repairWidth_->setVisible(!widths.isEmpty() && searchWidth_->recommendedValue()>=1);
    repairRate_->setText(QCoreApplication::translate("ReceiverDialog","Use compatible rate · %1 MS/s").arg(r?r->receiver.rate/1e6:0,0,'f',3));
    repairRate_->setVisible(!rates.isEmpty() && r.has_value());
    repairAntenna_->setText(QCoreApplication::translate("ReceiverDialog","Use compatible port · %1").arg(recommendedAntenna));
    repairAntenna_->setVisible(!antennas.isEmpty() && r.has_value());autoGain_->setVisible(!gains.isEmpty());
    planStatus_->setText(plan.valid()?QCoreApplication::translate("ReceiverDialog","Ready to apply · Monitoring configuration compatible"):
        QCoreApplication::translate("ReceiverDialog","Cannot apply · %1 configuration conflict(s). Fix the highlighted fields below.").arg(conflicts));
    planStatus_->setStyleSheet(plan.valid()?"color:#39a68c":"color:#e76c61");
    accept_->setEnabled(plan.valid());
    auto narrowband=initial_.narrowbandDetectors;auto snapshots=initial_.snapshotDetectors;
    if(!initial_.narrowband)narrowband={false,false,false,false};
    if(!initial_.snapshots)snapshots={{false,false},false,false,false};
    const auto rows=assessReceiverModulations(*capabilities_,c,lc,initial_.enableLora,narrowband,snapshots);
    modulationSummary_->setText(QCoreApplication::translate("ModulationAssessment","%1 MS/s · Search %2–%3 MHz. Individual rows assess each detector; Apply requires the complete configuration to be valid.")
        .arg(c.rate/1e6,0,'f',3).arg((c.frequency-lc.search_width/2)/1e6,0,'f',6).arg((c.frequency+lc.search_width/2)/1e6,0,'f',6));
    const int selectedRow=std::max(0,modulationTable_->currentRow());
    const QSignalBlocker tableSignals(modulationTable_);
    modulationTable_->setRowCount(rows.size());
    for(qsizetype i=0;i<rows.size();++i) {
        const auto& row=rows[i];const QStringList cells{row.name,row.status,row.parsingSummary,row.impactSummary};
        for(int col=0;col<cells.size();++col) {
            auto* item=new QTableWidgetItem(cells[col]);item->setToolTip(col==2?row.parsing:col==3?row.impact:cells[col]);item->setTextAlignment(Qt::AlignLeft|Qt::AlignTop);
            if(col==0){item->setData(Qt::UserRole,row.id);item->setData(Qt::UserRole+1,row.name+u'\n'+row.parsing+u'\n'+row.impact);}
            if(col==1)item->setForeground(!row.enabled?QColor("#999999"):!row.compatible?QColor("#e76c61"):!row.covered?QColor("#d59b4b"):QColor("#39a68c"));
            modulationTable_->setItem(i,col,item);
        }
    }
    modulationTable_->resizeRowsToContents();
    if(!rows.isEmpty()) {
        modulationTable_->setCurrentCell(std::min(selectedRow,int(rows.size()-1)),0);
        modulationDetails_->setPlainText(modulationTable_->item(modulationTable_->currentRow(),0)->data(Qt::UserRole+1).toString());
    }
}
catch(const std::exception& e) {fail(QString::fromUtf8(e.what()));}
std::optional<MonitorRecommendation> ReceiverDialog::recommendation() const try {
    if(!capabilities_)return std::nullopt;
    return recommendMonitorConfiguration(*capabilities_,configuration(),initial_.lora,initial_.enableLora,initial_.narrowband,initial_.snapshots);
}
catch(const std::exception&) {return std::nullopt;}
void ReceiverDialog::useRecommended() {
    const auto r=recommendation();if(!r || busy_)return;
    {const QSignalBlocker a(sampleRate_),b(antenna_),c(gain_),d(searchWidth_),e(inputMode_),f(ifBandwidth_);
        populateRates(r->receiver.rate);antenna_->setCurrentIndex(antenna_->findData(QString::fromStdString(r->receiver.antenna)));
        inputMode_->setCurrentIndex(inputMode_->findData(QString::fromStdString(r->receiver.input_mode)));
        ifBandwidth_->setCurrentIndex(ifBandwidth_->findData(r->receiver.if_bandwidth_hz));
        pluginParameters_->setValues(r->receiver.receiver_settings);
        gain_->setCurrentIndex(gain_->findData(r->receiver.auto_gain?QStringLiteral("auto"):QString::number(r->receiver.gain_tenths)));searchWidth_->setSelection(static_cast<int>(r->search_width_hz/1000),"maximum");}
    updatePlan();
}
void ReceiverDialog::populateRates(std::uint32_t preferred) {
    const QSignalBlocker blocked(sampleRate_);sampleRate_->clear();sampleRate_->setEditable(advancedRates_->isChecked());
    auto choices=receiverRateChoices(*capabilities_);
    if(std::find(choices.begin(),choices.end(),preferred)==choices.end())choices.push_back(preferred);
    for(const auto rate:choices) {
        auto cfg=configuration();cfg.rate=rate;
        const auto p=planMonitorConfiguration(*capabilities_,cfg,initial_.lora,initial_.enableLora,initial_.narrowband,initial_.snapshots);
        QStringList reasons;
        for(const auto& issue:p.issues)if(issue.blocking && issue.field=="sample_rate_hz")reasons<<configurationIssueText(issue);
        if(!advancedRates_->isChecked() && !reasons.isEmpty() && rate!=preferred)continue;
        auto label=QCoreApplication::translate("ReceiverDialog","%1 MS/s").arg(locale().toString(rate/1e6,'f',3));
        if(!reasons.isEmpty())label+=QCoreApplication::translate("ReceiverDialog"," · Incompatible");
        sampleRate_->addItem(label,rate);const auto index=sampleRate_->count()-1;
        sampleRate_->setItemData(index,reasons.join(u'\n'),Qt::ToolTipRole);
        if(!advancedRates_->isChecked() && !reasons.isEmpty())
            if(auto* model=qobject_cast<QStandardItemModel*>(sampleRate_->model()))model->item(index)->setEnabled(false);
    }
    sampleRate_->setCurrentIndex(sampleRate_->findData(preferred));
    sampleRate_->setToolTip(QCoreApplication::translate("ReceiverDialog","Compatible rates are shown by default. Advanced input accepts an integer rate in S/s; incompatible combinations cannot be applied."));
}
void ReceiverDialog::apply() {
    if(busy_ || !capabilities_)return;
    const auto selected=selection();const auto c=configuration();
    auto lc=initial_.lora;lc.search_width=searchWidth_->value()*1000.;
    if(!planMonitorConfiguration(*capabilities_,c,lc,initial_.enableLora,initial_.narrowband,initial_.snapshots).valid())return;
    QStringList args{"configure","--device",QString::number(selected.index),"--rate",QString::number(c.rate),
        "--frequency",QString::number(c.frequency),"--gain",c.auto_gain?"auto":QString::number(c.gain_tenths/10.,'f',1)};
    if(!selected.serial.isEmpty())args.append({"--serial",selected.serial});
    if(!selected.antenna.isEmpty())args.append({"--antenna",selected.antenna});
    args.append({"--input-mode",selected.inputMode,"--if-bandwidth",QString::number(selected.ifBandwidth,'f',0)});
    args.append({"--settings",QString::fromStdString(c.receiver_settings.dump())});
    launch(Request::apply,args);
}

void ReceiverDialog::setBusy(bool busy) {
    busy_=busy;
    connectionParameters_->setEnabled(!busy);
    backend_->setEnabled(!busy);
    refresh_->setEnabled(!busy);
    device_->setEnabled(!busy && device_->count()>0);
    antenna_->setEnabled(!busy && antenna_->count()>1);
    sampleRate_->setEnabled(!busy && capabilities_.has_value());
    frequency_->setEnabled(!busy);searchWidth_->setEnabled(!busy);gain_->setEnabled(!busy);advancedRates_->setEnabled(!busy);
    inputMode_->setEnabled(!busy && capabilities_ && capabilities_->direct_sampling_supported);
    ifBandwidth_->setEnabled(!busy && capabilities_ && !capabilities_->filter_bandwidths.empty());
    recommended_->setEnabled(!busy && recommendation().has_value());
    repairWidth_->setEnabled(!busy);repairRate_->setEnabled(!busy);repairAntenna_->setEnabled(!busy);autoGain_->setEnabled(!busy);
    if(auto* buttons=findChild<QDialogButtonBox*>())buttons->button(QDialogButtonBox::Cancel)->setEnabled(!busy || request_!=Request::apply);
    if(busy)accept_->setEnabled(false);
}

void ReceiverDialog::populateConnection() {
    const auto backend=backend_->currentData().toString();
    const auto* provider=find_receiver(backend.toStdString());
    Json saved=backend==initial_.backend?initial_.settings:Json::parse(QSettings().value(receiverSettingsPrefix(backend)+"settings","{}").toString().toStdString(),nullptr,false);
    if(!saved.is_object())saved=Json::object();
    connectionParameters_->populate(provider?provider->connection_parameters:Json::array(),saved);
}

void ReceiverDialog::scan() {
    clearPlan();
    manualGains_.reset();pluginParameters_->populate(Json::array(),Json::object());
    const QSignalBlocker blocked(device_);
    device_->clear();
    antenna_->clear();
    sampleRate_->clear();
    launch(Request::devices,{"devices","--settings",QString::fromStdString(connectionParameters_->values().dump())});
}

void ReceiverDialog::probe() {
    clearPlan();
    manualGains_.reset();pluginParameters_->populate(Json::array(),Json::object());
    antenna_->clear();
    sampleRate_->clear();
    if(device_->currentIndex()<0)return;
    auto arguments=QStringList{"probe","--device",QString::number(device_->currentData().toUInt())};
    const auto serial=selection().serial;
    if(!serial.isEmpty())arguments.append({"--serial",serial});
    arguments.append({"--settings",QString::fromStdString(connectionParameters_->values().dump())});
    launch(Request::probe,std::move(arguments));
}
void ReceiverDialog::clearPlan() {
    capabilities_.reset();feedback_->clear();planStatus_->clear();recommendedSummary_->clear();coverageSummary_->clear();
    modulationTable_->setRowCount(0);modulationSummary_->clear();modulationDetails_->clear();
    for(auto* label:{rateProblem_,frequencyProblem_,antennaProblem_,widthProblem_,gainProblem_,inputProblem_,ifProblem_})label->hide();
    for(auto* button:{repairWidth_,repairRate_,repairAntenna_,autoGain_})button->hide();
}

void ReceiverDialog::launch(Request request,QStringList arguments) {
    if(request!=Request::devices && access_) {const auto reason=access_(selection());if(!reason.isEmpty()){fail({},reason);return;}}
    request_=request;
    timed_out_=false;
    setBusy(true);
    status_->setToolTip({});
    status_->setText(request==Request::apply?
        QCoreApplication::translate("ReceiverDialog","Applying receiver settings…"):
        QCoreApplication::translate("ReceiverDialog","Querying the receiver…"));
    arguments.append({"--receiver",backend_->currentData().toString()});
    deadline_.start(5000);
    process_.start(companion_cli(),arguments);
}

void ReceiverDialog::fail(const QString& detail,const QString& message) {
    deadline_.stop();
    status_->setText(message.isEmpty()
        ?QCoreApplication::translate("ReceiverDialog","Could not query the receiver. Check its connection and driver.")
        :message);
    status_->setToolTip(detail.isEmpty()?QString():"<pre>"+detail.toHtmlEscaped()+"</pre>");
    setBusy(false);
    accept_->setEnabled(false);
}

void ReceiverDialog::finished(int code,QProcess::ExitStatus state) {
    deadline_.stop();
    const auto output=process_.readAllStandardOutput();
    const auto detail=QString::fromUtf8(process_.readAllStandardError().right(3000));
    if(timed_out_) {
        fail({},QCoreApplication::translate("ReceiverDialog","Receiver query timed out."));
        return;
    }
    if(code!=0 || state!=QProcess::NormalExit) {
        fail(detail,request_==Request::apply?
            QCoreApplication::translate("ReceiverDialog","Could not apply the receiver settings. Check the device and driver."):QString());return;
    }
    const auto result=QJsonDocument::fromJson(output);
    if(!result.isObject()) {
        fail(detail+u'\n'+QString::fromUtf8(output.left(3000)),
             QCoreApplication::translate("ReceiverDialog","The receiver utility returned an invalid response. Check the Foreshadow installation."));
        return;
    }
    const auto values=result.object();
    if(request_==Request::apply) {
        try {
            applied_=Json::parse(output.constData());
            if(!applied_.contains("actual") || !applied_.at("actual").contains("sample_rate_hz"))
                throw std::invalid_argument("Missing configuration readback");
            accept();
        } catch(const std::exception& e) {fail(QString::fromUtf8(e.what()));}
        return;
    }
    if(request_==Request::devices) {
        if(values.value("backend")=="unavailable") {
            fail(values.value("reason").toString(),receiverUnavailableMessage(values));
            return;
        }
        const QSignalBlocker blocked(device_);
        for(const auto& value:values.value("devices").toArray()) {
            const auto item=value.toObject();
            const auto index=item.value("index").toInt(-1);
            if(index<0 || index>1000)continue;
            const auto identification=item.value("identification").toObject();
            auto name=identification.value("label").toString();
            if(name.isEmpty())name=item.value("name").toString();
            device_->addItem(QStringLiteral("%1 · %2").arg(index).arg(name.isEmpty()?backend_->currentText():name),index);
            device_->setItemData(device_->count()-1,identification.value("serial").toString(),serialRole);
        }
        if(!device_->count()) {
            status_->setText(QCoreApplication::translate("ReceiverDialog","No receivers found."));
            setBusy(false);
            return;
        }
        const bool sameBackend=backend_->currentData().toString()==initial_.backend;
        const auto saved=sameBackend
            ?(initial_.serial.isEmpty()?device_->findData(static_cast<int>(initial_.index)):
                device_->findData(initial_.serial,serialRole)):-1;
        if(sameBackend && !initial_.serial.isEmpty() && saved<0) {
            device_->setCurrentIndex(-1);
            fail({},QCoreApplication::translate("ReceiverDialog","The saved receiver is not connected. Select another device or refresh."));
            return;
        }
        device_->setCurrentIndex(saved<0?0:saved);
        if(sameBackend && !initial_.serial.isEmpty() && selection().serial.isEmpty()) {
            device_->setCurrentIndex(-1);
            fail({},QCoreApplication::translate("ReceiverDialog","More than one entry matches the saved receiver. Select a device."));
            return;
        }
        probe();
        return;
    }
    manualGains_=probeGainPresets(values);
    const auto backend=backend_->currentData().toString();
    const auto& receiver=receiver_by_name(backend.toStdString());
    gainLabel_->setText(receiver.gain_kind==ReceiverGainKind::if_reduction?
        QCoreApplication::translate("MonitorWindow","IF gain reduction (dB)"):
        QCoreApplication::translate("MonitorWindow","Gain dB"));
    try {capabilities_=Json::parse(output.constData()).at("capabilities").get<ReceiverCapabilities>();}
    catch(const std::exception& e){fail(QString::fromUtf8(e.what()));return;}
    {
        Json saved=initial_.backend==backend?initial_.settings:Json::parse(QSettings().value(receiverSettingsPrefix(backend)+"settings","{}").toString().toStdString(),nullptr,false);
        if(!saved.is_object())saved=Json::object();
        auto descriptors=receiverParameters(*capabilities_,configuration());
        auto deviceFields=Json::array();for(const auto& d:descriptors)if(!d.value("connection",false))deviceFields.push_back(d);
        pluginParameters_->populate(deviceFields,saved);
    }
    {const QSignalBlocker a(inputMode_),b(ifBandwidth_);
        const auto prefix=receiverSettingsPrefix(backend);
        const auto input=backend==initial_.backend?initial_.inputMode:QSettings().value(prefix+"input_mode","auto").toString();
        inputMode_->clear();
        for(const auto& d:receiverParameters(*capabilities_,configuration()))if(d.value("key",std::string())=="input_mode") {
            for(const auto& value:d.at("choices")) {
                const auto key=QString::fromStdString(value.get<std::string>());
                auto label=key;
                if(key=="auto")label=QCoreApplication::translate("ReceiverDialog","Automatic input");
                else if(key=="tuner")label=QCoreApplication::translate("ReceiverDialog","Tuner");
                else if(key=="direct_i")label=QCoreApplication::translate("ReceiverDialog","Direct sampling I");
                else if(key=="direct_q")label=QCoreApplication::translate("ReceiverDialog","Direct sampling Q (HF)");
                inputMode_->addItem(label,key);
            }
        }
        if(!inputMode_->count())inputMode_->addItem(QCoreApplication::translate("ReceiverDialog","Automatic input"),"auto");
        auto inputIndex=inputMode_->findData(input);
        if(inputIndex<0){inputMode_->addItem(input,input);inputIndex=inputMode_->count()-1;}
        inputMode_->setCurrentIndex(inputIndex);
        inputMode_->setVisible(capabilities_->direct_sampling_supported);inputModeLabel_->setVisible(capabilities_->direct_sampling_supported);
        ifBandwidth_->clear();ifBandwidth_->addItem(QCoreApplication::translate("ReceiverDialog","Automatic filter"),0.);
        for(const auto width:capabilities_->filter_bandwidths)ifBandwidth_->addItem(QStringLiteral("%1 MHz").arg(width/1e6,0,'f',3),width);
        const auto saved=backend==initial_.backend?initial_.ifBandwidth:QSettings().value(prefix+"if_bandwidth_hz",0.).toDouble();
        auto index=ifBandwidth_->findData(saved);if(index<0){ifBandwidth_->addItem(QStringLiteral("%1 MHz").arg(saved/1e6),saved);index=ifBandwidth_->count()-1;}
        ifBandwidth_->setCurrentIndex(index);
        ifBandwidth_->setVisible(!capabilities_->filter_bandwidths.empty());ifBandwidthLabel_->setVisible(!capabilities_->filter_bandwidths.empty());
    }
    const auto preferred=backend==initial_.backend && initial_.sampleRate?initial_.sampleRate:
        QSettings().value(receiverSettingsPrefix(backend)+"sample_rate_hz",2'000'000u).toUInt();
    populateRates(preferred);
    gain_->clear();gain_->addItem(QCoreApplication::translate("MonitorWindow","Automatic gain"),"auto");
    for(const auto g:capabilities_->manual_gains_tenths)
        gain_->addItem(QString::number(g/10.,'f',1),QString::number(g));
    const auto preferredGain=backend==initial_.backend && !initial_.gain.isEmpty()?initial_.gain:
        QSettings().value(receiverSettingsPrefix(backend)+"gain",QString::fromLatin1(receiver.default_gain)).toString();
    const auto gainIndex=gain_->findData(preferredGain=="auto"?preferredGain:QString::number(std::lround(preferredGain.toDouble()*10)));
    if(gainIndex<0)gain_->addItem(preferredGain,QString::number(std::lround(preferredGain.toDouble()*10)));
    gain_->setCurrentIndex(gainIndex>=0?gainIndex:gain_->count()-1);
    antenna_->addItem(QCoreApplication::translate("ReceiverDialog","Device default"),QString());
    for(const auto& value:values.value("antennas").toArray()) {
        const auto name=value.toString();
        if(!name.isEmpty())antenna_->addItem(name,name);
    }
    if(backend==initial_.backend && (initial_.serial.isEmpty()?device_->currentData().toUInt()==initial_.index:
                                    device_->currentData(serialRole).toString()==initial_.serial)) {
        const auto saved=antenna_->findData(initial_.antenna);
        if(saved>=0)antenna_->setCurrentIndex(saved);
    }
    status_->setText(receiverProbeSummary(values));
    setBusy(false);
    updatePlan();
}
}
