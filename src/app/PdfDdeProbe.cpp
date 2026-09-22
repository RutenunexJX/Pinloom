#include "pinloom/core/SumatraPdfDdeClient.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 4 || args[1] != QLatin1String("--request")) return 2;
    const QString command = args[2].trimmed();
    Pinloom::SumatraPdfDdeRequestResult result;
    if (command == QLatin1String("ProbeHealth()")) {
        result.text = QStringLiteral("ready");
    } else {
        if (command != QLatin1String("GetFileState()") && command != QLatin1String("GetMousePos()")) return 2;
        result = Pinloom::executeSumatraPdfDdeCommandInProbe(command, std::clamp(args[3].toInt(), 1, 3000));
    }
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 3;
    const auto json = QJsonDocument(QJsonObject{{QStringLiteral("text"), result.text},
                                               {QStringLiteral("error"), result.error}}).toJson(QJsonDocument::Compact);
    return output.write(json) == json.size() && output.flush() ? 0 : 3;
}
