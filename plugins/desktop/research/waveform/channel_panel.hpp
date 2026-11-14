#pragma once
#include "trace_view.hpp"
#include "signalscope/backend/iq/text_catalog.hpp"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QPointer>
#include <QSpinBox>
#include <array>

namespace signalscope::desktop {
class WaveformChannelPanel final:public QWidget {
public:
    WaveformChannelPanel(const QString& language,QWidget* parent=nullptr);
    ~WaveformChannelPanel() override;
    QWidget* controls() const {return controls_;}
    std::optional<ChannelExtractionOptions> draft() const;
    void restoreDraft(const Json&);
    void restoreView(const Json&);
    void setResult(std::shared_ptr<const WaveformAnalysis>,std::optional<ChannelExtractionOptions>,bool preserve);
    void setAvailable(bool enabled);
    SignalSampleRange range() const {return views_[0]->range();}
    void selectBand(double offset_hz,double passband_hz);
    std::optional<SignalSampleRange> draftFileRange() const;
    void resetFileScope();
    void setRecordedSource(std::shared_ptr<const SignalSourceInfo>,SignalSampleRange loaded,bool preserve_draft);
    void setSourceRange(SignalSampleRange);
    void selectAnalysisRange(SignalSampleRange);
    void setSourceCursors(std::optional<std::uint64_t>,std::optional<std::uint64_t>);
    Json configuration() const;
    Json snapshot() const;
    std::function<void()> apply;
    std::function<void(SignalSampleRange)> sourceRangeChanged;
    // The integer raw cursor identifies the preceding observed raw sample.
    // The panel also displays the exact rational position of its own point.
    std::function<void(std::uint64_t,bool)> sourceCursorChanged;
    std::function<void(SourceSamplePosition)> revealOriginal;
private:
    QString text(const std::string&,const Json& arguments=Json::object()) const;
    void changeRange(SignalSampleRange);
    void refreshCursors();
    void refreshStatus();
    QString language_;TextCatalog catalog_;
    std::shared_ptr<const WaveformAnalysis> analysis_;
    std::optional<ChannelExtractionOptions> applied_;
    std::optional<std::uint64_t> a_,b_;
    bool synchronizing_=false,available_=false;
    QCheckBox* enabled_;QDoubleSpinBox *offset_,*passband_,*transition_;QSpinBox* rate_;QPushButton* apply_;
    QLabel *status_,*cursor_;std::array<TraceView*,4> views_{};
    QPushButton* reveal_;
    QPointer<QWidget> controls_;
    QWidget* file_options_;QCheckBox* file_scope_;QLineEdit *file_first_,*file_count_;QLabel* file_total_;
    std::filesystem::path file_source_path_;SignalSampleRange loaded_file_range_;
};
}
