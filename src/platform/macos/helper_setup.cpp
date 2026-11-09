#include "helper_setup.h"
#include "dummynet_backend.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QFile>
#include <QDirIterator>
#include <QSettings>

#include <sys/stat.h>
#include <unistd.h>

namespace {
const QString root = QStringLiteral("/Library/Application Support/Clumsier/Helper");
// Verify runtime files as well as the executable: privileged Python imports
// must never resolve to user-writable code. Symlinks may stay inside the runtime.
bool trusted(QString *error) {
    auto check = [&](const QString &path) {
        struct stat info{};
        const QByteArray native = QFile::encodeName(path);
        return ::stat(native.constData(), &info) == 0 && info.st_uid == 0
            && !(info.st_mode & (S_IWGRP | S_IWOTH));
    };
    QString parent = root;
    while (parent != "/") {
        if (QFileInfo(parent).isSymLink() || !check(parent)) { *error = "The installed helper is missing or writable. Reinstall the Clumsier package."; return false; }
        parent = QFileInfo(parent).absolutePath();
    }
    QDirIterator files(root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        files.next();
        const QFileInfo file = files.fileInfo();
        const QString resolved = file.canonicalFilePath();
        if (!(resolved == root || resolved.startsWith(root + '/')) || !check(file.filePath())) {
            *error = "Unsafe installed helper runtime. Reinstall the Clumsier package.";
            return false;
        }
    }
    return QFileInfo(root + "/clumsier-lag-helper").isExecutable();
}
}
MacHelperSetup::MacHelperSetup(DummynetBackend *backend, QObject *parent)
    : QObject(parent), backend_(backend) {
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            message_ = "Could not open the macOS authorization prompt. Try again.";
            emit changed();
        }
    });
    connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            const QString detail = QString::fromUtf8(process_.readAllStandardError()).trimmed();
            if (status != QProcess::NormalExit || code != 0) {
                message_ = "Networking was not authorized. You can retry; nothing starts automatically. " + detail.left(500);
            } else {
                char error[NETWORK_ERROR_SIZE]{};
                message_ = backend_->probe(error) ? "Networking ready. Choose a delay and press Start. All TCP traffic may be affected; UDP is not delayed."
                    : "Helper authorization finished, but connection failed: " + QString::fromUtf8(error);
            }
            emit changed();
        });
}
MacHelperSetup::~MacHelperSetup() {
    if (busy()) { process_.kill(); process_.waitForFinished(2000); }
}
bool MacHelperSetup::packaged() const {
    return QFileInfo(QCoreApplication::applicationDirPath() + "/../Resources/ClumsierLocalPackage").exists();
}
bool MacHelperSetup::firstRun() const { return !QSettings().value("macLocalSetupSeen", false).toBool(); }
void MacHelperSetup::acknowledge() { QSettings().setValue("macLocalSetupSeen", true); }
void MacHelperSetup::authorize() {
    if (busy() || !packaged()) return;
    QString error;
    if (!trusted(&error)) { message_ = error.isEmpty() ? "Installed helper executable is missing. Reinstall Clumsier." : error; emit changed(); return; }
    char networkError[NETWORK_ERROR_SIZE]{};
    if (backend_->probe(networkError)) { message_ = "Networking already ready. No second helper started."; emit changed(); return; }
    // Fixed privileged executable and numeric UID only; no user input or shell
    // fragments from presets, app paths, environment, or server names.
    const QString script = QStringLiteral("do shell script \"/usr/bin/env -i PATH=/usr/bin:/bin:/usr/sbin:/sbin HOME=/var/root '/Library/Application Support/Clumsier/Helper/clumsier-lag-helper' --uid %1 --background\" with administrator privileges").arg(getuid());
    message_ = "Waiting for administrator approval. No lag is enabled by authorization alone.";
    process_.start("/usr/bin/osascript", {"-e", script});
    emit changed();
}
