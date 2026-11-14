#pragma once
#include "signalscope/backend/distribution/distribution.hpp"
#include <QDialog>
#include <functional>
#include <memory>
namespace signalscope::desktop {
class ToolboxCenter final : public QDialog {
  public:
    ToolboxCenter(std::shared_ptr<distribution::Service>, std::function<bool()> canApply,
                  std::function<void()> configureBuiltins, QWidget *parent = nullptr);
    ~ToolboxCenter() override;
    void checkForUpdates(bool quiet = false);
    void showSoftwareUpdates();
    bool busy() const;
    std::function<void(int)> updatesAvailable;

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace signalscope::desktop
