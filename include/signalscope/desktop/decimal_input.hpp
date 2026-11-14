#pragma once

#include <QLocale>
#include <QRegularExpression>

namespace signalscope::desktop {
inline QRegularExpression decimalInputPattern(const QLocale& locale,int wholeDigits,int fractionalDigits) {
    const auto point=locale.decimalPoint();
    const auto separator=point==u'.'?QStringLiteral("\\."):
        QStringLiteral("(?:\\.|%1)").arg(QRegularExpression::escape(point));
    return QRegularExpression(QStringLiteral("\\p{Nd}{0,%1}(?:%2\\p{Nd}{0,%3})?")
                                  .arg(wholeDigits).arg(separator).arg(fractionalDigits));
}

inline QString canonicalDecimalInput(QString value,const QLocale& locale) {
    const auto point=locale.decimalPoint();
    if(point!=u'.')value.replace(point,QStringLiteral("."));
    auto codepoints=value.toStdU32String();
    for(auto& codepoint:codepoints) {
        if(QChar::category(codepoint)==QChar::Number_DecimalDigit)
            codepoint=u'0'+QChar::digitValue(codepoint);
    }
    return QString::fromStdU32String(codepoints);
}
}
