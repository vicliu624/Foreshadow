#include "legal_notices.hpp"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QPushButton>
#include <QTextBrowser>
#include <QUrl>
#include <QVBoxLayout>

namespace signalscope::desktop {
void showLegalNotices(QWidget* parent) {
    const auto root=QDir(QCoreApplication::applicationDirPath());
    QString index;
    for(const auto& relative:{"../share/foreshadow/compliance/index.html",
                              "../Resources/licenses/foreshadow/index.html"}) {
        const auto candidate=root.absoluteFilePath(QString::fromLatin1(relative));
        if(QFileInfo::exists(candidate)){index=candidate;break;}
    }
    const auto title=QCoreApplication::translate("LegalNotices","Licenses and source");
    if(index.isEmpty()) {
        QMessageBox::information(parent,title,QCoreApplication::translate("LegalNotices",
            "License materials are available in the packaged review or release build."));
        return;
    }
    auto* dialog=new QDialog(parent);dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("licenses-and-source");dialog->setWindowTitle(title);dialog->resize(960,640);
    auto* layout=new QVBoxLayout(dialog);auto* browser=new QTextBrowser;
    browser->setOpenLinks(false);browser->setSource(QUrl::fromLocalFile(index));
    QObject::connect(browser,&QTextBrowser::anchorClicked,dialog,[browser](const QUrl& url){QDesktopServices::openUrl(browser->source().resolved(url));});
    layout->addWidget(browser);
    auto* buttons=new QDialogButtonBox(QDialogButtonBox::Close);
    auto* folder=buttons->addButton(QCoreApplication::translate("LegalNotices","Open material folder"),QDialogButtonBox::ActionRole);
    QObject::connect(folder,&QPushButton::clicked,dialog,[index]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(index).absolutePath()));});
    QObject::connect(buttons,&QDialogButtonBox::rejected,dialog,&QDialog::close);
    layout->addWidget(buttons);dialog->show();
}
}
