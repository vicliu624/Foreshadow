#pragma once
#include "research_views.hpp"
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
class WaveformWindowPanel final:public QWidget {
public:
    WaveformWindowPanel(const QString&,QWidget* parent=nullptr);
    ~WaveformWindowPanel() override;
    QWidget* controls() const {return controls_;}
    std::optional<WindowResearchOptions> draft() const;
    bool draftProcessed() const;
    void restoreDraft(const Json&);
    void restoreView(const Json&);
    void setAvailable(bool);
    void setResult(std::shared_ptr<const WindowResearch>,bool processed=false);
    const std::shared_ptr<const WindowResearch>& result() const {return result_;}
    bool processed() const {return processed_;}
    Json configuration() const;
    Json snapshot() const;
    std::function<void()> apply;
    std::function<void(const ResearchBandSelection&,std::shared_ptr<const WaveformAnalysis>)> bandSelected;
private:
    QString text(const std::string&,const Json& args=Json::object()) const;
    void refreshColors();
    QString language_;TextCatalog catalog_;QPointer<QWidget> controls_;
    std::shared_ptr<const WindowResearch> result_;bool processed_=false;
    double applied_low_db_=-100,applied_high_db_=-20;
    QCheckBox* enabled_;QComboBox *input_,*fft_,*window_,*scaling_;QSpinBox *hop_,*lags_,*bins_;QDoubleSpinBox *low_,*high_;QPushButton* apply_;
    QLabel* scope_;std::array<QLabel*,7> statistics_{};QTabWidget* pages_;
    ResearchSpectrogramView* spectrogram_;ResearchCurveView *psd_,*correlation_,*histogram_;
};
}
