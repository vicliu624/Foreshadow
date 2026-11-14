#include "research_workspace_provider.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/desktop/localization.hpp"
#include <QLabel>
using namespace signalscope;
using namespace signalscope::desktop;
LiveResearchProvider researchWorkspaceFixture(const std::filesystem::path& resources,std::shared_ptr<ResearchWorkspaceFixture> state) {
    return {"independent.workspace",[resources,state](const QString& language,QWidget* parent){
        auto catalog=std::make_shared<TextCatalog>();catalog->load(resources,{"i18n"});
        const auto text=[catalog,language](const char* key){Json value={{"label_key",key}};catalog->resolve(value);return localizedText(value.at("label"),language);};
        auto* main=new QLabel(text("title"),parent);main->setWindowTitle(text("title"));
        auto* detail=new QLabel(text("detail"),parent);detail->setWindowTitle(text("detail"));
        LiveResearchTool tool;tool.widget=main;tool.monitorBound=false;tool.workspaceLayoutTitle=text("layout");
        tool.panels.push_back({"independent.detail",text("detail"),detail,false,Qt::RightDockWidgetArea,main,Qt::Horizontal});
        tool.recordingActions=[state,main,detail,text](const ResearchRecordingContext& input){
            if(state->rejectActions)throw std::runtime_error("independent_recording_failure");
            return std::vector<LiveResearchAction>{{"open-recording",text("inspect"),{},true,[state,input]{state->recording=input;++state->opened;state->busy=true;},{main,detail}}};
        };
        tool.stop=[state]{state->busy=false;};tool.busy=[state]{return state->busy;};
        tool.failure=[state](std::exception_ptr){++state->failures;state->busy=false;};
        tool.snapshot=[state]{return Json{{"independent_opens",state->opened}};};return tool;
    },3};
}
