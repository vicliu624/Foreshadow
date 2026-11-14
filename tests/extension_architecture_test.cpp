#include "test_process.hpp"
#include <QCoreApplication>
#include <QDirIterator>
#include <QRegularExpression>
#include <nlohmann/json.hpp>
#include <set>
using Json=nlohmann::json;
using amc_test::require;
namespace {
std::set<std::string> placeholders(const std::string& message) {
    const QRegularExpression pattern(QStringLiteral("\\{([A-Za-z_][A-Za-z_0-9]*)\\}"));
    auto matches=pattern.globalMatch(QString::fromStdString(message));std::set<std::string> result;
    while(matches.hasNext())result.insert(matches.next().captured(1).toStdString());return result;
}
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    try {
        require(argc==2,"Expected source root");const QDir root(app.arguments()[1]);
        const auto policy=Json::parse(amc_test::read(root.filePath("specs/extension-contracts.json")));
        require(policy.at("version")==1 && QFileInfo::exists(root.filePath(QString::fromStdString(policy.at("normative_spec")))),"Extension specification is missing");
        QStringList violations;int checked=0;
        for(const auto& rule:policy.at("rules")) {
            const QRegularExpression expression(QString::fromStdString(rule.at("pattern")));
            require(expression.isValid(),"Invalid architecture constraint expression");
            for(const auto& target:rule.at("paths")) {
                const auto path=root.filePath(QString::fromStdString(target));QStringList files;
                if(QFileInfo(path).isDir()) {
                    QDirIterator iterator(path,{"*.cpp","*.hpp","*.h"},QDir::Files,QDirIterator::Subdirectories);
                    while(iterator.hasNext())files.push_back(iterator.next());
                }else {require(QFileInfo::exists(path),"Architecture constraint points to a missing consumer");files.push_back(path);}
                for(const auto& file:files) {
                    const auto content=QString::fromUtf8(amc_test::read(file));++checked;
                    const auto match=expression.match(content);
                    if(match.hasMatch())violations<<root.relativeFilePath(file)+':'+QString::number(content.left(match.capturedStart()).count(u'\n')+1)+" ["+QString::fromStdString(rule.at("id"))+"] "+QString::fromStdString(rule.at("reason"));
                }
            }
        }
        for(const auto& directory:policy.at("catalog_roots")) {
            const QDir catalogs(root.filePath(QString::fromStdString(directory)));
            const auto english=Json::parse(amc_test::read(catalogs.filePath("en.json")));
            for(const auto& file:catalogs.entryList({"*.json"},QDir::Files)) {
                const auto translated=Json::parse(amc_test::read(catalogs.filePath(file)));
                require(translated.is_object(),"Language catalog must be an object");
                for(const auto& [key,value]:translated.items()) {
                    if(!value.is_string() || !english.contains(key) || placeholders(value)!=placeholders(english.at(key)))
                        violations<<catalogs.filePath(file)+" [catalog-key-or-placeholder] "+QString::fromStdString(key);
                }
            }
        }
        {
            const auto legacy=policy.at("frozen_event_compatibility").get<std::vector<std::string>>();
            const auto source=QString::fromUtf8(amc_test::read(root.filePath("src/frontend/desktop/event_presentation.cpp")));
            const QRegularExpression selector(QStringLiteral("level[ \\t]*==[ \\t]*\"([^\"]+)\""));
            auto matches=selector.globalMatch(source);
            while(matches.hasNext()) {
                const auto id=matches.next().captured(1).toStdString();
                if(std::find(legacy.begin(),legacy.end(),id)==legacy.end())violations<<"event_presentation.cpp [new-event-id-routing] "+QString::fromStdString(id);
            }
        }
        if(!violations.empty()){std::cerr<<violations.join(u'\n').toUtf8().constData()<<'\n';return 1;}
        std::cout<<"Extension boundaries and resource contracts passed ("<<checked<<" consumer checks).\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
