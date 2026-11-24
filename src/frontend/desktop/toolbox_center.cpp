#include "toolbox_center.hpp"
#include "signalscope/backend/iq/path.hpp"
#include "signalscope/desktop/plugin_text.hpp"
#include "toolbox_services.hpp"
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTabBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <thread>

namespace signalscope::desktop {
using namespace distribution;
class ToolboxCenter::Impl {
  public:
    struct Row {
        std::string id;
        Json installed, manifest;
        std::optional<Package> candidate;
        Compatibility compatibility;
    };
    ToolboxCenter *owner;
    std::shared_ptr<Service> service;
    std::function<bool()> canApply;
    std::function<void()> configure;
    Catalog catalog;
    std::vector<Row> rows;
    std::jthread job;
    bool working = false;
    QTabBar *tabs;
    QLineEdit *search;
    QTableWidget *table;
    QPlainTextEdit *detail;
    QLabel *status;
    QProgressBar *progress;
    QPushButton *check, *install, *enable, *remove, *rollback, *offline, *cache, *restart, *cancel, *configureButton;
    Impl(ToolboxCenter *parent, std::shared_ptr<Service> s, std::function<bool()> guard, std::function<void()> settings)
        : owner(parent), service(std::move(s)), canApply(std::move(guard)), configure(std::move(settings)) {
        owner->setObjectName("toolbox-center-window");
        owner->setWindowTitle(toolboxText("title"));
        owner->resize(1060, 650);
        owner->setMinimumSize(640, 460);
        auto *layout = new QVBoxLayout(owner);
        auto *heading = new QLabel(toolboxText("heading"));
        heading->setWordWrap(true);
        layout->addWidget(heading);
        auto *top = new QHBoxLayout;
        search = new QLineEdit;
        search->setObjectName("toolbox-search");
        search->setPlaceholderText(toolboxText("search"));
        top->addWidget(search, 1);
        check = new QPushButton(toolboxText("check"));
        check->setObjectName("toolbox-check");
        top->addWidget(check);
        layout->addLayout(top);
        tabs = new QTabBar;
        tabs->setObjectName("toolbox-tabs");
        for (const auto *key : {"official", "installed", "updates", "software"})
            tabs->addTab(toolboxText(key));
        layout->addWidget(tabs);
        auto *split = new QSplitter;
        table = new QTableWidget;
        table->setObjectName("toolbox-list");
        table->setColumnCount(5);
        table->setHorizontalHeaderLabels({toolboxText("name"), toolboxText("current"), toolboxText("loaded"),
                                          toolboxText("latest"), toolboxText("state")});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::SingleSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->verticalHeader()->hide();
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        for (int col = 1; col < 5; ++col)
            table->horizontalHeader()->setSectionResizeMode(col, QHeaderView::ResizeToContents);
        detail = new QPlainTextEdit;
        detail->setReadOnly(true);
        detail->setObjectName("toolbox-details");
        split->addWidget(table);
        split->addWidget(detail);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 2);
        layout->addWidget(split, 1);
        auto *actions = new QHBoxLayout;
        const auto button = [&](QPushButton *&target, const char *key, const char *name) {
            target = new QPushButton(toolboxText(key));
            target->setObjectName(name);
            actions->addWidget(target);
        };
        button(install, "install", "toolbox-install");
        button(enable, "disable", "toolbox-enable");
        button(remove, "uninstall", "toolbox-uninstall");
        button(rollback, "rollback", "toolbox-rollback");
        button(configureButton, "configure", "toolbox-configure");
        actions->addStretch();
        layout->addLayout(actions);
        progress = new QProgressBar;
        progress->setObjectName("toolbox-progress");
        progress->hide();
        layout->addWidget(progress);
        status = new QLabel;
        status->setTextFormat(Qt::PlainText);
        status->setWordWrap(true);
        status->setObjectName("toolbox-status");
        layout->addWidget(status);
        auto *bottom = new QHBoxLayout;
        offline = new QPushButton(toolboxText("offline"));
        offline->setObjectName("toolbox-offline");
        bottom->addWidget(offline);
        cache = new QPushButton(toolboxText("clear_cache"));
        bottom->addWidget(cache);
        auto *automatic = new QCheckBox(toolboxText("automatic"));
        automatic->setChecked(QSettings().value("updates/check_on_startup", true).toBool());
        bottom->addWidget(automatic);
        bottom->addStretch();
        restart = new QPushButton(toolboxText("restart"));
        restart->setObjectName("toolbox-restart");
        bottom->addWidget(restart);
        cancel = new QPushButton(toolboxText("cancel"));
        cancel->setEnabled(false);
        bottom->addWidget(cancel);
        auto *close = new QPushButton(toolboxText("close"));
        bottom->addWidget(close);
        layout->addLayout(bottom);
        QObject::connect(close, &QPushButton::clicked, owner, &QDialog::hide);
        QObject::connect(automatic, &QCheckBox::toggled, owner,
                         [](bool value) { QSettings().setValue("updates/check_on_startup", value); });
        QObject::connect(check, &QPushButton::clicked, owner, [this] { owner->checkForUpdates(); });
        QObject::connect(cancel, &QPushButton::clicked, owner, [this] {
            job.request_stop();
            status->setText(toolboxText("cancelling"));
        });
        QObject::connect(search, &QLineEdit::textChanged, owner, [this] { refresh(); });
        QObject::connect(tabs, &QTabBar::currentChanged, owner, [this] { refresh(); });
        QObject::connect(table, &QTableWidget::itemSelectionChanged, owner, [this] { selection(); });
        QObject::connect(install, &QPushButton::clicked, owner, [this] {
            const auto *row = selected();
            if (row && row->candidate)
                installPackage(*row->candidate, {});
        });
        QObject::connect(enable, &QPushButton::clicked, owner, [this] { mutate("enable"); });
        QObject::connect(remove, &QPushButton::clicked, owner, [this] { mutate("uninstall"); });
        QObject::connect(rollback, &QPushButton::clicked, owner, [this] { mutate("rollback"); });
        QObject::connect(configureButton, &QPushButton::clicked, owner, [this] {
            owner->hide();
            if (configure)
                configure();
        });
        QObject::connect(cache, &QPushButton::clicked, owner, [this] {
            run([this](std::stop_token) { service->store().clearCache(); },
                [this] { status->setText(toolboxText("cache_cleared")); });
        });
        QObject::connect(offline, &QPushButton::clicked, owner, [this] { importPackage(); });
        QObject::connect(restart, &QPushButton::clicked, owner, [this] {
            if (!allowed())
                return;
            try {
                if (scheduleDistributionRestart(*service, QCoreApplication::arguments()))
                    QCoreApplication::quit();
            } catch (const std::exception &error) {
                status->setText(toolboxError(error));
            }
        });
        try {
            catalog = service->cached();
        } catch (const std::exception &error) {
            status->setText(toolboxError(error));
        }
        refresh();
    }
    ~Impl() {
        job.request_stop();
        if (job.joinable())
            job.join();
    }
    bool allowed() {
        if (canApply && !canApply()) {
            status->setText(toolboxText("active_tasks"));
            return false;
        }
        return true;
    }
    const Row *selected() const {
        const auto selectedRows = table->selectionModel()->selectedRows();
        if (selectedRows.size() != 1)
            return nullptr;
        const auto row = selectedRows.front().row();
        if (!table->item(row, 0))
            return nullptr;
        const auto id = table->item(row, 0)->data(Qt::UserRole).toString().toStdString();
        for (const auto &item : rows)
            if (item.id == id)
                return &item;
        return nullptr;
    }
    void controls() {
        check->setEnabled(!working);
        offline->setEnabled(!working);
        cache->setEnabled(!working);
        cancel->setEnabled(working);
        progress->setVisible(working);
        restart->setEnabled(!working && service->restartRequired());
        selection();
    }
    void run(std::function<void(std::stop_token)> work, std::function<void()> done) {
        if (working)
            return;
        working = true;
        controls();
        progress->setRange(0, 0);
        QPointer<ToolboxCenter> safe(owner);
        job = std::jthread([this, safe, work = std::move(work), done = std::move(done)](std::stop_token stop) {
            std::exception_ptr error;
            try {
                work(stop);
            } catch (...) {
                error = std::current_exception();
            }
            if (safe)
                QMetaObject::invokeMethod(
                    safe,
                    [this, safe, error, done] {
                        if (!safe)
                            return;
                        working = false;
                        owner->setWindowModality(Qt::NonModal);
                        progress->hide();
                        try {
                            refresh();
                            if (error)
                                std::rethrow_exception(error);
                            if (done)
                                done();
                        } catch (const std::exception &exception) {
                            status->setText(toolboxError(exception));
                        }
                        controls();
                    },
                    Qt::QueuedConnection);
        });
    }
    Progress progressCallback() {
        QPointer<ToolboxCenter> safe(owner);
        return [this, safe](std::uint64_t received, std::uint64_t total) {
            if (safe)
                QMetaObject::invokeMethod(
                    safe,
                    [this, safe, received, total] {
                        if (!safe)
                            return;
                        progress->setRange(0, total ? 1000 : 0);
                        if (total)
                            progress->setValue(
                                static_cast<int>(std::min<std::uint64_t>(1000, received * 1000 / total)));
                        status->setText(
                            toolboxText("downloading", {{"received", received / 1024}, {"total", total / 1024}}));
                    },
                    Qt::QueuedConnection);
        };
    }
    void refresh() {
        const auto *old = selected();
        const auto selectedId = old ? old->id : std::string();
        rows.clear();
        const auto state = service->store().state();
        const auto env = service->environment();
        std::map<std::string, Row> byId;
        for (const auto &[id, item] : state.at("installed").items()) {
            auto &row = byId[id];
            row.id = id;
            row.installed = item;
            row.manifest = item.at("manifest");
        }
        for (const auto &p : catalog.packages) {
            if (!Version::parse(p.version).prerelease.empty())
                continue;
            auto &row = byId[p.id];
            row.id = p.id;
            const auto preferred = p.platform == env.platform || p.platform == "any";
            const auto existingPreferred =
                row.candidate && (row.candidate->platform == env.platform || row.candidate->platform == "any");
            if (!row.candidate || (preferred && !existingPreferred) ||
                (preferred == existingPreferred &&
                 Version::parse(p.version) > Version::parse(row.candidate->version))) {
                row.candidate = p;
                row.compatibility = compatible(p, env);
                if (row.compatibility.compatible &&
                    (row.installed.is_null() ||
                     Version::parse(p.version) > Version::parse(row.installed.at("version"))))
                    row.compatibility = service->store().assessInstall(p, env);
                if (row.installed.is_null())
                    row.manifest = p.manifest;
            }
            if (row.manifest.is_null())
                row.manifest = p.manifest;
        }
        int updateCount = 0;
        for (auto &[id, row] : byId) {
            const bool update = row.candidate && row.compatibility.compatible &&
                                (row.installed.is_null() ||
                                 Version::parse(row.candidate->version) > Version::parse(row.installed.at("version")));
            if (update && !row.installed.is_null())
                ++updateCount;
            const bool application = row.manifest.at("kind") == "application";
            if ((tabs->currentIndex() == 3) != application)
                continue;
            if (tabs->currentIndex() == 1 && row.installed.is_null())
                continue;
            if (tabs->currentIndex() == 2 && (!update || row.installed.is_null()))
                continue;
            const auto name = pluginText(row.manifest.value("name", Json(id)), QString::fromStdString(id));
            if (!search->text().isEmpty() &&
                !(name + ' ' + QString::fromStdString(id)).contains(search->text(), Qt::CaseInsensitive))
                continue;
            rows.push_back(std::move(row));
        }
        const QSignalBlocker block(table);
        table->setRowCount(static_cast<int>(rows.size()));
        int selectionIndex = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const auto &row = rows[i];
            const auto name = pluginText(row.manifest.value("name", Json(row.id)), QString::fromStdString(row.id));
            auto *item = new QTableWidgetItem(name);
            item->setData(Qt::UserRole, QString::fromStdString(row.id));
            table->setItem(static_cast<int>(i), 0, item);
            table->setItem(
                static_cast<int>(i), 1,
                new QTableWidgetItem(row.installed.is_null()
                                         ? toolboxText("not_installed")
                                         : QString::fromStdString(row.installed.at("version").get<std::string>())));
            const auto &loaded = service->loadedInstallations();
            table->setItem(
                static_cast<int>(i), 2,
                new QTableWidgetItem(loaded.contains(row.id)
                                         ? QString::fromStdString(loaded.at(row.id).at("version").get<std::string>())
                                         : toolboxText("not_loaded")));
            table->setItem(static_cast<int>(i), 3,
                           new QTableWidgetItem(row.candidate ? QString::fromStdString(row.candidate->version)
                                                              : toolboxText("with_application")));
            const auto stateText = !row.compatibility.compatible
                                       ? toolboxText("error." + row.compatibility.code, row.compatibility.arguments)
                                   : row.installed.is_null()               ? toolboxText("available")
                                   : !row.installed.value("enabled", true) ? toolboxText("disabled")
                                   : row.installed.value("bundled", false) ? toolboxText("bundled")
                                                                           : toolboxText("installed_state");
            const bool pending = service->pendingInstallation(row.id);
            table->setItem(static_cast<int>(i), 4,
                           new QTableWidgetItem(pending ? toolboxText("pending_restart") : stateText));
            if (row.id == selectedId)
                selectionIndex = static_cast<int>(i);
        }
        if (!rows.empty())
            table->selectRow(selectionIndex);
        selection();
        restart->setEnabled(!working && service->restartRequired());
        if (owner->updatesAvailable)
            owner->updatesAvailable(updateCount);
    }
    void selection() {
        const auto *row = selected();
        install->setEnabled(false);
        enable->setEnabled(false);
        remove->setEnabled(false);
        rollback->setEnabled(false);
        configureButton->setEnabled(false);
        if (!row) {
            detail->setPlainText(toolboxText("choose"));
            return;
        }
        const auto name = pluginText(row->manifest.value("name", Json(row->id)), QString::fromStdString(row->id));
        QString text = name + "\n" + QString::fromStdString(row->id) + "\n\n" +
                       pluginText(row->manifest.value("description", Json::object())) + "\n\n";
        text += toolboxText("platform", {{"platform", row->manifest.value("platform", std::string())}}) + "\n";
        if (row->candidate) {
            text += toolboxText("package_sizes", {{"download", row->candidate->download_size},
                                                  {"installed", row->candidate->installed_size}}) +
                    "\n";
            for (const auto &dependency : row->candidate->manifest.value("dependencies", Json::array()))
                text +=
                    toolboxText("package_dependency", {{"id", dependency.at("id")},
                                                       {"minimum", dependency.value("minimum", std::string("*"))},
                                                       {"maximum", dependency.value("maximum", std::string("*"))}}) +
                    "\n";
            text += "\n" + toolboxText("release_notes") + "\n" + pluginText(row->candidate->notes) + "\n";
            if (!row->compatibility.compatible)
                text += "\n" + toolboxText("error." + row->compatibility.code, row->compatibility.arguments);
        }
        if (!row->installed.is_null())
            text += "\n" + toolboxText("restart_notice");
        detail->setPlainText(text);
        install->setText(row->installed.is_null() ? toolboxText("install") : toolboxText("update"));
        install->setEnabled(!working && row->candidate && row->compatibility.compatible &&
                            (row->installed.is_null() ||
                             Version::parse(row->candidate->version) > Version::parse(row->installed.at("version"))));
        const auto actions =
            row->manifest.value("actions", std::vector<std::string>{"enable", "disable", "uninstall", "rollback"});
        const auto supports = [&](const char *action) {
            return std::find(actions.begin(), actions.end(), action) != actions.end();
        };
        if (!row->installed.is_null()) {
            const bool enabled = row->installed.value("enabled", true);
            enable->setText(toolboxText(enabled ? "disable" : "enable"));
            enable->setEnabled(!working && supports(enabled ? "disable" : "enable"));
            remove->setEnabled(!working && !row->installed.value("bundled", false) && supports("uninstall"));
            rollback->setEnabled(!working && row->installed.contains("previous") && supports("rollback"));
        }
        configureButton->setEnabled(!working && supports("configure") && bool(configure));
    }
    void installPackage(Package package, std::filesystem::path offlineArchive) {
        const auto assessment = service->store().assessInstall(package, service->environment());
        if (!assessment.compatible) {
            status->setText(toolboxText("error." + assessment.code, assessment.arguments));
            return;
        }
        status->setText(toolboxText("preparing"));
        const auto progress = progressCallback();
        auto archive = std::make_shared<std::filesystem::path>();
        run(
            [this, package, offlineArchive, progress, archive](std::stop_token stop) {
                *archive = offlineArchive.empty() ? service->download(package, stop, progress) : offlineArchive;
                if (stop.stop_requested())
                    throw Error("cancelled");
            },
            [this, package, archive] {
                if (!allowed())
                    return;
                owner->setWindowModality(Qt::ApplicationModal);
                owner->show();
                status->setText(toolboxText("installing"));
                run(
                    [this, package, archive](std::stop_token stop) {
                        if (stop.stop_requested())
                            throw Error("cancelled");
                        service->store().install(package, *archive, service->environment());
                    },
                    [this] {
                        owner->setWindowModality(Qt::NonModal);
                        status->setText(toolboxText("installed_restart"));
                    });
                cancel->setEnabled(false);
            });
    }
    void mutate(const std::string &operation) {
        const auto *row = selected();
        if (!row || row->installed.is_null() || !allowed())
            return;
        const auto id = row->id;
        const bool enabled = row->installed.value("enabled", true);
        if (operation == "uninstall" &&
            QMessageBox::question(owner, toolboxText("uninstall"),
                                  toolboxText("confirm_uninstall", {{"name", row->id}})) != QMessageBox::Yes)
            return;
        owner->setWindowModality(Qt::ApplicationModal);
        run(
            [this, operation, id, enabled](std::stop_token) {
                if (operation == "enable")
                    service->store().setEnabled(id, !enabled);
                else if (operation == "rollback")
                    service->store().rollback(id);
                else
                    service->store().uninstall(id);
            },
            [this] { status->setText(toolboxText("installed_restart")); });
    }
    void importPackage() {
        const auto file =
            QFileDialog::getOpenFileName(owner, toolboxText("offline"), {}, "*.amctbx *.amcpkg *.tgz *.tar.gz");
        if (file.isEmpty())
            return;
        try {
            const auto path = path_from_utf8(file.toStdString());
            const auto hash = sha256(path);
            const auto companion = path.parent_path() / "catalog.json";
            if (std::filesystem::is_regular_file(companion))
                catalog = service->importCatalog(readJson(companion).dump());
            for (const auto &candidate : catalog.packages)
                if (candidate.sha256 == hash) {
                    installPackage(candidate, path);
                    return;
                }
            throw Error("offline_catalog");
        } catch (const std::exception &error) {
            status->setText(toolboxError(error));
        }
    }
};
ToolboxCenter::ToolboxCenter(std::shared_ptr<Service> service, std::function<bool()> guard,
                             std::function<void()> configure, QWidget *parent)
    : QDialog(parent), impl_(std::make_unique<Impl>(this, std::move(service), std::move(guard), std::move(configure))) {
}
ToolboxCenter::~ToolboxCenter() = default;
bool ToolboxCenter::busy() const { return impl_->working; }
void ToolboxCenter::showSoftwareUpdates() {
    impl_->tabs->setCurrentIndex(3);
    show();
    raise();
    activateWindow();
}
void ToolboxCenter::checkForUpdates(bool quiet) {
    if (impl_->working)
        return;
    impl_->status->setText(toolboxText("checking"));
    auto result = std::make_shared<Catalog>();
    impl_->run(
        [this, result](std::stop_token stop) { *result = impl_->service->check(stop, impl_->progressCallback()); },
        [this, result, quiet] {
            impl_->catalog = *result;
            impl_->refresh();
            QSettings().setValue("updates/last_check", QDateTime::currentDateTimeUtc());
            impl_->status->setText(toolboxText("checked"));
            if (!quiet) {
                show();
                raise();
            }
        });
}
} // namespace signalscope::desktop
