#include "pinloom/widgets/AnchorLibraryWindow.h"
#include "pinloom/widgets/ClipLibraryWindow.h"
#include "pinloom/widgets/PinloomVisualTheme.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPushButton>
#include <QSaveFile>
#include <QTest>
#include <algorithm>

using namespace Pinloom;

class UiPerformanceTest : public QObject {
    Q_OBJECT
    int paints_ = 0, layouts_ = 0, resizes_ = 0;
    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::Paint) ++paints_;
        if (event->type() == QEvent::LayoutRequest) ++layouts_;
        if (event->type() == QEvent::Resize) ++resizes_;
        return false;
    }
    template<class Window, class SetQuery>
    QJsonObject measure(Window &window, const QString &name, SetQuery setQuery) {
        window.show();
        QTest::qWait(100);
        window.installEventFilter(this);
        for (auto *child : window.template findChildren<QWidget *>()) child->installEventFilter(this);
        paints_ = layouts_ = resizes_ = 0;
        QList<double> times;
        for (int i = 0; i < 12; ++i) {
            QElapsedTimer elapsed;
            elapsed.start();
            setQuery(i % 2 ? QStringLiteral("Record") : QStringLiteral("Record 1"));
            times.append(elapsed.nsecsElapsed() / 1000000.0);
            QCoreApplication::processEvents();
        }
        std::sort(times.begin(), times.end());
        return {{"surface", name}, {"records", 2000}, {"samples", times.size()},
                {"width", window.width()}, {"height", window.height()},
                {"dpr", window.devicePixelRatioF()}, {"dispatchMedianMs", times.at(6)},
                {"dispatchP95Ms", times.last()}, {"paintEvents", paints_},
                {"layoutEvents", layouts_}, {"resizeEvents", resizes_}};
    }
private slots:
    void libraryFiltering() {
        qApp->setFont(QFont(QStringLiteral("Segoe UI"), 9));
        applyPinloomVisualTheme(*qApp, PinloomVisualScheme::Light);
        QList<AnchorLibraryFile> files;
        QList<Clip> clips;
        for (int i = 0; i < 2000; ++i) {
            AnchorLibraryFile file;
            file.resource.id = QStringLiteral("resource-%1").arg(i);
            file.resource.title = QStringLiteral("Record %1").arg(i);
            file.resource.location = QStringLiteral("E:/Pinloom-benchmark-fixture/%1.txt").arg(i);
            file.resource.aliases = {QStringLiteral("alias-%1").arg(i)};
            file.resource.tags = {QStringLiteral("benchmark"), QStringLiteral("tag-%1").arg(i % 20)};
            for (int j = 0; j < 2; ++j) {
                Anchor anchor;
                anchor.id = QStringLiteral("anchor-%1-%2").arg(i).arg(j);
                anchor.name = QStringLiteral("Point %1-%2").arg(i).arg(j);
                file.anchors.append({file.resource.id, anchor});
            }
            files.append(file);
            Clip clip;
            clip.id = QStringLiteral("clip-%1").arg(i);
            clip.name = file.resource.title;
            clip.tags = file.resource.tags;
            clip.text = QString(2000, QLatin1Char('x'));
            clip.state = ClipState::Saved;
            clips.append(clip);
        }
        QJsonArray results;
        for (const auto &size : {QSize(1000, 700), QSize(1600, 900)}) {
            AnchorLibraryWindowOptions anchorOptions;
            anchorOptions.filesProvider = [&] { return files; };
            AnchorLibraryWindow anchors(anchorOptions);
            anchors.resize(size);
            results.append(measure(anchors, QStringLiteral("anchors"), [&](const QString &q) { anchors.setFilterText(q); }));
            QCOMPARE(anchors.visibleFileCount(), 2000);
            anchors.close();
            ClipLibraryWindowOptions clipOptions;
            clipOptions.clipsProvider = [&] { return clips; };
            ClipLibraryWindow clipWindow(clipOptions);
            clipWindow.resize(size);
            results.append(measure(clipWindow, QStringLiteral("clips"), [&](const QString &q) { clipWindow.setSearchText(q); }));
            QCOMPARE(clipWindow.visibleClipCount(), 2000);
            clipWindow.close();
        }
        QJsonObject report{{"method", "Qt offscreen CPU dispatch and event counts; not display FPS"}, {"results", results}};
        const auto bytes = QJsonDocument(report).toJson();
        qInfo().noquote() << bytes;
        const auto output = qEnvironmentVariable("PINLOOM_PERFORMANCE_OUTPUT");
        if (!output.isEmpty()) {
            QSaveFile file(output);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
            QVERIFY(file.commit());
        }
    }
};
QTEST_MAIN(UiPerformanceTest)
#include "ui_performance_test.moc"
