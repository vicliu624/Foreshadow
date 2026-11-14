#pragma once

#include "plugins/desktop/research/research_plugin.hpp"
#include "plugins/desktop/research/research_audio.hpp"
#include <QImage>
#include <QList>
#include <QStringList>
#include <optional>
#include <stop_token>
#include <variant>
#include <nlohmann/json.hpp>

namespace signalscope::desktop {
struct ResearchArtifactFormat {
    QString id;
    int version;
    bool operator==(const ResearchArtifactFormat&) const=default;
};
struct ResearchArtifact {
    ResearchArtifactFormat format;
    QByteArray bytes;
};
using ResearchResult=std::variant<QImage,QString,ResearchAudio,ResearchArtifact>;
enum class ResearchOutput { image,text,audio };
struct ExternalResearchPlugin {
    ResearchPlugin plugin;
    QString executable;
    QString language;
    std::variant<ResearchOutput,ResearchArtifactFormat> output=ResearchOutput::image;
    bool supportsRange=false;
    std::optional<ResearchArtifactFormat> input;
    QStringList resources={};
    nlohmann::json parameterDescriptions=nlohmann::json::array();

    bool accepts(const ResearchArtifactFormat& format) const {
        return input && *input==format;
    }

    ResearchResult render(const QString& metadata,std::stop_token stop,
                          std::optional<ResearchSampleRange> range={},const nlohmann::json& settings=nlohmann::json::object()) const;
    ResearchResult render(const ResearchArtifact& artifact,std::stop_token stop,const nlohmann::json& settings=nlohmann::json::object()) const;

private:
    ResearchResult run(const QString& workingDirectory,const QString& inputPath,std::stop_token stop,
                       std::optional<ResearchSampleRange> range,const nlohmann::json& settings) const;
};

QList<ExternalResearchPlugin> discoverResearchPlugins(const QString& directory,const QString& language);
ExternalResearchPlugin readResearchPlugin(const QString& manifest,const QString& language);
enum class ResearchInstallResult { installed, invalid_package, duplicate_id, storage_error };
ResearchInstallResult installResearchPlugin(const QString& manifestPath,const QString& directory);
QString removeResearchPlugin(const QString& id,const QString& directory);
}
