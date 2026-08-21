#pragma once

#include "pinloom/core/ApplicationLaunchSettings.h"
#include "pinloom/core/ExcelCommand.h"
#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/PowerPointCommand.h"
#include "pinloom/core/SumatraPdfCommand.h"
#include "pinloom/core/VisioCommand.h"
#include "pinloom/core/WordCommand.h"
#include "pinloom/widgets/PinloomEntry.h"

#include <QObject>
#include <functional>

class QWidget;

namespace Pinloom {

struct PinloomOpenServiceOptions {
    std::function<bool(const PinloomOpenTarget &)> hostOpenHandler;
    std::function<bool(const QString &clipId, QString *error)> clipInsertionHandler;
    ApplicationLaunchSettings applicationLaunchSettings;
    std::function<bool(const ExcelJumpCommand &, QString *error)> excelLaunchHandler;
    std::function<bool(const VisioJumpCommand &, QString *error)> visioLaunchHandler;
    std::function<bool(const WordJumpCommand &, QString *error)> wordLaunchHandler;
    std::function<bool(const PowerPointJumpCommand &, QString *error)> powerPointLaunchHandler;
    std::function<QString()> sumatraPdfExecutablePathProvider;
    std::function<bool(const SumatraPdfCommand &, QString *error)> sumatraPdfLaunchHandler;
};

class PinloomOpenService final : public QObject {
public:
    PinloomOpenService(ILibraryRepository &repository,
                       PinloomOpenServiceOptions options,
                       QObject *parent = nullptr);

    bool open(const PinloomOpenTarget &target, QWidget *dialogParent = nullptr);
    QString statusText() const;

private:
    bool openExcel(const PinloomOpenTarget &target);
    bool openVisio(const PinloomOpenTarget &target);
    bool openWord(const PinloomOpenTarget &target);
    bool openPowerPoint(const PinloomOpenTarget &target);
    bool openSumatraPdf(const PinloomOpenTarget &target);
    void setStatus(const QString &status);
    void recordOpen(const PinloomOpenTarget &target);

    ILibraryRepository &repository_;
    PinloomOpenServiceOptions options_;
    QString statusText_;
};

} // namespace Pinloom
