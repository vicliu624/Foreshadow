#pragma once
#include <QWidget>
class QComboBox;
class QSpinBox;

namespace signalscope::desktop {
class SearchWidthControl : public QWidget {
    Q_OBJECT
public:
    explicit SearchWidthControl(QWidget* parent=nullptr);
    int value() const; // kHz
    int minimum() const {return 1;}
    int maximum() const {return 100000;}
    QString mode() const {return mode_;}
    void setValue(int khz);
    void setSelection(int khz,const QString& mode);
    void setAvailableWidth(double hz);
    int recommendedValue() const;
signals:
    void valueChanged(int khz);
private:
    void rebuild();
    QComboBox* presets_;
    QSpinBox* custom_;
    QString mode_="fixed";
    int value_=1000,available_=100000;
    bool availableKnown_=false;
};
}
