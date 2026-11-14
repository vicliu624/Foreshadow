#pragma once
#include "recorded_worker.hpp"
#include "trace_view.hpp"
#include "plane_view.hpp"
#include "plugins/desktop/common/parameter_editor.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <array>

namespace signalscope::desktop {
// A consumer of reference descriptors and comparison traces. Model equations,
// parameter dependencies and translations remain with the model providers.
class WaveformModelPanel final:public QWidget {
public:
    WaveformModelPanel(const QString&,QWidget* parent=nullptr);
    ~WaveformModelPanel() override;
    QWidget* controls() const {return controls_;}
    void setAvailable(bool);
    void setInputs(std::shared_ptr<const WaveformAnalysis> raw,std::shared_ptr<const WaveformAnalysis> processed);
    void setSelection(SignalSampleRange raw,SignalSampleRange processed);
    std::optional<WaveformModelRequest> draft();
    static std::optional<WaveformModelRequest> readConfiguration(const Json&);
    void restoreDraft(const Json&);
    void restoreView(const Json&);
    void setResult(std::shared_ptr<const ReferenceComparison>,std::optional<WaveformModelRequest>);
    void setSourceCursors(std::optional<std::uint64_t>,std::optional<std::uint64_t>);
    QString failureText(std::exception_ptr,const std::string& fallback_key);
    Json configuration() const;
    Json snapshot() const;
    const std::shared_ptr<const ReferenceComparison>& result() const {return result_;}
    const std::optional<WaveformModelRequest>& appliedRequest() const {return applied_;}
    std::function<void()> apply;
    std::function<void(SignalSampleRange)> sourceRangeChanged;
    std::function<void(std::uint64_t,bool)> sourceCursorChanged;
private:
    QString text(const std::string&,const Json& args=Json::object()) const;
    Json descriptors(const ReferenceModelProvider&,const Json&) const;
    ReferenceModelContext context() const;
    ReferenceFitOptions fitOptions() const;
    void populate(const Json& parameters=Json::object());
    void refreshDescriptors();
    void changeRange(SignalSampleRange);
    void refreshCursors();
    QString language_;TextCatalog catalog_;QPointer<QWidget> controls_;
    QCheckBox *enabled_,*gain_,*dc_;QComboBox *input_,*provider_;
    QDoubleSpinBox* offset_;QSpinBox *delay_min_,*delay_max_;QPushButton* apply_;
    ParameterEditor* parameters_;QLabel *about_,*scope_,*fit_,*cursor_,*issues_;
    QTabWidget* pages_;std::array<TraceView*,3> views_{};TracePlaneView* plane_;
    std::shared_ptr<const WaveformAnalysis> raw_,processed_;
    SignalSampleRange raw_range_,processed_range_;
    std::shared_ptr<const ReferenceComparison> result_;
    std::optional<WaveformModelRequest> applied_;
    std::optional<std::uint64_t> a_,b_;
    bool refreshing_=false,synchronizing_=false;
};
}
