#pragma once

#include "signalscope/backend/dsp/spectrum.hpp"
#include <QWidget>
#include <vector>

class QTabWidget;

namespace signalscope::desktop {
// Display adapters receive the latest immutable preview on the desktop thread.
class SignalDisplay : public QWidget {
public:
    virtual void reset()=0;
    virtual void setFrame(const SpectrumFrame& frame)=0;
};

std::vector<SignalDisplay*> mountSignalDisplays(QTabWidget& tabs);
std::vector<SignalDisplay*> createSignalDisplays();
}
