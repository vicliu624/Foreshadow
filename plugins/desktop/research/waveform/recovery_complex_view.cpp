#include "recovery_complex_view.hpp"
#include "signalscope/desktop/number_format.hpp"
#include <QHBoxLayout>
#include <QHeaderView>
#include <QMouseEvent>
#include <QPainter>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <set>

namespace signalscope::desktop {
class RecoveryComplexCanvas final : public QWidget {
  public:
    struct Dot {
        QPointF point;
        RecoveryLink link;
        bool reference = false;
    };
    std::vector<Dot> dots;
    double extent_ = 1;
    std::function<void(RecoveryLink)> selected;
    explicit RecoveryComplexCanvas(QWidget *parent) : QWidget(parent) {
        setObjectName("waveform-recovery-complex-canvas");
        setMinimumSize(100, 100);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }
    QPointF pixel(QPointF p) const {
        const double scale = std::max(1., std::min(width(), height()) - 48.) / (2 * extent());
        return {width() / 2. + p.x() * scale, height() / 2. - p.y() * scale};
    }

  protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), palette().base());
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(palette().mid().color());
        p.drawLine(24, height() / 2, width() - 24, height() / 2);
        p.drawLine(width() / 2, 24, width() / 2, height() - 24);
        p.setPen(palette().text().color());
        p.drawText(QPointF(width() - 20, height() / 2. - 5), QStringLiteral("I"));
        p.drawText(QPointF(width() / 2. + 5, 20), QStringLiteral("Q"));
        p.drawText(QPointF(6, height() - 6), QString::number(extent(), 'g', 4));
        for (const auto &d : dots) {
            p.setPen(d.reference ? QColor(210, 125, 30) : palette().highlight().color());
            const auto at = pixel(d.point);
            if (d.reference) {
                p.drawLine(at + QPointF(-4, -4), at + QPointF(4, 4));
                p.drawLine(at + QPointF(-4, 4), at + QPointF(4, -4));
            } else {
                p.setBrush(palette().highlight());
                p.drawEllipse(at, 2.2, 2.2);
            }
        }
    }
    void mousePressEvent(QMouseEvent *e) override {
        const Dot *best = nullptr;
        double distance = 144;
        for (const auto &dot : dots)
            if (!dot.reference) {
                const auto delta = pixel(dot.point) - e->position();
                const auto d = QPointF::dotProduct(delta, delta);
                if (d < distance) {
                    distance = d;
                    best = &dot;
                }
            }
        if (best && selected)
            selected(best->link);
    }

  private:
    double extent() const { return extent_; }
};
RecoveryComplexView::RecoveryComplexView(std::function<QString(const std::string &)> common,
                                         std::function<QString(const std::string &)> owned, QWidget *parent)
    : QWidget(parent), common_(std::move(common)), owned_(std::move(owned)) {
    setObjectName("waveform-recovery-complex");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    metrics_ = new QTreeWidget;
    metrics_->setObjectName("waveform-recovery-metrics");
    metrics_->setHeaderLabels({common_("recovery.measurement"), common_("recovery.value"), common_("recovery.unit")});
    metrics_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    metrics_->setMaximumHeight(150);
    metrics_->setMinimumHeight(75);
    layout->addWidget(metrics_);
    auto *row = new QHBoxLayout;
    sequence_ = new QComboBox;
    sequence_->setObjectName("waveform-recovery-complex-sequence");
    sequence_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    sequence_->setMinimumContentsLength(12);
    group_ = new QComboBox;
    group_->setObjectName("waveform-recovery-complex-group");
    row->addWidget(sequence_, 1);
    row->addWidget(group_);
    layout->addLayout(row);
    domain_ = new QLabel;
    domain_->setObjectName("waveform-recovery-complex-domain");
    domain_->setWordWrap(true);
    layout->addWidget(domain_);
    canvas_ = new RecoveryComplexCanvas(this);
    layout->addWidget(canvas_, 1);
    canvas_->selected = [this](RecoveryLink link) {
        if (selected)
            selected(link);
    };
    connect(sequence_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { groups(); });
    connect(group_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this] { redraw(); });
}
void RecoveryComplexView::set(std::shared_ptr<const RecoveryResult> r) {
    result_ = std::move(r);
    const QSignalBlocker block(sequence_);
    sequence_->clear();
    metrics_->clear();
    if (result_) {
        for (const auto &m : result_->metrics) {
            auto *item = new QTreeWidgetItem(
                metrics_, {owned_(m.label_key), m.available ? displayMeasurement(m.value) : owned_(m.reason_key),
                           owned_(m.unit_key)});
            item->setToolTip(0, QString::fromStdString(m.id));
        }
        for (unsigned i = 0; i < result_->sequences.size(); ++i) {
            const auto &s = result_->sequences[i];
            if (s.kind == RecoveryValueKind::complex && !s.reference)
                sequence_->addItem(owned_(s.label_key), i);
        }
        // Default to the last measured plane, independent of provider IDs.
        if (sequence_->count())
            sequence_->setCurrentIndex(sequence_->count() - 1);
    }
    groups();
}
void RecoveryComplexView::groups() {
    const QSignalBlocker block(group_);
    group_->clear();
    group_->addItem(common_("recovery.all_groups"), -1);
    if (result_ && sequence_->currentIndex() >= 0) {
        std::set<unsigned> groups;
        for (const auto &v : result_->sequences[sequence_->currentData().toUInt()].values)
            groups.insert(v.group);
        for (auto g : groups)
            group_->addItem(QString::number(g), g);
    }
    redraw();
}
void RecoveryComplexView::redraw() {
    canvas_->dots.clear();
    domain_->clear();
    if (result_ && sequence_->currentIndex() >= 0) {
        const auto at = sequence_->currentData().toUInt();
        const auto &s = result_->sequences[at];
        const auto group = group_->currentData().toLongLong();
        domain_->setText(owned_(s.domain_key) + QStringLiteral(" · ") + common_("recovery.complex_legend"));
        const auto stride = std::max<std::size_t>(1, (s.values.size() + 8191) / 8192);
        for (unsigned i = 0; i < s.values.size(); i += static_cast<unsigned>(stride)) {
            const auto &v = s.values[i];
            if (v.known && (group < 0 || v.group == group))
                canvas_->dots.push_back({{v.complex_value.real(), v.complex_value.imag()}, {at, i, 1}, false});
        }
        for (const auto &ref : result_->sequences)
            if (ref.reference && ref.domain_key == s.domain_key) {
                const auto step = std::max<std::size_t>(1, (ref.values.size() + 8191) / 8192);
                for (std::size_t i = 0; i < ref.values.size(); i += step) {
                    const auto &v = ref.values[i];
                    if (canvas_->dots.size()>=16384)break;
                    if (v.known && (group < 0 || v.group == group))
                        canvas_->dots.push_back({{v.complex_value.real(), v.complex_value.imag()}, {}, true});
                }
            }
    }
    canvas_->extent_ = 1;
    for (const auto &dot : canvas_->dots)
        canvas_->extent_ = std::max({canvas_->extent_, std::abs(dot.point.x()) * 1.1, std::abs(dot.point.y()) * 1.1});
    canvas_->setProperty("displayedPoints", static_cast<unsigned>(canvas_->dots.size()));
    canvas_->update();
}
Json RecoveryComplexView::configuration() const {
    return {{"sequence_id", result_ && sequence_->currentIndex() >= 0
                                ? Json(result_->sequences[sequence_->currentData().toUInt()].id)
                                : Json(nullptr)},
            {"group", group_->currentData().isValid() ? group_->currentData().toLongLong() : -1}};
}
void RecoveryComplexView::restore(const Json &value) {
    if (!result_ || value.at("sequence_id").is_null())
        return;
    const auto id = value.at("sequence_id").get<std::string>();
    int index = -1;
    for (int i = 0; i < sequence_->count(); ++i)
        if (result_->sequences[sequence_->itemData(i).toUInt()].id == id)
            index = i;
    if (index < 0)
        throw std::invalid_argument("Missing complex sequence");
    sequence_->setCurrentIndex(index);
    const auto group = value.at("group").get<long long>();
    index = group_->findData(QVariant::fromValue<qlonglong>(group));
    if (index < 0)
        throw std::invalid_argument("Missing complex group");
    group_->setCurrentIndex(index);
}
} // namespace signalscope::desktop
