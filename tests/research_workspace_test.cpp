#include "research_workspace_provider.hpp"
#include "live_tools.hpp"
#include "workspace.hpp"
#include "signalscope/backend/iq/path.hpp"
#include <QApplication>
#include <QAction>
#include <QFileInfo>
#include <QLabel>
#include <QMainWindow>
#include <QMenu>
#include <QSettings>
#include <QTemporaryDir>
#include <iostream>
using namespace signalscope;
using namespace signalscope::desktop;
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
LiveResearchProvider legacy(unsigned version,QWidget*& widget,unsigned& calls) {
    return {QString::number(version),[&widget,&calls](const QString&,QWidget* parent){LiveResearchTool tool;tool.widget=widget=new QLabel("Legacy",parent);
        tool.actions=[&calls](const Json&){return std::vector<LiveResearchAction>{{"legacy","Legacy",{},true,[&calls]{++calls;}}};};return tool;},version};
}
void multipart(const std::filesystem::path& resources) {
    QMainWindow window;window.resize(1000,700);Workspace workspace(window);
    auto state=std::make_shared<ResearchWorkspaceFixture>();QWidget* legacyWidget=nullptr;unsigned legacyCalls=0;
    LiveTools host({researchWorkspaceFixture(resources,state),legacy(1,legacyWidget,legacyCalls)},"fr-CA",&window);
    require(host.panels().size()==3,"Independent extra panel was not discovered");
    require(host.panels()[0].title==QString::fromUtf8("Espace de recherche indépendant") && host.panels()[1].title=="Independent sample inspector","Independent resources or per-key fallback failed");
    for(const auto& panel:host.panels()) {
        workspace.add(panel.id,panel.title,panel.widget,panel.preferredArea);
        if(panel.relativeTo)workspace.place(panel.widget,panel.relativeTo,panel.orientation);
        workspace.setVisible(panel.widget,false);
    }
    require(host.layouts().size()==1 && host.layouts()[0].title=="Independent research layout","An independent module layout was hidden or lost resource fallback");
    for(const auto& layout:host.layouts())workspace.addLayout(layout.id,layout.title,[&workspace,layout]{
        std::vector<WorkspaceLayoutItem> items{{layout.primary,Qt::RightDockWidgetArea}};
        for(const auto& panel:layout.panels)items.push_back({panel.widget,panel.preferredArea,panel.relativeTo,panel.orientation});
        workspace.applyLayout(items);
    });
    host.showPanel=[&](auto* widget){workspace.show(widget);};workspace.restore([](bool){});window.show();QCoreApplication::processEvents();
    const auto* mainToggle=window.findChild<QAction*>("view/"+host.panels()[0].id);
    auto* extraToggle=window.findChild<QAction*>("view/"+host.panels()[1].id);
    require(mainToggle && extraToggle,"An independently declared extra panel did not enter View");
    QTemporaryDir directory;
    const auto metadata=QFileInfo(directory.filePath(QString::fromUtf8("信号 📡.sigmf-meta"))).filesystemFilePath();
    const ResearchRecordingContext input{metadata,ResearchSampleRange{(1ULL<<54)+1,37},{{"modulation_id","independent.unknown"},{"features",{{"future_field",{{"value",17},{"unit","Hz"}}}}}}};
    QMenu recording;host.addRecordingActions(recording,input);
    require(recording.actions().size()==1 && recording.actions()[0]->text()=="Inspect recorded samples","Unknown recorded event was filtered or lacked resource fallback");
    recording.actions()[0]->trigger();QCoreApplication::processEvents();
    require(workspace.visible(host.panels()[0].widget) && workspace.visible(host.panels()[1].widget) && mainToggle->isChecked() && extraToggle->isChecked(),"A generic recording action failed to reveal its declared panes");
    require(state->recording.metadata==metadata && state->recording.range->first==input.range->first && state->recording.event==input.event,"Native path, 64-bit range or unknown evidence changed in the host");
    host.setSession(true);host.setSession(false);require(state->busy,"Monitor changes canceled an independently owned recorded analysis");
    require(host.diagnostics().size()==2 && host.diagnostics()[0].at("status").at("independent_opens")==1,"Extra panels corrupted the tool-to-diagnostic mapping");
    QMenu events;host.addEventActions(events,input.event);require(events.actions().size()==1,"Legacy actions were routed through an extra panel");events.actions()[0]->trigger();
    require(legacyCalls==1 && workspace.visible(legacyWidget),"A provider after a multipart tool opened the wrong primary pane");
    auto* preset=window.findChild<QAction*>("workspace-layout/"+host.layouts()[0].id);require(preset,"An independent layout was not registered in View");preset->trigger();QCoreApplication::processEvents();
    require(workspace.visible(host.panels()[0].widget) && workspace.visible(host.panels()[1].widget) && !workspace.visible(legacyWidget),"An explicit provider layout did not select its declared panels");
    window.resize(640,480);QCoreApplication::processEvents();require(!workspace.visible(legacyWidget),"Resizing re-applied an unrelated layout");
    extraToggle->trigger();require(state->busy && host.busy(),"Closing a secondary pane stopped the research operation");
    state->rejectActions=true;QMenu failures;host.addRecordingActions(failures,input);QCoreApplication::processEvents();
    require(state->failures==1 && host.diagnostics()[0].at("faulted")==true && host.diagnostics()[1].at("faulted")==false,"Recording callback failures were not isolated by tool ownership");
    host.stopAll();require(!host.busy(),"Generic stop did not stop multipart research");
}
void compatibility() {
    QWidget owner;QWidget *one=nullptr,*two=nullptr;unsigned a=0,b=0;
    LiveTools host({legacy(1,one,a),legacy(2,two,b)},"en",&owner);
    QMenu menu;host.addEventActions(menu,Json::object());for(auto* action:menu.actions())action->trigger();
    require(a==1 && b==1 && host.panels().size()==2,"v3 host broke v1 or v2 action contracts");
    auto invalid=legacy(2,two,b);invalid.create=[](const QString&,QWidget* parent){LiveResearchTool t;t.widget=new QLabel("bad",parent);t.panels.push_back({"unsupported","Unsupported",new QLabel(parent)});return t;};
    LiveTools rejected({invalid},"en",&owner);require(rejected.diagnostics()[0].at("available")==false,"New capability masqueraded as protocol v2");
}
}
int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setOrganizationName("AMC Tests");app.setApplicationName("Research workspace extension");
    QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());
    try {require(argc==2,"Expected independent provider resources");multipart(QFileInfo(app.arguments()[1]).filesystemFilePath());compatibility();std::cout<<"Independent multi-panel provider, View, recording inputs, Unicode paths, large sample ranges, unknown events, failure isolation and v1/v2 compatibility passed.\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
