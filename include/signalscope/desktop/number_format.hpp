#pragma once

#include <QLocale>
#include <cmath>

namespace signalscope::desktop {
inline QLocale measurementLocale(QLocale locale=QLocale()) {
    locale.setNumberOptions(locale.numberOptions() | QLocale::OmitGroupSeparator);
    return locale;
}

inline QString displayDecimal(double value,int precision,const QLocale& locale=QLocale()) {
    return measurementLocale(locale).toString(value,'f',precision);
}
// A measurement can be much smaller than the fixed decimal readout. Keep its
// sign and nonzero magnitude visible without changing the stored numeric data.
inline QString displayMeasurement(double value,const QLocale& locale=QLocale()) {
    const auto magnitude=std::abs(value);
    return measurementLocale(locale).toString(value==0?0:value,magnitude!=0 && (magnitude<1e-4 || magnitude>=1e6)?'e':'f',6);
}
}
