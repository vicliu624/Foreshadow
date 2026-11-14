#pragma once
#include "signalscope/backend/dsp/recovery.hpp"
#include <QComboBox>
#include <QLabel>
#include <QTreeWidget>
#include <QWidget>
namespace signalscope::desktop {
class RecoveryComplexCanvas;
class RecoveryComplexView final : public QWidget {
  public:
    RecoveryComplexView(std::function<QString(const std::string &)> common,
                        std::function<QString(const std::string &)> owned, QWidget *parent = nullptr);
    void set(std::shared_ptr<const RecoveryResult>);
    Json configuration() const;
    void restore(const Json &);
    std::function<void(RecoveryLink)> selected;

  private:
    void groups();
    void redraw();
    std::function<QString(const std::string &)> common_, owned_;
    std::shared_ptr<const RecoveryResult> result_;
    QComboBox *sequence_, *group_;
    QLabel *domain_;
    QTreeWidget *metrics_;
    RecoveryComplexCanvas *canvas_;
};
} // namespace signalscope::desktop
