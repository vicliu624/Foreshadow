#include "plugins/desktop/common/parameter_editor.hpp"
#include "signalscope/backend/dsp/reference_model.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "test_process.hpp"
#include <QApplication>
#include <QDoubleSpinBox>
using namespace signalscope;
using namespace signalscope::desktop;
int main(int argc,char** argv){QApplication app(argc,argv);app.setProperty("signalscope-language","en");try{
    const auto provider=reference_model_provider("lora-symbol-chirp");const ReferenceModelContext context{2000,{0,128},0,127./2000};ParameterEditor editor(nullptr,"reference-");auto catalog=TextCatalog::resource(provider.resource_namespace);
    const auto describe=[&](const Json& pending){auto descriptors=reference_model_parameters(provider,context,pending);catalog.resolve(descriptors);return descriptors;};
    editor.populate(describe(Json::object()),Json::object());editor.changed=[&]{editor.updateDescriptors(describe(editor.values()));};
    auto* sf=editor.findChild<QDoubleSpinBox*>("reference-sf");auto* symbol=editor.findChild<QDoubleSpinBox*>("reference-symbol");amc_test::require(sf && symbol,"Generic model parameters were not rendered");
    sf->setValue(12);symbol->setValue(600);amc_test::require(symbol->value()==600 && editor.problems().empty(),"Dependent SF maximum did not expand a generic numeric field");
    sf->setValue(5);amc_test::require(symbol->value()==600 && !editor.problems().empty(),"Reducing SF silently clamped an invalid pending symbol");
    symbol->setValue(20);amc_test::require(editor.problems().empty(),"A corrected whole configuration did not recover");
    editor.populate(describe({{"sf",5},{"symbol",600}}),{{"sf",5},{"symbol",600}});amc_test::require(editor.values().at("symbol")==600 && !editor.problems().empty(),"Invalid saved parameters were silently rewritten");
    ReferenceModelProvider independent;independent.id="independent.dynamic-bounds";independent.resource_namespace="independent.resources";independent.label_key="label";independent.protocol_version=2;independent.prepare=[](const auto&,const Json&){return PreparedReferenceModel{};};
    independent.describe=[](const auto&,const Json& pending){const auto cap=pending.value("limit",100);return Json::array({{{"key","value"},{"type","number"},{"extension",true},{"label","Independent value"},{"default",0},{"minimum",0},{"maximum",cap}}});};register_reference_model(independent);
    editor.changed={};editor.populate(reference_model_parameters(reference_model_provider(independent.id),context,{{"limit",900}}),{{"value",700}});amc_test::require(editor.values().at("value")==700 && editor.problems().empty(),"An independent provider needed a consumer branch for numeric bounds");
    std::cout<<"Whole-configuration model descriptors, numeric range updates and invalid saved values passed\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
