#include "plugins/desktop/research/external_research.hpp"
#include "signalscope/desktop/localization.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include "signalscope/backend/distribution/distribution.hpp"
#include "signalscope/backend/iq/path.hpp"

#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringDecoder>
#include <QTemporaryDir>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

namespace signalscope::desktop {
namespace {
constexpr auto artifactLimit=32*1024*1024;

bool validPluginId(const QString& id) {
    static const QRegularExpression pattern(R"(\Aexternal\.[a-z0-9][a-z0-9._-]{0,54}\z)");
    return pattern.match(id).hasMatch();
}

std::optional<ResearchArtifactFormat> readArtifactFormat(const QJsonValue& value) {
    static const QRegularExpression pattern(R"(\A[a-z][a-z0-9_-]*(\.[a-z0-9][a-z0-9_-]*)+\z)");
    const auto format=value.toObject();
    const auto id=format.value("format").toString();
    const auto version=format.value("version").toInt();
    if(id.size()>128 || !pattern.match(id).hasMatch() || version<=0)return {};
    return ResearchArtifactFormat{id,version};
}

ExternalResearchPlugin readPlugin(const QString& path,const QString& language,QByteArray* contents=nullptr) {
    static const QRegularExpression executablePattern(R"(\A[A-Za-z0-9][A-Za-z0-9._-]{0,127}\z)");
    const QFileInfo manifestFile(path);
    QFile file(path);
    if(!manifestFile.isFile() || manifestFile.isSymLink() || !file.open(QIODevice::ReadOnly))return {};
    const auto bytes=file.read(16385);
    if(bytes.size()>16384)return {};
    const auto manifest=QJsonDocument::fromJson(bytes).object();
    const auto id=manifest.value("id").toString();
    const auto name=manifest.value("executable").toString();
    const auto output=manifest.value("output");
    std::variant<ResearchOutput,ResearchArtifactFormat> outputFormat=ResearchOutput::image;
    if(output.isObject()) {
        const auto format=readArtifactFormat(output);
        if(!format)return {};
        outputFormat=*format;
    } else if(!output.isUndefined() && output!="png" && output!="text" && output!="wav")return {};
    else if(output=="text")outputFormat=ResearchOutput::text;
    else if(output=="wav")outputFormat=ResearchOutput::audio;
    const auto sampleRange=manifest.value("sampleRange");
    if(!sampleRange.isUndefined() && !sampleRange.isBool())return {};
    const auto api=manifest.value("api").toInt();
    const auto input=readArtifactFormat(manifest.value("input"));
    if((api!=1 && api!=2) || (api==1 && !manifest.value("input").isUndefined()) ||
       (api==2 && (!input || sampleRange.toBool())) || !validPluginId(id) ||
       !executablePattern.match(name).hasMatch())return {};
    const QFileInfo executable(manifestFile.dir().absoluteFilePath(name));
    if(!executable.isFile() || executable.isSymLink() || !executable.isExecutable())return {};
    QStringList resources;const auto base=manifestFile.dir();
    const auto addResource=[&](const QString& asset) {
        const auto cleaned=QDir::cleanPath(asset);
        if(asset.isEmpty() || QDir::isAbsolutePath(asset) || cleaned!=asset || cleaned.startsWith("../") || asset=="plugin.json" || asset==name)return false;
        const QFileInfo item(base.filePath(asset));
        if(!item.isFile() || item.isSymLink() || item.size()>16*1024*1024 || !item.canonicalFilePath().startsWith(base.canonicalPath()+'/'))return false;
        if(!resources.contains(asset))resources.push_back(asset);return true;
    };
    const auto assets=manifest.value("resources");
    if(!assets.isUndefined() && (!assets.isArray() || assets.toArray().size()>32))return {};
    for(const auto& value:assets.toArray())if(!value.isString() || !addResource(value.toString()))return {};
    std::vector<std::string> catalogDirectories;
    const auto catalogs=manifest.value("catalogs");
    if(!catalogs.isUndefined() && (!catalogs.isArray() || catalogs.toArray().size()>16))return {};
    for(const auto& value:catalogs.toArray()) {
        if(!value.isString())return {};const auto relative=value.toString();
        const QFileInfo folder(base.filePath(relative));
        if(relative.isEmpty() || QDir::isAbsolutePath(relative) || QDir::cleanPath(relative)!=relative || relative.startsWith("../") || !folder.isDir() || folder.isSymLink() || !folder.canonicalFilePath().startsWith(base.canonicalPath()+'/'))return {};
        const QDir directory(folder.absoluteFilePath());const auto files=directory.entryList({"*.json"},QDir::Files);
        if(files.size()>128)return {};
        for(const auto& file:files)if(!addResource(relative+'/'+file))return {};
        catalogDirectories.push_back(relative.toStdString());
    }
    auto parameters=nlohmann::json::array();
    const auto fields=manifest.value("parameters");
    if(!fields.isUndefined() && (!fields.isArray() || fields.toArray().size()>64))return {};
    QSet<QString> keys;
    for(const auto& value:fields.toArray()) {
        if(!value.isObject())return {};const auto field=value.toObject();const auto key=field.value("key").toString();
        const auto type=field.value("type").toString();
        if(!executablePattern.match(key).hasMatch() || keys.contains(key) || !QStringList{"boolean","integer","number","enum","string"}.contains(type) || (type=="enum" && field.value("choices").toArray().isEmpty()))return {};
        keys.insert(key);auto descriptor=nlohmann::json::parse(QJsonDocument(field).toJson().toStdString());descriptor["extension"]=true;parameters.push_back(std::move(descriptor));
    }
    try {TextCatalog texts;texts.load(base.absolutePath().toStdString(),catalogDirectories);texts.resolve(parameters);}
    catch(const std::exception&){return {};}
    const auto titles=manifest.value("title").toObject();
    const auto fallback=titles.value("en").toString().simplified();
    if(fallback.isEmpty())return {};
    for(auto entry=titles.constBegin();entry!=titles.constEnd();++entry) {
        if(!entry.value().isString() || entry.value().toString().simplified().size()>80)return {};
    }
    const auto title=localizedText(nlohmann::json::parse(QJsonDocument(titles).toJson().toStdString()),language,fallback).simplified();
    if(contents)*contents=bytes;
    return {{id,title.isEmpty()?fallback:title,true},executable.absoluteFilePath(),language,std::move(outputFormat),
            sampleRange.toBool(),input,resources,parameters};
}

}

ExternalResearchPlugin readResearchPlugin(const QString& manifest,const QString& language){return readPlugin(manifest,language);}
QList<ExternalResearchPlugin> discoverResearchPlugins(const QString& directory,const QString& language) {
    if(directory.isEmpty())return {};
    QList<ExternalResearchPlugin> plugins;
    QSet<QString> ids;
    const auto disabled=distribution::runtimeDisabledComponents("research");
    const auto add=[&](const QString& path,const QString& expectedId=QString()){
        auto plugin=readPlugin(path,language);
        if(plugin.plugin.id.isEmpty() || (!expectedId.isEmpty() && expectedId!=plugin.plugin.id) ||
           ids.contains(plugin.plugin.id) || disabled.contains(plugin.plugin.id.toStdString()))return;
        ids.insert(plugin.plugin.id);
        plugin.plugin.managed=!expectedId.isEmpty();
        plugins.push_back(std::move(plugin));
    };
    for(const auto& component:distribution::runtimeComponents("research")) {
        const auto size=plugins.size();add(QString::fromStdString(path_to_utf8(component.path)),QString::fromStdString(component.id));
        if(plugins.size()>size){plugins.back().plugin.toolboxId=QString::fromStdString(component.package_id);plugins.back().plugin.managed=false;}
    }
    const auto installed=QDir(directory).absoluteFilePath("installed");
    if(!QFileInfo(installed).isSymLink()) {
        QDirIterator packages(installed,QDir::Dirs|QDir::NoDotAndDotDot|QDir::NoSymLinks);
        while(packages.hasNext()) {
            const QDir package(packages.next());
            add(package.filePath("plugin.json"),package.dirName());
        }
    }
    QDirIterator manifests(directory,{"*.json"},QDir::Files|QDir::NoSymLinks);
    for(int scanned=0;scanned<64 && manifests.hasNext();++scanned)add(manifests.next());
    std::sort(plugins.begin(),plugins.end(),[](const auto& a,const auto& b){return a.plugin.id<b.plugin.id;});
    return plugins;
}

ResearchInstallResult installResearchPlugin(const QString& manifestPath,const QString& directory) {
    using Result=ResearchInstallResult;
    if(directory.isEmpty())return Result::storage_error;
    QByteArray contents;
    const auto plugin=readPlugin(manifestPath,"en",&contents);
    const QFileInfo binary(plugin.executable);
    if(plugin.plugin.id.isEmpty() || binary.size()>64*1024*1024 || binary.fileName().compare("plugin.json",Qt::CaseInsensitive)==0)
        return Result::invalid_package;
    for(const auto& existing:discoverResearchPlugins(directory,"en"))
        if(existing.plugin.id==plugin.plugin.id)return Result::duplicate_id;
    const QDir installed(QDir(directory).absoluteFilePath("installed"));
    if(QFileInfo(installed.absolutePath()).isSymLink() || !QDir().mkpath(installed.absolutePath()))
        return Result::storage_error;
    const auto id=plugin.plugin.id;
    if(!installed.mkdir(id))
        return QFileInfo::exists(installed.filePath(id))?Result::duplicate_id:Result::storage_error;
    const QDir package(installed.filePath(id));
    const auto executable=package.filePath(binary.fileName());
    QStringList copied;
    bool assetsReady=true;
    for(const auto& asset:plugin.resources) {
        const auto destination=package.filePath(asset);
        if(!QDir().mkpath(QFileInfo(destination).path()) || !QFile::copy(QFileInfo(manifestPath).dir().filePath(asset),destination)){assetsReady=false;break;}
        copied.push_back(destination);
    }
    if(assetsReady && QFile::copy(plugin.executable,executable) && QFileInfo(executable).isExecutable()) {
        QSaveFile manifest(package.filePath("plugin.json"));
        if(manifest.open(QIODevice::WriteOnly) && manifest.write(contents)==contents.size() && manifest.commit())
            return Result::installed;
    }
    QFile::remove(executable);
    for(const auto& file:copied)QFile::remove(file);
    for(const auto& file:copied) {
        auto directory=QFileInfo(file).dir();
        while(directory.absolutePath()!=package.absolutePath() && directory.absolutePath().startsWith(package.absolutePath()+'/')) {
            const auto path=directory.absolutePath();directory.cdUp();if(!directory.rmdir(QFileInfo(path).fileName()))break;
        }
    }
    installed.rmdir(id);
    return Result::storage_error;
}

QString removeResearchPlugin(const QString& id,const QString& directory) {
    if(directory.isEmpty() || !validPluginId(id))return {};
    const QDir root(directory);
    const QFileInfo installed(root.absoluteFilePath("installed"));
    const QFileInfo package(QDir(installed.absoluteFilePath()).absoluteFilePath(id));
    if(!installed.isDir() || installed.isSymLink() || !package.isDir() || package.isSymLink() ||
       readPlugin(QDir(package.absoluteFilePath()).filePath("plugin.json"),"en").plugin.id!=id)return {};
    const auto archive=root.absoluteFilePath("removed");
    if(QFileInfo(archive).isSymLink() || !QDir().mkpath(archive))return {};
    const auto destination=QDir(archive).absoluteFilePath(id+'-'+QUuid::createUuid().toString(QUuid::WithoutBraces));
    return QDir().rename(package.absoluteFilePath(),destination)?destination:QString();
}

ResearchResult ExternalResearchPlugin::render(const QString& metadata,std::stop_token stop,
                                              std::optional<ResearchSampleRange> range,const nlohmann::json& settings) const {
    if(input)throw std::invalid_argument("This plugin requires an artifact input");
    if(stop.stop_requested())return {};
    QTemporaryDir scratch;
    if(!scratch.isValid())throw std::runtime_error("Could not create a research plugin working directory");
    return run(scratch.path(),QFileInfo(metadata).absoluteFilePath(),stop,range,settings);
}

ResearchResult ExternalResearchPlugin::render(const ResearchArtifact& artifact,std::stop_token stop,const nlohmann::json& settings) const {
    if(!accepts(artifact.format) || artifact.bytes.size()>artifactLimit)
        throw std::invalid_argument("Research artifact does not match the plugin input format or size limit");
    if(stop.stop_requested())return {};
    QTemporaryDir scratch;
    if(!scratch.isValid())throw std::runtime_error("Could not create a research plugin working directory");
    const auto inputPath=scratch.filePath("input.bin");
    QSaveFile inputFile(inputPath);
    if(!inputFile.open(QIODevice::WriteOnly) || inputFile.write(artifact.bytes)!=artifact.bytes.size() || !inputFile.commit())
        throw std::runtime_error("Could not prepare the research artifact input");
    return run(scratch.path(),inputPath,stop,{},settings);
}

ResearchResult ExternalResearchPlugin::run(const QString& workingDirectory,const QString& inputPath,
                                          std::stop_token stop,std::optional<ResearchSampleRange> range,const nlohmann::json& settings) const {
    if(stop.stop_requested())return {};
    const auto* artifactFormat=std::get_if<ResearchArtifactFormat>(&output);
    const auto* legacyOutput=std::get_if<ResearchOutput>(&output);
    const bool text=legacyOutput && *legacyOutput==ResearchOutput::text;
    const bool audio=legacyOutput && *legacyOutput==ResearchOutput::audio;
    const auto outputPath=QDir(workingDirectory).filePath(artifactFormat?"artifact.bin":text?"report.txt":audio?"audio.wav":"plot.png");
    QProcess process;
    process.setWorkingDirectory(workingDirectory);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    QStringList args{"--input",inputPath,"--output",outputPath,"--language",language};
    if(supportsRange && range)
        args.append({"--first-sample",QString::number(qulonglong(range->first)),"--samples",QString::number(qulonglong(range->count))});
    if(!parameterDescriptions.empty()) {
        nlohmann::json values=nlohmann::json::object();
        for(const auto& d:parameterDescriptions)if(d.contains("default"))values[d.at("key").get<std::string>()]=d.at("default");
        values.update(settings);const auto data=QByteArray::fromStdString(values.dump());
        const auto path=QDir(workingDirectory).filePath("settings.json");QSaveFile settingsFile(path);
        if(!settingsFile.open(QIODevice::WriteOnly) || settingsFile.write(data)!=data.size() || !settingsFile.commit())throw std::runtime_error("Could not prepare research plugin settings");
        args.append({"--settings-file",path});
    }
    process.start(executable,args);
    if(!process.waitForStarted(3000))throw std::runtime_error("Could not start the research plugin");
    QElapsedTimer elapsed;
    elapsed.start();
    while(process.state()!=QProcess::NotRunning && !process.waitForFinished(100)) {
        if(stop.stop_requested() || elapsed.elapsed()>=30000) {
            process.kill();
            process.waitForFinished(1000);
            if(stop.stop_requested())return {};
            throw std::runtime_error("The research plugin timed out");
        }
    }
    if(stop.stop_requested())return {};
    if(process.exitStatus()!=QProcess::NormalExit || process.exitCode()!=0)
        throw std::runtime_error("Research plugin failed with exit code "+std::to_string(process.exitCode()));
    const QFileInfo resultFile(outputPath);
    const auto limit=text?1024*1024:artifactLimit;
    if(!resultFile.isFile() || resultFile.isSymLink() || resultFile.size()>limit)
        throw std::runtime_error("Research plugin output is missing or exceeds the file size limit");
    if(artifactFormat || text || audio) {
        QFile file(outputPath);
        if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Could not read the research result");
        auto bytes=file.read(limit+1);
        if(file.error()!=QFileDevice::NoError || bytes.size()>limit)
            throw std::runtime_error("Could not read the research result within the file size limit");
        if(artifactFormat)return ResearchArtifact{*artifactFormat,std::move(bytes)};
        if(audio)return readResearchAudio(std::move(bytes));
        QStringDecoder decode(QStringDecoder::Utf8,QStringConverter::Flag::Stateless);
        QString report=decode(bytes);
        if(bytes.isEmpty() || decode.hasError() || report.isEmpty())
            throw std::runtime_error("Research report must contain valid UTF-8 text");
        return report;
    }
    QImageReader reader(outputPath,"png");
    const auto size=reader.size();
    if(!size.isValid() || size.width()>4096 || size.height()>4096)
        throw std::runtime_error("Research plugin output is invalid or exceeds the image size limit");
    const auto image=reader.read();
    if(image.isNull())throw std::runtime_error("Research plugin did not produce a valid PNG");
    return image;
}
}
