#pragma once

#include "signalscope/backend/iq/iq.hpp"
#include <QStringList>

namespace signalscope::desktop {
QStringList eventColumns();
QStringList eventCells(const Json& event);
QString eventEvidence(const Json& event);
}
