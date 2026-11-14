#include "live_tools.hpp"
#include <QCoreApplication>
#include <QLabel>
#include <QMenu>
#include <QMetaObject>
#include <QAction>
#include <QThread>
#include <mutex>
#include <set>
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"

namespace signalscope::desktop {
struct LiveTools::Dispatch {std::mutex mutex;LiveTools* target;std::atomic_bool alive=true;explicit Dispatch(LiveTools* value):target(value){}};
LiveTools::LiveTools(std::vector<LiveResearchProvider> providers,const QString& language,QWidget* parent):QObject(parent),dispatch_(std::make_shared<Dispatch>(this)),language_(language) {
    for(auto& provider:providers) {
        LiveResearchTool tool;QString loadError;
        try {
            if(!supportsLiveResearchProtocol(provider.protocolVersion) || !provider.create)throw std::invalid_argument("Unsupported live research protocol");
            tool=provider.create(language,parent);
            if(!tool.widget)throw std::invalid_argument("Live research provider returned no panel");
            if(provider.protocolVersion<2 && (tool.attachReception || tool.summary || tool.health || tool.attachReceiverControls))throw std::invalid_argument("Live research v2 capabilities require protocol v2");
            if(provider.protocolVersion<3 && (!tool.panels.empty() || tool.recordingActions || !tool.monitorBound || !tool.workspaceLayoutTitle.isEmpty()))throw std::invalid_argument("Multipart research and recording inputs require protocol v3");
            std::set<QString> ids;std::set<QWidget*> widgets{tool.widget};
            for(const auto& panel:tool.panels) {
                if(panel.id.isEmpty() || !ids.insert(panel.id).second || !panel.widget || !widgets.insert(panel.widget).second)
                    throw std::invalid_argument("Duplicate or incomplete research panel descriptor");
                if(panel.relativeTo && (panel.relativeTo==panel.widget || !widgets.contains(panel.relativeTo)))throw std::invalid_argument("Research panel placement must reference an earlier owned panel");
            }
        } catch(const std::exception& error) {
            try{if(tool.stop)tool.stop();}catch(...){}
            delete tool.widget;tool={};
            loadError=QString::fromUtf8(error.what());
            auto* label=new QLabel(QCoreApplication::translate("LiveTools","Could not load live tool %1: %2")
                .arg(provider.id,QString::fromUtf8(error.what())),parent);
            label->setWordWrap(true);label->setWindowTitle(provider.id);
            if(tool.stop)try{tool.stop();}catch(...){}
            tool={};tool.widget=label;tool.initiallyVisible=true;
        }
        primary_panels_.push_back(panels_.size());
        if(!tool.workspaceLayoutTitle.isEmpty())layouts_.push_back({"live-research/"+provider.id,tool.workspaceLayoutTitle,tool.widget,tool.panels});
        panels_.push_back({"live-research/"+provider.id,tool.widget->windowTitle(),tool.widget,tool.initiallyVisible,loadError});
        for(const auto& panel:tool.panels)
            panels_.push_back({"live-research/"+provider.id+"/"+panel.id,panel.title,panel.widget,panel.initiallyVisible,{},panel.preferredArea,panel.relativeTo,panel.orientation});
        tools_.push_back(std::move(tool));
        faulted_.push_back(std::make_unique<std::atomic<bool>>(false));
        failures_.push_back({});
        configurationWaitPairs_.push_back(std::make_shared<std::atomic<std::uint64_t>>(0));
    }
}
LiveTools::~LiveTools(){
    {std::lock_guard lock(dispatch_->mutex);dispatch_->alive=false;dispatch_->target=nullptr;}
    ++generation_;receiving_=false;stopAll();
    for(auto& tool:tools_)try{if(tool.attachReception)tool.attachReception({});if(tool.attachReceiverControls)tool.attachReceiverControls({});if(tool.sourceChanged)tool.sourceChanged({false,{}});if(tool.widget)tool.widget->setEnabled(false);}catch(...){}
}
void LiveTools::setSession(bool receiving) {
    ++generation_;receiving_=receiving;awaitingStream_=receiving;
    for(std::size_t i=0;i<tools_.size();++i) {
        auto& tool=tools_[i];const bool linked=!tool.attachReception && tool.monitorBound;
        if(linked){*faulted_[i]=false;failures_[i].clear();}
        try {if(linked && tool.stop)tool.stop();if(tool.sourceChanged)tool.sourceChanged({receiving,{}});}
        catch(...){reportFailure(i,std::current_exception(),generation_);}
    }
}
IqBlockSubscriber LiveTools::subscriber(){return [dispatch=dispatch_](const auto& block){std::lock_guard lock(dispatch->mutex);if(dispatch->target)dispatch->target->deliver(block);};}
void LiveTools::deliver(const IqBlockView& block) {
    const auto epoch=generation_.load();if(!receiving_)return;
    if(awaitingStream_.exchange(false))QMetaObject::invokeMethod(this,[this,stream=block.stream,epoch]{
        if(epoch!=generation_ || !receiving_)return;
        for(std::size_t i=0;i<tools_.size();++i)if(tools_[i].sourceChanged)try{tools_[i].sourceChanged({true,stream});}
            catch(...){reportFailure(i,std::current_exception(),epoch);}
    },Qt::QueuedConnection);
    for(std::size_t i=0;i<tools_.size();++i)if(!tools_[i].attachReception && !*faulted_[i] && tools_[i].offer)try{tools_[i].offer(block);}
        catch(...){reportFailure(i,std::current_exception(),epoch);}
}
void LiveTools::reportFailure(std::size_t index,std::exception_ptr error,std::uint64_t epoch) const {
    if(epoch!=generation_ || faulted_[index]->exchange(true))return;
    QMetaObject::invokeMethod(const_cast<LiveTools*>(this),[this,index,error,epoch]{
        if(epoch!=generation_)return;
        try{std::rethrow_exception(error);}catch(const std::exception& e){failures_[index]=QString::fromUtf8(e.what());}catch(...){failures_[index]=QStringLiteral("unknown_exception");}
        if(tools_[index].failure)try{tools_[index].failure(error);}catch(...){}
    },Qt::QueuedConnection);
}
void LiveTools::addEventActions(QMenu& menu,const Json& event) {
    for(std::size_t i=0;i<tools_.size();++i)if(tools_[i].actions)try{addActions(menu,i,tools_[i].actions(event));}
        catch(...){reportFailure(i,std::current_exception(),generation_);}
}
void LiveTools::addRecordingActions(QMenu& menu,const ResearchRecordingContext& context) {
    for(std::size_t i=0;i<tools_.size();++i)if(tools_[i].recordingActions)try{addActions(menu,i,tools_[i].recordingActions(context));}
        catch(...){reportFailure(i,std::current_exception(),generation_);}
}
void LiveTools::addActions(QMenu& menu,std::size_t i,std::vector<LiveResearchAction> actions) {
    for(const auto& action:actions) {
        for(auto* target:action.reveal)
            if(target!=tools_[i].widget && std::none_of(tools_[i].panels.begin(),tools_[i].panels.end(),[&](const auto& panel){return panel.widget==target;}))
                throw std::invalid_argument("Research action references an undeclared panel");
    }
    for(auto& action:actions) {
        auto* item=menu.addAction(action.label);item->setObjectName(panels_[primary_panels_[i]].id+"/"+action.id);
        item->setEnabled(action.enabled);item->setToolTip(action.unavailableReason);
        QObject::connect(item,&QAction::triggered,this,[this,i,trigger=std::move(action.trigger),reveal=std::move(action.reveal)]{
            try {if(showPanel){showPanel(tools_[i].widget);for(auto* panel:reveal)if(panel!=tools_[i].widget)showPanel(panel);}if(trigger)trigger();}
            catch(...){reportFailure(i,std::current_exception(),generation_);}
        });
    }
}
void LiveTools::stopAll(){for(std::size_t i=0;i<tools_.size();++i)if(tools_[i].stop)try{tools_[i].stop();}catch(...){reportFailure(i,std::current_exception(),generation_);}}
bool LiveTools::busy() const {for(std::size_t i=0;i<tools_.size();++i)try{if(tools_[i].busy && tools_[i].busy())return true;}catch(...){reportFailure(i,std::current_exception(),generation_);return true;}return false;}
namespace {
struct DeliveryGate {std::atomic_bool active=true;std::atomic<std::uint64_t> generation=1,ready=0;std::weak_ptr<ReceptionLease> source;};
QString failureText(const QString& title,const QString& detail,const QString& language) {
    static const auto catalog=TextCatalog::resource("desktop-status");Json value={{"label_key","tool_failed"},{"arguments",{{"name",title.toStdString()},{"detail",detail.toStdString()}}}};catalog.resolve(value);return localizedText(value.at("label"),language);
}
struct UiLease final:ReceptionLease {
    std::shared_ptr<ReceptionLease> source;std::shared_ptr<DeliveryGate> gate;
    void release() noexcept override {if(gate)gate->active=false;if(source)source->release();}
    ~UiLease() override {release();}
};
}
void LiveTools::setReceptionAcquire(ReceptionAcquire acquire,std::function<void()> receiverControls) {
    const auto lifetime=dispatch_;
    const auto controls=[lifetime,receiverControls]{if(!lifetime->alive)return;if(!QCoreApplication::instance() || QThread::currentThread()!=QCoreApplication::instance()->thread())throw std::logic_error("Receiver controls require the UI thread");if(receiverControls)receiverControls();};
    for(std::size_t i=0;i<tools_.size();++i)if(tools_[i].attachReceiverControls)try{tools_[i].attachReceiverControls(receiverControls?controls:std::function<void()>{});}catch(...){reportFailure(i,std::current_exception(),generation_);}
    for(std::size_t index=0;index<tools_.size();++index)if(tools_[index].attachReception)try {tools_[index].attachReception([this,acquire,index,lifetime](const auto& demand,ReceptionConsumer consumer){
        if(!lifetime->alive)throw std::runtime_error("Reception host disconnected");
        if(!QCoreApplication::instance() || QThread::currentThread()!=QCoreApplication::instance()->thread())throw std::logic_error("Reception requests require the UI thread");
        *faulted_[index]=false;failures_[index].clear();
        auto result=std::make_shared<UiLease>();result->gate=std::make_shared<DeliveryGate>();
        const auto gate=result->gate;const auto dispatch=dispatch_;const auto waiting=configurationWaitPairs_[index];
        result->source=acquire(demand,{[dispatch,gate,index,callback=std::move(consumer.changed)](const auto& status){
            if(!gate->active)return;const auto epoch=++gate->generation;
            std::lock_guard lock(dispatch->mutex);auto* host=dispatch->target;if(!host)return;
            QMetaObject::invokeMethod(host,[host,gate,index,callback,status,epoch]{
                if(!gate->active || gate->generation!=epoch)return;
                try {if(callback)callback(status);}catch(...) {
                    gate->active=false;if(const auto source=gate->source.lock())source->release();*host->faulted_[index]=true;
                    const auto error=std::current_exception();try{std::rethrow_exception(error);}catch(const std::exception& e){host->failures_[index]=QString::fromUtf8(e.what());}catch(...){host->failures_[index]=QStringLiteral("unknown_exception");}
                    if(host->tools_[index].failure)try{host->tools_[index].failure(error);}catch(...){}
                }
                if(gate->active && status.stream)gate->ready=epoch;
            },Qt::QueuedConnection);
        },[dispatch,gate,waiting,offer=std::move(consumer.offer)](const auto& block){
            if(!dispatch->alive || !gate->active)return;
            if(gate->ready!=gate->generation){*waiting+=block.bytes.size()/bytes_per_pair(block.stream.format);return;}
            if(offer)offer(block);
        }});
        if(!result->source)throw std::invalid_argument("Reception provider returned no lease");gate->source=result->source;
        return result;
    });}catch(...){reportFailure(index,std::current_exception(),generation_);}
}
QString LiveTools::summary() const {
    QStringList result;for(std::size_t i=0;i<tools_.size();++i)try{if(!*faulted_[i] && tools_[i].summary){const auto value=tools_[i].summary();if(!value.isEmpty())result<<value;}}catch(...){reportFailure(i,std::current_exception(),generation_);}
    return result.join(QStringLiteral(" · "));
}
QString LiveTools::health() const {
    QStringList result;for(std::size_t i=0;i<tools_.size();++i)try{if(*faulted_[i])result<<failureText(panels_[primary_panels_[i]].title,failures_[i],language_);else if(tools_[i].health){const auto value=tools_[i].health();if(!value.isEmpty())result<<value;}}catch(...){reportFailure(i,std::current_exception(),generation_);}return result.join(QStringLiteral(" · "));
}
bool LiveTools::showHealth(){for(std::size_t i=0;i<tools_.size();++i)try{if(*faulted_[i] || (tools_[i].health && !tools_[i].health().isEmpty())){if(showPanel)showPanel(tools_[i].widget);return true;}}catch(...){reportFailure(i,std::current_exception(),generation_);}return false;}
Json LiveTools::diagnostics() const {
    Json result=Json::array();for(std::size_t i=0;i<tools_.size();++i) {
        const auto& panel=panels_[primary_panels_[i]];
        Json item={{"id",panel.id.toStdString()},{"available",panel.loadError.isEmpty()},{"detail",panel.loadError.toStdString()},{"faulted",faulted_[i]->load()}};
        item["configuration_wait_pairs_total"]=configurationWaitPairs_[i]->load();
        if(tools_[i].busy)try{item["busy"]=tools_[i].busy();}catch(...){item["busy_unknown"]=true;}
        if(!failures_[i].isEmpty())item["failure_detail"]=failures_[i].toUtf8().toStdString();
        if(tools_[i].snapshot)try{item["status"]=tools_[i].snapshot();}
            catch(const std::exception& error){item["snapshot_error"]=error.what();}
        result.push_back(std::move(item));
    }return result;
}
}
