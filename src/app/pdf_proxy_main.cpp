#include "pinloom/core/PdfXChangeCommand.h"
#include "pinloom/core/PdfXChangeOpenProxy.h"
#include "pinloom/widgets/PinloomSingleInstance.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <QStringList>

namespace {

QString configuredPdfXChangeExecutablePath(QSettings &settings)
{
    const QString configured =
        settings.value(QStringLiteral("applications/pdfXChangeExecutablePath")).toString().trimmed();
    return configured.isEmpty() ? Pinloom::resolvePdfXChangeExecutablePath() : configured;
}

QStringList pdfFileArguments(const QStringList &arguments)
{
    QStringList files;
    for (int index = 1; index < arguments.size(); ++index) {
        const QString path = Pinloom::normalizedPdfXChangeOpenFilePath(arguments.at(index));
        if (!path.isEmpty()) {
            files.append(path);
        }
    }
    files.removeDuplicates();
    return files;
}

QString siblingExecutablePath(const QString &baseName)
{
    QString executableName = baseName;
#ifdef Q_OS_WIN
    if (!executableName.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive)) {
        executableName.append(QStringLiteral(".exe"));
    }
#endif
    return QDir(QCoreApplication::applicationDirPath()).filePath(executableName);
}

void startPinloomResidentIfNeeded()
{
    QString error;
    if (Pinloom::sendPinloomSingleInstanceMessage(Pinloom::defaultPinloomSingleInstanceServerName(),
                                                  QStringLiteral("resident"),
                                                  80,
                                                  &error)) {
        return;
    }

    const QString appPath = siblingExecutablePath(QStringLiteral("pinloom_app"));
    if (!QFileInfo::exists(appPath)) {
        return;
    }
    QProcess::startDetached(appPath, {QStringLiteral("--hidden")});
}

bool notifyOpenedPdf(const QString &filePath)
{
    const QString message = Pinloom::pinloomPdfXChangeOpenMessageForFile(filePath);
    if (message.isEmpty()) {
        return false;
    }

    QString error;
    return Pinloom::sendPinloomSingleInstanceMessage(Pinloom::defaultPinloomSingleInstanceServerName(),
                                                     message,
                                                     80,
                                                     &error);
}

int showProxyError(int exitCode, const QString &message)
{
    QMessageBox::warning(nullptr, QStringLiteral("Pinloom PDF Proxy"), message);
    return exitCode;
}

} // namespace

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinloom"));
    QApplication::setOrganizationName(QStringLiteral("Pinloom"));

    const QStringList pdfFiles = pdfFileArguments(QCoreApplication::arguments());
    if (pdfFiles.isEmpty()) {
        return 2;
    }

    QSettings settings;
    const QString pdfXChangePath = configuredPdfXChangeExecutablePath(settings);
    if (pdfXChangePath.trimmed().isEmpty()) {
        return showProxyError(
            3,
            QStringLiteral("PDF-XChange Editor was not found. Configure PDFXEdit.exe in Pinloom settings."));
    }

    bool allLaunched = true;
    for (const QString &filePath : pdfFiles) {
        const bool launched =
            QProcess::startDetached(pdfXChangePath, {QDir::toNativeSeparators(filePath)});
        if (!launched) {
            allLaunched = false;
            continue;
        }

        Pinloom::rememberPdfXChangeOpenedFile(settings, filePath);
        notifyOpenedPdf(filePath);
    }

    startPinloomResidentIfNeeded();
    if (!allLaunched) {
        return showProxyError(
            4,
            QStringLiteral("Pinloom could not launch PDF-XChange Editor: %1")
                .arg(QDir::toNativeSeparators(pdfXChangePath)));
    }
    return 0;
}
