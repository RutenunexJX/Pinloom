#include "pinloom/core/PinloomFileAssociations.h"
#include "pinloom/core/PinloomStartup.h"

#include <QDir>
#include <QFileInfo>
#include <QMap>
#include <QSettings>
#include <QVariant>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shlobj.h>
#endif

namespace Pinloom {
namespace {
const QString Owner = QStringLiteral("pinloom-file-open/v1");
const QString Application = QStringLiteral("Software/Classes/Applications/pinloom_app.exe");
const QString Document = QStringLiteral("Software/Classes/Pinloom.PDF");
const QString Registration = QStringLiteral("Software/Pinloom/FileOpen");
const QString RegisteredApplication = QStringLiteral("Software/RegisteredApplications/Pinloom");
const QString OpenWith = QStringLiteral("Software/Classes/.pdf/OpenWithProgids/Pinloom.PDF");
const QStringList OwnedTrees{Application, Document, Registration};

QString executablePath(const QString &path)
{
    return QDir::toNativeSeparators(QDir::cleanPath(QFileInfo(path).absoluteFilePath()));
}

QMap<QString, QVariant> valuesFor(const QString &executable)
{
    const QString path = executablePath(executable);
    const QString command = QStringLiteral("\"%1\" -- \"%2\"").arg(path, QStringLiteral("%1"));
    const QString icon = QStringLiteral("\"%1\",0").arg(path);
    QMap<QString, QVariant> values{
        {Application + "/FriendlyAppName", "Pinloom"},
        {Application + "/DefaultIcon/.", icon},
        {Application + "/shell/open/command/.", command},
        {Document + "/.", "Pinloom PDF Document"},
        {Document + "/FriendlyTypeName", "Pinloom PDF Document"},
        {Document + "/DefaultIcon/.", icon},
        {Document + "/shell/open/command/.", command},
        {Document + "/Application/ApplicationName", "Pinloom"},
        {Document + "/Application/ApplicationIcon", icon},
        {Registration + "/Capabilities/ApplicationName", "Pinloom"},
        {Registration + "/Capabilities/ApplicationDescription", "Read PDF documents with Pinloom's paired reader."},
        {Registration + "/Capabilities/ApplicationIcon", icon},
        {RegisteredApplication, "Software\\Pinloom\\FileOpen\\Capabilities"},
        {OpenWith, QString()}
    };
    for (const auto &extension : pinloomReaderFileExtensions()) {
        values.insert(Application + "/SupportedTypes/." + extension, QString());
        values.insert(Registration + "/Capabilities/FileAssociations/." + extension, "Pinloom.PDF");
    }
    for (const auto &tree : OwnedTrees) {
        values.insert(tree + "/PinloomOwner", Owner);
        values.insert(tree + "/PinloomExecutable", path);
    }
    return values;
}

bool hasValues(QSettings &registry, const QString &tree)
{
    registry.beginGroup(tree);
    const bool result = !registry.allKeys().isEmpty();
    registry.endGroup();
    return result;
}

QString ownerError(const QString &tree)
{
    return QStringLiteral("File association registration is owned or changed by another application: %1. No changes were made.").arg(tree);
}

QString save(QSettings &registry, const QMap<QString, QVariant> &values, bool remove)
{
    QMap<QString, QVariant> previous;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        previous.insert(it.key(), registry.value(it.key()));
        if (remove) {
            // Preserve individual values subsequently changed by somebody else.
            if (registry.value(it.key()) == it.value()) registry.remove(it.key());
        } else registry.setValue(it.key(), it.value());
    }
    registry.sync();
    if (registry.status() == QSettings::NoError) return {};
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
        if (it.value().isValid()) registry.setValue(it.key(), it.value());
        else registry.remove(it.key());
    }
    registry.sync();
    return QStringLiteral("Could not save the current user's file associations. Check registry permissions and retry.");
}
}

QString registerPinloomFileAssociations(QSettings &registry, const QString &executable)
{
    registry.sync();
    if (registry.status() != QSettings::NoError)
        return QStringLiteral("Cannot read the current user's file associations.");
    const auto values = valuesFor(executable);
    for (const auto &tree : OwnedTrees) {
        if (!hasValues(registry, tree)) continue;
        if (registry.value(tree + "/PinloomOwner") != Owner
            || registry.value(tree + "/PinloomExecutable").toString().isEmpty()) return ownerError(tree);
        const auto previous = valuesFor(registry.value(tree + "/PinloomExecutable").toString());
        for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
            if (it.key().startsWith(tree + '/') && registry.contains(it.key())
                && registry.value(it.key()) != it.value()) return ownerError(it.key());
        }
    }
    for (const auto &key : {RegisteredApplication, OpenWith}) {
        if (registry.contains(key) && registry.value(key) != values.value(key)) return ownerError(key);
    }
    return save(registry, values, false);
}

QString unregisterPinloomFileAssociations(QSettings &registry, const QString &executable)
{
    registry.sync();
    if (registry.status() != QSettings::NoError)
        return QStringLiteral("Cannot read the current user's file associations.");
    bool owned = false;
    QString registeredExecutable = executable;
    for (const auto &tree : OwnedTrees) {
        if (!hasValues(registry, tree)) continue;
        if (registry.value(tree + "/PinloomOwner") != Owner) return ownerError(tree);
        if (registry.value(tree + "/PinloomExecutable").toString().compare(executablePath(executable), Qt::CaseInsensitive) != 0)
            return QStringLiteral("Another Pinloom package owns the current registration. Run Unregister File Associations from that package.");
        registeredExecutable = registry.value(tree + "/PinloomExecutable").toString();
        owned = true;
    }
    if (!owned) return {};
    const QString error = save(registry, valuesFor(registeredExecutable), true);
    if (!error.isEmpty()) return error;
    for (const auto &tree : OwnedTrees) {
        if (!hasValues(registry, tree)) registry.remove(tree);
    }
    registry.sync();
    return registry.status() == QSettings::NoError ? QString()
        : QStringLiteral("File associations were removed, but empty registration keys could not be cleaned up.");
}

void notifyPinloomFileAssociationsChanged()
{
#ifdef Q_OS_WIN
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
#endif
}
} // namespace Pinloom
