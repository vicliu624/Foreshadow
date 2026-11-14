#include "analysis_panel.hpp"
#include "plugins/desktop/research/research_provider.hpp"
#include "plugins/desktop/research/research_audio_playback.hpp"
#include "decimal_input.hpp"
#include "signalscope/desktop/number_format.hpp"
#include "desktop_error.hpp"
#include "plugins/desktop/research/external_research.hpp"
#include "plugins/desktop/research/research_range.hpp"
#include "signalscope/backend/iq/iq.hpp"
#include <algorithm>
#include <functional>
#include <vector>
#include <cmath>
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QSaveFile>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>
#include <thread>
#include <utility>
#ifdef Q_OS_LINUX
#include <sched.h>
#endif

namespace {
using signalscope::desktop::ResearchResult;
using signalscope::desktop::ResearchAudio;
using signalscope::desktop::ResearchArtifact;
using signalscope::desktop::ExternalResearchPlugin;
using signalscope::desktop::ResearchSampleRange;
using SampleRange=std::optional<ResearchSampleRange>;
using signalscope::desktop::ResearchOptions;
using signalscope::desktop::ResearchProvider;
using signalscope::desktop::ResearchControls;
}

class AnalysisPanel::Impl {
public:
    Impl(AnalysisPanel* owner,const QString& pluginDirectory,const QString& language)
        :owner_(owner),external_(signalscope::desktop::discoverResearchPlugins(pluginDirectory,language)),
         plots_(signalscope::desktop::researchProviders(external_)) {
        auto* layout=new QVBoxLayout(owner);
        auto* tools=new QHBoxLayout;
        plot_=new QComboBox;
        for(const auto& plot:plots_) {
            plot_->addItem(plot.plugin.title,plot.plugin.id);
            if(plot.plugin.external)
                item(plot_->count()-1)->setEnabled(QSettings().value("research/"+plot.plugin.id,true).toBool());
        }
        zoom_=new QComboBox;zoom_->addItem(QCoreApplication::translate("AnalysisPanel", "Fit width"),0);
        zoom_->addItem(QCoreApplication::translate("AnalysisPanel", "Fit page"),-1);
        for(int n:{100,150,200})zoom_->addItem(QString::number(n)+"%",n);
        save_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Export analysis plot"));save_->setEnabled(false);
        cancel_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Cancel analysis"));cancel_->setEnabled(false);
        tools->addWidget(plot_);tools->addWidget(zoom_);tools->addWidget(save_);tools->addWidget(cancel_);tools->addStretch();layout->addLayout(tools);
        process_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Process analysis data…"));
        auto* processorMenu=new QMenu(process_);
        process_->setMenu(processorMenu);
        process_->hide();
        layout->addWidget(process_);
        previous_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Back to input data"));
        previous_->hide();
        layout->addWidget(previous_);
        QObject::connect(previous_,&QPushButton::clicked,owner,[this]{restorePreviousArtifact();});
        QObject::connect(processorMenu,&QMenu::aboutToShow,owner,[this,processorMenu]{
            processorMenu->clear();
            if(!artifact_)return;
            for(const auto& plugin:external_) {
                if(plugin.accepts(artifact_->format) && QSettings().value("research/"+plugin.plugin.id,true).toBool())
                    processorMenu->addAction(plugin.plugin.title,owner_,[this,id=plugin.plugin.id]{processArtifact(id);});
            }
        });
        rangeControls_=new QWidget;
        auto* rangeLayout=new QHBoxLayout(rangeControls_);
        rangeLayout->setContentsMargins(0,0,0,0);
        rangeUnit_=new QComboBox;
        rangeUnit_->addItem(QCoreApplication::translate("AnalysisPanel", "Samples"));
        rangeUnit_->addItem(QCoreApplication::translate("AnalysisPanel", "Seconds"));
        rangeLayout->addWidget(rangeUnit_);
        firstSample_=new QLineEdit;
        sampleCount_=new QLineEdit;
        rangeValidator_=new QRegularExpressionValidator(QRegularExpression("[0-9]{0,20}"),rangeControls_);
        firstSample_->setValidator(rangeValidator_);
        sampleCount_->setValidator(rangeValidator_);
        firstLabel_=new QLabel(QCoreApplication::translate("AnalysisPanel", "First sample"));
        countLabel_=new QLabel(QCoreApplication::translate("AnalysisPanel", "Sample count"));
        rangeLayout->addWidget(firstLabel_);
        rangeLayout->addWidget(firstSample_);
        rangeLayout->addWidget(countLabel_);
        rangeLayout->addWidget(sampleCount_);
        applyRange_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Analyze range"));
        rangeLayout->addWidget(applyRange_);
        layout->addWidget(rangeControls_);
        controls_.resize(plots_.size());
        for(std::size_t i=0;i<plots_.size();++i)if(plots_[i].controls) {
            controls_[i]=plots_[i].controls(owner);
            if(controls_[i].widget){layout->addWidget(controls_[i].widget);controls_[i].widget->hide();}
        }
        updateRangeControls();
        QObject::connect(rangeUnit_,&QComboBox::currentIndexChanged,owner,[this](int index){
            const bool seconds=index==1;
            firstSample_->clear();
            sampleCount_->clear();
            rangeValidator_->setRegularExpression(seconds
                ?signalscope::desktop::decimalInputPattern(owner_->locale(),20,9)
                :QRegularExpression(QStringLiteral("[0-9]{0,20}")));
            firstLabel_->setText(seconds?QCoreApplication::translate("AnalysisPanel", "Start (s)")
                                        :QCoreApplication::translate("AnalysisPanel", "First sample"));
            countLabel_->setText(seconds?QCoreApplication::translate("AnalysisPanel", "Duration (s)")
                                        :QCoreApplication::translate("AnalysisPanel", "Sample count"));
        });
        status_=new QLabel(QCoreApplication::translate("AnalysisPanel", "Select an I/Q recording and choose Signal analysis. Plots are generated in the background and the original data is preserved."));status_->setTextFormat(Qt::PlainText);status_->setWordWrap(true);layout->addWidget(status_);
        scroll_=new QScrollArea;image_=new QLabel;image_->setAlignment(Qt::AlignTop|Qt::AlignLeft);scroll_->setWidget(image_);layout->addWidget(scroll_,1);
        reportView_=new QPlainTextEdit;
        reportView_->setReadOnly(true);
        reportView_->hide();
        layout->addWidget(reportView_,1);
        playAudio_=new QPushButton(QCoreApplication::translate("AnalysisPanel", "Play audio"));
        playAudio_->hide();
        layout->addWidget(playAudio_,0,Qt::AlignLeft);
#ifdef SIGNALSCOPE_HAS_AUDIO
        QObject::connect(playAudio_,&QPushButton::clicked,owner,[this]{
            if(!audio_)return;
            if(audioPlayer_.playing())audioPlayer_.stop();
            else audioPlayer_.play(*audio_);
        });
        audioPlayer_.playingChanged=[this](bool playing){
            playAudio_->setText(playing
                ?QCoreApplication::translate("AnalysisPanel", "Stop audio")
                :QCoreApplication::translate("AnalysisPanel", "Play audio"));
        };
        audioPlayer_.failed=[this](const QString& detail){
            if(!audio_)return;
            status_->setText(QCoreApplication::translate("AnalysisPanel", "Could not play the analysis audio."));
            status_->setToolTip("<pre>"+detail.toHtmlEscaped()+"</pre>");
        };
#endif
        QObject::connect(zoom_,&QComboBox::currentIndexChanged,owner,[this]{display();});
        QObject::connect(plot_,&QComboBox::currentIndexChanged,owner,[this]{
            updateRangeControls();
            if(!metadata_.isEmpty())analyze(metadata_);
        });
        QObject::connect(applyRange_,&QPushButton::clicked,owner,[this]{if(!metadata_.isEmpty())analyze(metadata_);});
        QObject::connect(save_,&QPushButton::clicked,owner,[this]{exportResult();});
        QObject::connect(cancel_,&QPushButton::clicked,owner,[this]{native_.request_stop();});
    }
    ~Impl(){
        if(native_.joinable()){native_.request_stop();native_.join();}
    }

