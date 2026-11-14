#pragma once
#include "recorded_worker.hpp"
#include "plugins/desktop/common/parameter_editor.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTableView>
#include <QTabWidget>
#include <QTreeWidget>

namespace signalscope::desktop {
class RecoveryTableModel;
class RecoveryComplexView;
class WaveformRecoveryPanel final:public QWidget {
public:
    WaveformRecoveryPanel(const QString&,QWidget* parent=nullptr);
    ~WaveformRecoveryPanel() override;
    QWidget* controls() const {return controls_;}
    void setAvailable(bool);
    void setInputs(std::shared_ptr<const WaveformAnalysis>,std::shared_ptr<const WaveformAnalysis>);
    void setSelection(SignalSampleRange raw,SignalSampleRange processed);
    std::optional<WaveformRecoveryRequest> draft();
    static std::optional<WaveformRecoveryRequest> readConfiguration(const Json&);
    void restoreDraft(const Json&);
    void restoreView(const Json&);
    void setResult(std::shared_ptr<const RecoveryResult>,std::optional<WaveformRecoveryRequest>);
    const std::shared_ptr<const RecoveryResult>& result() const {return result_;}
    const std::optional<WaveformRecoveryRequest>& appliedRequest() const {return applied_;}
    std::optional<RecoveryLink> selectedBytes() const;
    QString failureText(std::exception_ptr,const std::string& fallback);
    Json configuration() const;
    Json snapshot() const;
    std::function<void()> apply;
    std::function<void(bool binary)> exportResult;
    std::function<void(std::vector<SignalSampleRange>,std::shared_ptr<const WaveformAnalysis>,bool original)> locate;
private:
    QString text(const std::string&,const Json& args=Json::object()) const;
    QString owned(const std::string&) const;
    Json descriptions(const RecoveryProvider&,const Json&) const;
    RecoveryContext context() const;
    void populate(const Json& values=Json::object());
    void refreshDescriptors();
    void showFrame(int);
    void showStage(int);
    void select(RecoveryLink);
    void showDetails(Json);
    void locateSelection(bool original);
    QString language_;TextCatalog catalog_;QPointer<QWidget> controls_;
    QCheckBox* enabled_;QComboBox *input_,*provider_,*sequences_,*bytes_;
    ParameterEditor* parameters_;QLabel *about_,*issues_,*scope_,*selectionLabel_;
    QPushButton* apply_;QTabWidget* pages_;QTableView *hex_,*values_,*frames_;
    QTreeWidget *stages_,*checks_;QPlainTextEdit* details_;
    RecoveryTableModel *hexModel_,*valueModel_,*frameModel_;
    RecoveryComplexView* complex_;
    QAction *locateInput_,*locateOriginal_,*exportJson_,*exportBytes_;
    std::shared_ptr<const WaveformAnalysis> raw_,processed_;
    SignalSampleRange rawRange_,processedRange_;
    std::shared_ptr<const RecoveryResult> result_;std::optional<WaveformRecoveryRequest> applied_;
    std::optional<RecoveryLink> selected_;std::optional<unsigned> frame_;
    std::vector<SignalSampleRange> selectedRanges_;Json summaries_=Json::array();
    std::optional<Json> pendingParameters_;bool refreshing_=false,selecting_=false,available_=false;
};
}
