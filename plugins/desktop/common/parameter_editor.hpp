#pragma once
#include <QWidget>
#include <functional>
#include "signalscope/backend/iq/iq.hpp"
class QFormLayout;
class QLabel;
namespace signalscope::desktop {
// Generic controls for plugin-defined fields; no receiver IDs or driver headers.
class ParameterEditor final:public QWidget {
public:
    explicit ParameterEditor(QWidget* parent=nullptr,QString prefix="plugin-");
    void populate(const Json& descriptors,const Json& saved);
    void updateDescriptors(const Json& descriptors);
    Json values() const;
    void setValues(const Json&);
    void showIssues(const std::vector<std::pair<std::string,QString>>&);
    std::vector<std::pair<std::string,QString>> problems() const;
    std::function<void()> changed;
private:
    struct Row {std::string key,type;QWidget* field;QLabel* label;QLabel* issue;};
    QString prefix_;
    QFormLayout* layout_;
    std::vector<Row> rows_;
    Json descriptors_=Json::array();
};
QString parameterLabel(const Json& descriptor);
}