    QStandardItem* item(int index) const {
        return static_cast<QStandardItemModel*>(plot_->model())->item(index);
    }
    bool enabled(int index) const {
        return index>=0 && index<plot_->count() && item(index)->isEnabled();
    }
    void updateRangeControls() {
        const int index=plot_->currentIndex();
        rangeControls_->setVisible(index>=0 && plots_[index].supportsRange);
        rangeControls_->setEnabled(!busy_);
        for(std::size_t i=0;i<controls_.size();++i)if(controls_[i].widget) {
            controls_[i].widget->setVisible(static_cast<int>(i)==index);controls_[i].widget->setEnabled(!busy_);
        }
        applyRange_->setText(index>=0 && !plots_[index].runLabel.isEmpty()?plots_[index].runLabel:
            QCoreApplication::translate("AnalysisPanel","Analyze range"));
        applyRange_->setEnabled(!metadata_.isEmpty());
    }

    void endAnalysis() {
        busy_=false;
        activePluginId_.clear();
        cancel_->setEnabled(false);
        plot_->setEnabled(true);
        updateProcessors();
        updateRangeControls();
        const auto next=std::exchange(pendingPlot_,-1);
        if(enabled(next))plot_->setCurrentIndex(next);
    }
    QString resultFilename() const {
        const auto extension=artifact_?QStringLiteral(".%1.v%2.bin").arg(artifact_->format.id).arg(artifact_->format.version):
            audio_?".wav":!report_.isEmpty()?".txt":".png";
        return resultPluginId_+extension;
    }
    bool saveResult(const QString& path) {
        if(!save_->isEnabled())return false;
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly))return false;
        const bool text=!report_.isEmpty();
        const auto bytes=artifact_?artifact_->bytes:audio_?audio_->wav:report_.toUtf8();
        const bool written=(artifact_ || audio_ || text)?file.write(bytes)==bytes.size():original_.save(&file,"PNG");
        return written && file.commit();
    }
    void exportResult() {
        const auto filter=artifact_?QCoreApplication::translate("AnalysisPanel", "Binary files (*.bin)"):
            audio_?QStringLiteral("WAV (*.wav)"):
            !report_.isEmpty()?QCoreApplication::translate("AnalysisPanel", "Text files (*.txt)"):QStringLiteral("PNG (*.png)");
        const auto path=QFileDialog::getSaveFileName(owner_,save_->text(),
            QDir::home().filePath(resultFilename()),
            filter,nullptr,QFileDialog::DontUseNativeDialog);
        if(path.isEmpty())return;
        if(!saveResult(path))
            status_->setText(QCoreApplication::translate("AnalysisPanel", "Could not export the analysis result."));
    }
    void showResult(const ResearchResult& result,const QString& title) {
        resultTitle_=title;
        if(const auto* report=std::get_if<QString>(&result)) {
            report_=*report;
            reportView_->setPlainText(report_);
            reportView_->show();
            scroll_->hide();
            zoom_->setEnabled(false);
            save_->setText(QCoreApplication::translate("AnalysisPanel", "Export analysis report"));
        } else if(const auto* audio=std::get_if<ResearchAudio>(&result)) {
            audio_=*audio;
            auto summary=QCoreApplication::translate("AnalysisPanel", "Audio · %1 Hz · %2 channel(s) · %3 s")
                .arg(audio->rate).arg(audio->channels)
                .arg(signalscope::desktop::displayDecimal(double(audio->frames)/audio->rate,3,owner_->locale()));
            if(!audio->description.isEmpty())summary+="\n"+audio->description;
            reportView_->setPlainText(summary);
            reportView_->show();
            scroll_->hide();
            zoom_->setEnabled(false);
            save_->setText(QCoreApplication::translate("AnalysisPanel", "Export analysis audio"));
#ifdef SIGNALSCOPE_HAS_AUDIO
            playAudio_->show();
#endif
        } else if(const auto* artifact=std::get_if<ResearchArtifact>(&result)) {
            artifact_=*artifact;
            reportView_->setPlainText(QCoreApplication::translate("AnalysisPanel", "Format %1 · Version %2 · %3 bytes")
                .arg(artifact->format.id).arg(artifact->format.version).arg(artifact->bytes.size()));
            reportView_->show();
            scroll_->hide();
            zoom_->setEnabled(false);
            save_->setText(QCoreApplication::translate("AnalysisPanel", "Export analysis data"));
        } else {
            original_=QPixmap::fromImage(std::get<QImage>(result));
            if(original_.isNull()) {
                status_->setText(QCoreApplication::translate("AnalysisPanel", "The plotting process did not produce a valid image."));
                return;
            }
            display();
        }
        status_->setText(title+" · "+QFileInfo(metadata_).fileName());
        status_->setToolTip(metadata_);
        save_->setEnabled(true);
    }
    void clearResult() {
        save_->setEnabled(false);
        original_=QPixmap();
        image_->clear();
        report_.clear();
        audio_.reset();
        artifact_.reset();
        process_->hide();
#ifdef SIGNALSCOPE_HAS_AUDIO
        audioPlayer_.stop();
#endif
        playAudio_->hide();
        reportView_->clear();
        reportView_->hide();
        scroll_->show();
        zoom_->setEnabled(true);
        save_->setText(QCoreApplication::translate("AnalysisPanel", "Export analysis plot"));
    }
    void analyze(const QString& metadata) {
        if(busy_)return;
        if(!enabled(plot_->currentIndex())) {
            const QSignalBlocker blocker(plot_);
            plot_->setCurrentIndex(0);
        }
        status_->setToolTip({});
        metadata_=metadata;
        previousArtifact_.reset();
        previous_->hide();
        clearResult();
        const auto* plot=&plots_[plot_->currentIndex()];
        ResearchOptions options;
        try {if(controls_[plot_->currentIndex()].read)options.parameters=controls_[plot_->currentIndex()].read();}
        catch(const std::exception& e){updateRangeControls();status_->setText(QString::fromUtf8(e.what()));return;}
        const auto startText=firstSample_->text(),countText=sampleCount_->text();
        const bool hasRange=plot->supportsRange && (!startText.isEmpty() || !countText.isEmpty());
        const bool timeRange=hasRange && rangeUnit_->currentIndex()==1;
        const auto startSeconds=timeRange?signalscope::desktop::canonicalDecimalInput(startText,owner_->locale()):QString();
        const auto durationSeconds=timeRange?signalscope::desktop::canonicalDecimalInput(countText,owner_->locale()):QString();
        if(hasRange && !timeRange) {
            bool firstOk=false,countOk=false;
            const auto first=firstSample_->text().toULongLong(&firstOk);
            const auto count=sampleCount_->text().toULongLong(&countOk);
            if(!firstOk || !countOk || !count) {
                updateRangeControls();
                status_->setText(QCoreApplication::translate("AnalysisPanel", "Enter a starting sample and a positive sample count, or leave both fields blank."));
                return;
            }
            options.range=ResearchSampleRange{first,count};
        }
        startTask(plot->plugin,
            QCoreApplication::translate("AnalysisPanel", "Analyzing recorded I/Q…"),
            [plot,path=metadata_,threshold=threshold_db_,options,timeRange,startSeconds,durationSeconds]
            (std::stop_token stop,QString& error) mutable -> ResearchResult {
                if(timeRange) {
                    const auto info=signalscope::inspect_recording(QFile(path).filesystemFileName());
                    options.range=signalscope::desktop::researchRangeFromSeconds(startSeconds,durationSeconds,info.rate);
                    const auto& range=options.range;
                    if(!range || range->first>=info.pairs || range->count>info.pairs-range->first) {
                        error=QCoreApplication::translate("AnalysisPanel", "Enter a valid start and duration within the recording.");
                        return {};
                    }
                }
                return plot->render(path,threshold,stop,options);
            });
    }
    void updateProcessors() {
        process_->setVisible(artifact_ && std::any_of(external_.begin(),external_.end(),[this](const auto& plugin){
            return plugin.accepts(artifact_->format) && QSettings().value("research/"+plugin.plugin.id,true).toBool();
        }));
        process_->setEnabled(!busy_);
        previous_->setVisible(previousArtifact_.has_value());
        previous_->setEnabled(!busy_);
    }
    bool restorePreviousArtifact() {
        if(busy_ || !previousArtifact_)return false;
        auto previous=std::exchange(previousArtifact_,std::nullopt);
        clearResult();
        resultPluginId_=std::move(previous->pluginId);
        showResult(ResearchResult{std::move(previous->data)},previous->title);
        updateProcessors();
        return true;
    }
    bool processArtifact(const QString& id) {
        if(busy_ || !artifact_ || !QSettings().value("research/"+id,true).toBool())return false;
        const auto plugin=std::find_if(external_.begin(),external_.end(),[&](const auto& entry){
            return entry.plugin.id==id && entry.accepts(artifact_->format);
        });
        if(plugin==external_.end())return false;
        startTask(plugin->plugin,QCoreApplication::translate("AnalysisPanel", "Processing analysis data…"),
                  [processor=*plugin,input=*artifact_](std::stop_token stop,QString&){
                      return processor.render(input,stop);
                  });
        return true;
    }
    template<class Work>
    void startTask(const signalscope::desktop::ResearchPlugin& plugin,const QString& progress,Work work) {
        busy_=true;
        activePluginId_=plugin.id;
        updateRangeControls();
        updateProcessors();
        cancel_->setEnabled(true);
        plot_->setEnabled(false);
        status_->setText(progress);
        try {
            native_=std::jthread([this,id=plugin.id,title=plugin.title,work=std::move(work)](std::stop_token stop) mutable {
#ifdef Q_OS_LINUX
                sched_param priority{};
                sched_setscheduler(0,SCHED_IDLE,&priority);
#endif
                ResearchResult result;
                QString error,detail;
                try {if(!stop.stop_requested())result=work(stop,error);}
                catch(const std::exception& e){
                    error=signalscope::desktop::error_summary(
                        e,QCoreApplication::translate("AnalysisPanel", "Analysis could not finish. See the status tooltip for details."));
                    detail=QString::fromUtf8(e.what());
                }
                QMetaObject::invokeMethod(owner_,[this,id,title,result=std::move(result),error,detail]{
                    const bool cancelled=native_.get_stop_token().stop_requested();
                    native_.join();
                    if(cancelled)
                        status_->setText(QCoreApplication::translate("AnalysisPanel", "Analysis cancelled. The original recording was preserved."));
                    else if(!error.isEmpty()) {
                        status_->setText(error);
                        status_->setToolTip(detail.isEmpty()?QString():"<pre>"+detail.toHtmlEscaped()+"</pre>");
                    } else {
                        if(artifact_)
                            previousArtifact_=PreviousArtifact{std::move(*artifact_),resultPluginId_,resultTitle_};
                        clearResult();
                        resultPluginId_=id;
                        showResult(result,title);
                    }
                    endAnalysis();
                },Qt::QueuedConnection);
            });
        } catch(const std::exception& e) {
            signalscope::desktop::show_error(
                *status_,e,QCoreApplication::translate("AnalysisPanel", "Could not start analysis."));
            endAnalysis();
        }
    }
    void display(){
        if(original_.isNull())return;const int percent=zoom_->currentData().toInt();
        scroll_->setHorizontalScrollBarPolicy(percent>0?Qt::ScrollBarAsNeeded:Qt::ScrollBarAlwaysOff);
        const int width=percent>0?original_.width()*percent/100:std::max(100,scroll_->viewport()->width()-5);
        auto pixmap=percent<0?original_.scaled(width,std::max(100,scroll_->viewport()->height()-5),
                                             Qt::KeepAspectRatio,Qt::SmoothTransformation)
                             :original_.scaledToWidth(width,Qt::SmoothTransformation);
        image_->setPixmap(pixmap);image_->resize(pixmap.size());
        if(!percent)QTimer::singleShot(0,owner_,[this,width]{if(!zoom_->currentData().toInt()&&width!=std::max(100,scroll_->viewport()->width()-5))display();});
    }
    AnalysisPanel* owner_;QString metadata_;bool busy_=false;double threshold_db_=18;
    QList<ExternalResearchPlugin> external_;
    const std::vector<ResearchProvider> plots_;
    QString activePluginId_,resultPluginId_,resultTitle_;
    int pendingPlot_=-1;
    std::jthread native_;
    QString report_;
    std::optional<ResearchAudio> audio_;
    std::optional<ResearchArtifact> artifact_;
    struct PreviousArtifact {
        ResearchArtifact data;
        QString pluginId,title;
    };
    std::optional<PreviousArtifact> previousArtifact_;
    QPushButton *playAudio_,*process_,*previous_;
