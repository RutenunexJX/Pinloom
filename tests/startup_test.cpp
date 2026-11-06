#include "pinloom/core/PinloomStartup.h"
#include "pinloom/core/PinloomFileAssociations.h"
#include "pinloom/widgets/NativePdfReader.h"

#include <QDir>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

using namespace Pinloom;

namespace {
const QString Command = "Software/Classes/Applications/pinloom_app.exe/shell/open/command/.";
const QString Choice = "Software/Microsoft/Windows/CurrentVersion/Explorer/FileExts/.pdf/UserChoice/ProgId";
const QString Other = "Software/Classes/.pdf/OpenWithProgids/Other.Reader";

struct RegistryFixture {
    QTemporaryDir directory;
    QSettings settings;
    RegistryFixture()
#ifdef Q_OS_WIN
        : settings("HKEY_CURRENT_USER\\Software\\PinloomFileOpenTests\\" + QUuid::createUuid().toString(QUuid::Id128),
                   QSettings::NativeFormat)
#else
        : settings(directory.filePath("registry.ini"), QSettings::IniFormat)
#endif
    { settings.setFallbacksEnabled(false); }
    ~RegistryFixture() { settings.clear(); settings.sync(); }
};
}

class StartupTest : public QObject {
    Q_OBJECT
private slots:
    void noFilesKeepsManagementAndDeepLinks()
    {
        const auto empty = parsePinloomStartup({"pinloom_app.exe"}, QDir::tempPath());
        QVERIFY(empty.error.isEmpty());
        QCOMPARE(empty.action, PinloomStartupAction::Manage);
        const auto resident = parsePinloomStartup({"app", "--hidden", "--no-global-hooks", "--library", "isolated",
            "pinloom://entry/example"}, QDir::tempPath());
        QVERIFY(resident.error.isEmpty());
        QVERIFY(resident.hidden);
        QVERIFY(resident.noGlobalHooks);
        QCOMPARE(resident.action, PinloomStartupAction::Manage);
        QCOMPARE(resident.library, QDir::temp().filePath("isolated"));
        QCOMPARE(resident.deepLink, "pinloom://entry/example");
        QCOMPARE(parsePinloomStartup({"app", "--package-check"}, ".").action, PinloomStartupAction::PackageCheck);
    }
    void preservesFileArgumentsAndNormalizesBeforeDispatch()
    {
        QTemporaryDir directory;
        const QStringList names{"中文 路径 (1)&;'%!#$.pdf", "--hidden.pdf", "-platform.pdf"};
        QStringList arguments{"app", "--library", "library", "--"};
        arguments.append(names);
        const auto parsed = parsePinloomStartup(arguments, directory.path());
        QVERIFY2(parsed.error.isEmpty(), qPrintable(parsed.error));
        QCOMPARE(parsed.action, PinloomStartupAction::OpenFiles);
        QCOMPARE(parsed.files.size(), names.size());
        for (int i = 0; i < names.size(); ++i) QCOMPARE(parsed.files[i], directory.filePath(names[i]));
        QVERIFY(!parsed.hidden);
        QCOMPARE(parsed.library, directory.filePath("library"));
        const auto absolute = parsePinloomStartup({"app", parsed.files[0], parsed.files[1]}, QDir::rootPath());
        QCOMPARE(absolute.files, parsed.files.mid(0, 2));
    }
    void rejectsAmbiguousRequests()
    {
        for (const QStringList arguments : {
            QStringList{"app", "--library"}, {"app", "--library", "--hidden"}, {"app", "--"},
            {"app", "--bad.pdf"}, {"app", "--register-file-associations", "file.pdf"},
            {"app", "pinloom://entry/a", "a.pdf"}, {"app", "https://example.test/a.pdf"},
            {"app", "--package-check", "--unregister-file-associations"},
            {"app", "--library", "one", "--library", "two"}}) {
            QVERIFY2(!parsePinloomStartup(arguments, QDir::tempPath()).error.isEmpty(), qPrintable(arguments.join(' ')));
        }
    }
    void validatesWholeFileRequestBeforeOpening()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath("中文.PDF"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%PDF-1.7\n");
        file.close();
        QVERIFY(validateNativePdfFiles({file.fileName()}).isEmpty());
        QVERIFY(validateNativePdfFiles({file.fileName(), directory.filePath("missing.pdf")}).contains("does not exist"));
        QVERIFY(validateNativePdfFiles({directory.filePath("not-supported.epub")}).contains("Unsupported"));
        QVERIFY(!validateNativePdfFiles({}).isEmpty());
        QCOMPARE(pinloomReaderFileExtensions(), QStringList{"pdf"});
    }
    void registersMovesAndUnregistersWithoutChangingDefaults()
    {
        RegistryFixture fixture;
        auto &registry = fixture.settings;
        registry.setValue(Choice, "Other.Reader");
        registry.setValue(Other, "");
        const QString first = QDir::temp().filePath("first folder/中文/pinloom_app.exe");
        const QString second = QDir::temp().filePath("moved folder/pinloom_app.exe");
        QString error = registerPinloomFileAssociations(registry, first);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QCOMPARE(registry.value(Command).toString(), QStringLiteral("\"%1\" -- \"%2\"").arg(QDir::toNativeSeparators(first), "%1"));
        QCOMPARE(registry.value("Software/Classes/Applications/pinloom_app.exe/FriendlyAppName").toString(), "Pinloom");
        QVERIFY(registry.contains("Software/Classes/Applications/pinloom_app.exe/SupportedTypes/.pdf"));
        QVERIFY(!registry.contains("Software/Classes/Applications/pinloom_app.exe/SupportedTypes/.epub"));
        error = registerPinloomFileAssociations(registry, first);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        error = registerPinloomFileAssociations(registry, second);
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(registry.value(Command).toString().contains(QDir::toNativeSeparators(second)));
        QVERIFY(!unregisterPinloomFileAssociations(registry, first).isEmpty());
        error = unregisterPinloomFileAssociations(registry, second.toUpper());
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY(unregisterPinloomFileAssociations(registry, second).isEmpty());
        QVERIFY(!registry.contains(Command));
        QVERIFY(!registry.contains("Software/Classes/.pdf/OpenWithProgids/Pinloom.PDF"));
        QVERIFY(!registry.contains("Software/RegisteredApplications/Pinloom"));
        QCOMPARE(registry.value(Choice).toString(), "Other.Reader");
        QVERIFY(registry.contains(Other));
    }
    void refusesCollisionsAndPreservesForeignValues()
    {
        RegistryFixture fixture;
        auto &registry = fixture.settings;
        registry.setValue(Command, "other-program");
        const auto keys = registry.allKeys();
        QVERIFY(!registerPinloomFileAssociations(registry, "pinloom_app.exe").isEmpty());
        QCOMPARE(registry.allKeys(), keys);
        QVERIFY(!unregisterPinloomFileAssociations(registry, "pinloom_app.exe").isEmpty());
        QCOMPARE(registry.value(Command).toString(), "other-program");
    }
    void unregisterPreservesValuesChangedAfterRegistration()
    {
        RegistryFixture fixture;
        auto &registry = fixture.settings;
        const QString executable = QDir::temp().filePath("pinloom_app.exe");
        QVERIFY(registerPinloomFileAssociations(registry, executable).isEmpty());
        registry.setValue(Command, "external-change");
        registry.setValue("Software/Classes/Pinloom.PDF/ForeignValue", "keep");
        QVERIFY(!registerPinloomFileAssociations(registry, executable).isEmpty());
        QVERIFY(unregisterPinloomFileAssociations(registry, executable).isEmpty());
        QCOMPARE(registry.value(Command).toString(), "external-change");
        QCOMPARE(registry.value("Software/Classes/Pinloom.PDF/ForeignValue").toString(), "keep");
    }
};

QTEST_GUILESS_MAIN(StartupTest)
#include "startup_test.moc"
