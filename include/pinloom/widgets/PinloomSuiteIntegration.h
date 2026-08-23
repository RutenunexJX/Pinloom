#pragma once

#include "pinloom/widgets/PinloomHostBridge.h"

#include <QJsonObject>
#include <QObject>

#include <memory>

namespace SuiteApp {
class Provider;
}

namespace Pinloom {

class PinloomSuiteIntegration final : public QObject {
public:
    explicit PinloomSuiteIntegration(
        PinloomHostBridgeCallbacks callbacks,
        QObject* parent = nullptr);
    ~PinloomSuiteIntegration() override;

    bool start(QString* failureReason = nullptr);
    bool isRegistered() const;

    static QJsonObject appDescriptor(const QString& version,
                                     const QString& endpoint = {});
    QJsonObject processRequestForTesting(const QJsonObject& request);

private:
    QJsonObject processRequest(const QJsonObject& request);

    PinloomHostBridgeCallbacks callbacks_;
    std::unique_ptr<SuiteApp::Provider> provider_;
};

} // namespace Pinloom
