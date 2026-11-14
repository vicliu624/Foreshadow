#pragma once
#include "plugins/desktop/research/research_plugin.hpp"
#include <QList>
#include <QWidget>
#include <memory>
#include <optional>
class AnalysisPanel:public QWidget {
public:
    AnalysisPanel(const QString& pluginDirectory,const QString& language,QWidget* parent=nullptr);
    ~AnalysisPanel() override;
    void analyze(const QString& metadata);
    bool analyzeRange(const QString& metadata,const QString& pluginId,signalscope::desktop::ResearchSampleRange range,
                      std::optional<double> frequency={});
    void setThreshold(double value);
    bool hasResult() const;
    bool restorePreviousArtifact();
    bool exportResultToDirectory(const QString& directory);
    bool selectPlot(const QString& id);
    void setPluginEnabled(const QString& id,bool enabled);
    void forgetPlugin(const QString& id);
    bool isAnalyzing() const;
    QList<signalscope::desktop::ResearchPlugin> plugins() const;
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    class Impl;std::unique_ptr<Impl> impl_;
};