#ifdef SIGNALSCOPE_HAS_AUDIO
    signalscope::desktop::ResearchAudioPlayback audioPlayer_;
#endif
    QPlainTextEdit* reportView_;
    QWidget* rangeControls_;
    std::vector<ResearchControls> controls_;
    QComboBox* rangeUnit_;
    QRegularExpressionValidator* rangeValidator_;
    QLabel *firstLabel_,*countLabel_;
    QLineEdit *firstSample_,*sampleCount_;
    QPushButton* applyRange_;
    QLabel *image_,*status_;QScrollArea* scroll_;QComboBox *zoom_,*plot_;QPushButton *save_,*cancel_;QPixmap original_;
};
AnalysisPanel::AnalysisPanel(const QString& pluginDirectory,const QString& language,QWidget* parent)
    :QWidget(parent),impl_(std::make_unique<Impl>(this,pluginDirectory,language)){}
AnalysisPanel::~AnalysisPanel()=default;
void AnalysisPanel::analyze(const QString& metadata){impl_->analyze(metadata);}
bool AnalysisPanel::analyzeRange(const QString& metadata,const QString& pluginId,ResearchSampleRange range,
                                std::optional<double> frequency) {
    const auto index=impl_->plot_->findData(pluginId);
    if(impl_->busy_ || !impl_->enabled(index) || !impl_->plots_[index].supportsRange)return false;
    {
        const QSignalBlocker blocker(impl_->plot_);
        impl_->plot_->setCurrentIndex(index);
    }
    impl_->rangeUnit_->setCurrentIndex(0);
    impl_->firstSample_->setText(QString::number(qulonglong(range.first)));
    impl_->sampleCount_->setText(QString::number(qulonglong(range.count)));
    impl_->updateRangeControls();
    if(frequency && impl_->controls_[index].setContext)impl_->controls_[index].setContext({{"center_frequency_hz",*frequency}});
    impl_->analyze(metadata);
    return true;
}
void AnalysisPanel::setThreshold(double value){impl_->threshold_db_=value;}
bool AnalysisPanel::hasResult() const{return impl_->save_->isEnabled();}
bool AnalysisPanel::restorePreviousArtifact(){return impl_->restorePreviousArtifact();}
bool AnalysisPanel::exportResultToDirectory(const QString& directory) {
    return !directory.isEmpty() && impl_->saveResult(QDir(directory).filePath(impl_->resultFilename()));
}
bool AnalysisPanel::isAnalyzing() const{return impl_->busy_;}
bool AnalysisPanel::selectPlot(const QString& id) {
    const auto index=impl_->plot_->findData(id);
    if(index<0 && std::any_of(impl_->external_.begin(),impl_->external_.end(),[&](const auto& plugin){
        return plugin.input && plugin.plugin.id==id;
    })) {
        if(!impl_->processArtifact(id) && !impl_->busy_)
            impl_->status_->setText(QCoreApplication::translate("AnalysisPanel", "Produce a compatible analysis result before running this plugin."));
        return true;
    }
    if(!impl_->enabled(index))return false;
    if(impl_->busy_)impl_->pendingPlot_=index;
    else impl_->plot_->setCurrentIndex(index);
    return true;
}
void AnalysisPanel::setPluginEnabled(const QString& id,bool enabled) {
    const auto index=impl_->plot_->findData(id);
    if(index<0 && std::any_of(impl_->external_.begin(),impl_->external_.end(),[&](const auto& plugin){
        return plugin.input && plugin.plugin.id==id;
    })) {
        QSettings().setValue("research/"+id,enabled);
        if(!enabled && impl_->activePluginId_==id)impl_->native_.request_stop();
        impl_->updateProcessors();
        return;
    }
    if(index<0 || !impl_->plots_[index].plugin.external)return;
    QSettings().setValue("research/"+id,enabled);
    const bool current=impl_->plot_->currentIndex()==index;
    const QSignalBlocker blocker(impl_->plot_);
    impl_->item(index)->setEnabled(enabled);
    if(!enabled && impl_->pendingPlot_==index)impl_->pendingPlot_=-1;
    if(!enabled && impl_->busy_ && current)impl_->native_.request_stop();
}
void AnalysisPanel::forgetPlugin(const QString& id) {
    setPluginEnabled(id,false);
    impl_->external_.removeIf([&](const auto& plugin){return plugin.input && plugin.plugin.id==id;});
    QSettings().remove("research/"+id);
}
QList<signalscope::desktop::ResearchPlugin> AnalysisPanel::plugins() const {
    QList<signalscope::desktop::ResearchPlugin> plugins;
    for(int i=0;i<impl_->plot_->count();++i) {
        auto plugin=impl_->plots_[i].plugin;
        plugin.enabled=impl_->enabled(i);
        plugins.push_back(std::move(plugin));
    }
    for(const auto& entry:impl_->external_) {
        if(!entry.input)continue;
        auto plugin=entry.plugin;
        plugin.enabled=QSettings().value("research/"+plugin.id,true).toBool();
        plugins.push_back(std::move(plugin));
    }
    return plugins;
}
void AnalysisPanel::resizeEvent(QResizeEvent* event){QWidget::resizeEvent(event);impl_->display();}
