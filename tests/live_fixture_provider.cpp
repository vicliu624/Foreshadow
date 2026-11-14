#include "live_fixture_provider.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QLabel>
using namespace signalscope;
using namespace signalscope::desktop;
LiveResearchProvider liveFixtureProvider(const std::filesystem::path& root,const QString& id,std::shared_ptr<LiveFixtureState> state) {
    return {id,[root,state](const QString& language,QWidget* parent){
        auto catalog=std::make_shared<TextCatalog>();catalog->load(root,{"i18n"});
        const auto text=[catalog,language](const char* key){Json data={{"label_key",key}};catalog->resolve(data);return localizedText(data.at("label"),language);};
        auto* widget=new QLabel(text("stopped"),parent);widget->setWindowTitle(text("title"));
        return LiveResearchTool{widget,
            [state,widget,text](const LiveResearchContext& context){state->context=context;widget->setText(text(context.receiving?"receiving":"stopped"));},
            [state](const IqBlockView& block){
                if(state->reject)throw std::runtime_error("independent_fixture_failure");
                std::lock_guard lock(state->mutex);state->first=block.first_sample;state->retained.assign(block.bytes.begin(),block.bytes.end());
                state->pairs+=block.bytes.size()/bytes_per_pair(block.stream.format);
            },
            [state,widget,text](const Json& event)->std::vector<LiveResearchAction>{
                if(event.value("modulation_id",std::string())!="independent.future.signal")return {};
                return {{"inspect",text("inspect"),{},true,[state,widget,text]{state->busy=true;widget->setText(text("inspecting"));}}};
            },
            [state]{++state->stopped;state->busy=false;},[state]{return state->busy;},
            [state,widget,text](std::exception_ptr){++state->failures;state->busy=false;widget->setText(text("failed"));},false,
            [state]{return Json{{"independent_pairs",state->pairs.load()}};}};
    }};
}
LiveResearchProvider independentReceptionProvider(const std::filesystem::path& root,std::shared_ptr<LiveFixtureState> state) {
    return {"independent.reception",[root,state](const QString& language,QWidget* parent){
        auto catalog=std::make_shared<TextCatalog>();catalog->load(root,{"i18n"});
        const auto text=[catalog,language](const char* key){Json data={{"label_key",key}};catalog->resolve(data);return localizedText(data.at("label"),language);};
        auto* widget=new QLabel(text("stopped"),parent);widget->setWindowTitle(text("title"));
        LiveResearchTool tool;tool.widget=widget;
        tool.sourceChanged=[state](const auto& context){if(!state->lease)state->context=context;};
        tool.attachReception=[state](auto acquire){state->acquire=std::move(acquire);};
        tool.actions=[state,text](const Json&){return std::vector<LiveResearchAction>{{"request",text("inspect"),{},true,[state]{
            state->lease=state->acquire({433920000,400000,500000,2000000,{}},{[state](const auto& status){if(state->reject)throw std::runtime_error("independent_status_failure");state->context={status.stream.has_value(),status.stream};},[state](const auto& block){state->pairs+=block.bytes.size()/bytes_per_pair(block.stream.format);}});
        }}};};
        tool.offer=[state](const auto&){throw std::runtime_error("v2 consumer was fed twice by the monitor subscriber");};
        tool.stop=[state]{state->lease.reset();state->context={};++state->stopped;};
        tool.failure=[state](std::exception_ptr){++state->failures;state->lease.reset();state->context={};};
        tool.busy=[state]{return bool(state->lease);};tool.summary=[text]{return text("title");};return tool;
    }};
}
