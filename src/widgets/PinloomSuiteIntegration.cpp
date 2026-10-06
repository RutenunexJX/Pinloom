#include "pinloom/widgets/PinloomSuiteIntegration.h"

#include <suiteapp/protocol.h>
#include <suiteapp/provider.h>
#include <suiteapp/runtime.h>

#include <QCoreApplication>
#include <QJsonArray>
#include <QTimer>

namespace Pinloom {

namespace {

constexpr auto kOpenAction = "pinloom.entry.open";
constexpr auto kCreateSourceAnchorAction = "pinloom.source-anchor.create";
constexpr auto kPreviewSurface = "pinloom.entry.preview";

std::optional<PinloomHostIdentity> identityFromParams(
    const QJsonObject& params)
{
    QString uri = params.value(QStringLiteral("resourceUri")).toString();
    if (uri.isEmpty())
        uri = params.value(QStringLiteral("uri")).toString();
    if (!uri.isEmpty())
        return pinloomHostIdentityFromUri(uri);
    const QJsonObject arguments =
        params.value(QStringLiteral("arguments")).toObject();
    return pinloomHostIdentityFromJson(
        arguments.value(QStringLiteral("identity")).toObject());
}

PinloomSourceAnchorRequest sourceAnchorRequest(const QJsonObject& object)
{
    PinloomSourceAnchorRequest request;
    request.title = object.value(QStringLiteral("title")).toString();
    request.content = object.value(QStringLiteral("content")).toString();
    request.workspaceRoot =
        object.value(QStringLiteral("workspaceRoot")).toString();
    request.relativeFilePath =
        object.value(QStringLiteral("relativeFilePath")).toString();
    request.absoluteFilePath =
        object.value(QStringLiteral("absoluteFilePath")).toString();
    request.moduleName =
        object.value(QStringLiteral("moduleName")).toString();
    request.startLine = object.value(QStringLiteral("startLine")).toInt();
    request.startColumn = object.value(QStringLiteral("startColumn")).toInt();
    request.endLine = object.value(QStringLiteral("endLine")).toInt();
    request.endColumn = object.value(QStringLiteral("endColumn")).toInt();
    request.selectedTextHash =
        object.value(QStringLiteral("selectedTextHash")).toString();
    request.prefixContext =
        object.value(QStringLiteral("prefixContext")).toString();
    request.suffixContext =
        object.value(QStringLiteral("suffixContext")).toString();
    return request;
}

} // namespace

PinloomSuiteIntegration::PinloomSuiteIntegration(
    PinloomHostBridgeCallbacks callbacks,
    QObject* parent)
    : QObject(parent)
    , callbacks_(std::move(callbacks))
{
}

PinloomSuiteIntegration::~PinloomSuiteIntegration() = default;

bool PinloomSuiteIntegration::start(QString* failureReason)
{
    return start(SuiteApp::RuntimeStartOptions{}, failureReason);
}

bool PinloomSuiteIntegration::start(
    const SuiteApp::RuntimeStartOptions& runtimeOptions,
    QString* failureReason)
{
    if (provider_ && provider_->isListening())
        return true;
    const SuiteApp::RuntimeStatus runtime =
        SuiteApp::ensureRuntime(runtimeOptions);
    if (!runtime.available) {
        if (failureReason)
            *failureReason = runtime.errorMessage;
        return false;
    }
    provider_ = std::make_unique<SuiteApp::Provider>(
        appDescriptor(QCoreApplication::applicationVersion()),
        [this](const QJsonObject& request) {
            return processRequest(request);
        },
        this);
    return provider_->start(runtimeOptions.endpoint, failureReason);
}

bool PinloomSuiteIntegration::isRegistered() const
{
    return provider_ && provider_->isListening();
}

QJsonObject PinloomSuiteIntegration::appDescriptor(
    const QString& version,
    const QString& endpoint)
{
    return {
        {QStringLiteral("appId"), QStringLiteral("pinloom")},
        {QStringLiteral("displayName"), QStringLiteral("Pinloom")},
        {QStringLiteral("version"), version.isEmpty()
             ? QStringLiteral("0.0.0") : version},
        {QStringLiteral("processId"),
         static_cast<double>(QCoreApplication::applicationPid())},
        {QStringLiteral("endpoint"), endpoint.isEmpty()
             ? SuiteApp::endpointForApp(QStringLiteral("pinloom"))
             : endpoint},
        {QStringLiteral("protocols"),
         QJsonArray{QString::fromLatin1(SuiteApp::kProtocol)}},
        {QStringLiteral("resourceSchemes"),
         QJsonArray{QStringLiteral("pinloom")}},
        {QStringLiteral("actions"),
         QJsonArray{
             QJsonObject{{QStringLiteral("id"),
                          QString::fromLatin1(kOpenAction)},
                         {QStringLiteral("resourceSchemes"),
                          QJsonArray{QStringLiteral("pinloom")}},
                         {QStringLiteral("sideEffect"), QStringLiteral("ui")}},
             QJsonObject{{QStringLiteral("id"),
                          QString::fromLatin1(kCreateSourceAnchorAction)},
                         {QStringLiteral("sideEffect"), QStringLiteral("write")}}
         }},
        {QStringLiteral("surfaces"),
         QJsonArray{QJsonObject{
             {QStringLiteral("id"), QString::fromLatin1(kPreviewSurface)},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")}}}},
    };
}

QJsonObject PinloomSuiteIntegration::processRequestForTesting(
    const QJsonObject& request)
{
    return processRequest(request);
}

QJsonObject PinloomSuiteIntegration::processRequest(
    const QJsonObject& request)
{
    const QString method = request.value(QStringLiteral("method")).toString();
    const QJsonObject params =
        request.value(QStringLiteral("params")).toObject();

    if (method == QStringLiteral("action.invoke")
        && params.value(QStringLiteral("actionId")).toString()
            == QString::fromLatin1(kCreateSourceAnchorAction)) {
        const PinloomSourceAnchorRequest anchorRequest =
            sourceAnchorRequest(
                params.value(QStringLiteral("arguments")).toObject());
        if (!anchorRequest.isValid() || !callbacks_.createSourceAnchor) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("invalid_source_anchor"),
                QStringLiteral("A valid source-anchor request is required"));
        }
        QString status;
        const std::optional<PinloomEntry> entry =
            callbacks_.createSourceAnchor(anchorRequest, &status);
        if (!entry.has_value()) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("source_anchor_create_failed"),
                status.isEmpty() ? QStringLiteral("Pinloom rejected the source anchor")
                                 : status);
        }
        return SuiteApp::successResponse(
            request,
            {{QStringLiteral("entry"), pinloomHostEntryToJson(*entry)},
             {QStringLiteral("status"), status}});
    }

    const std::optional<PinloomHostIdentity> identity =
        identityFromParams(params);
    if (!identity.has_value()) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("invalid_resource"),
            QStringLiteral("A valid pinloom://entry, pinloom://anchor, or pinloom://clip resource is required"));
    }
    if (!callbacks_.resolve) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("resolver_unavailable"),
            QStringLiteral("Pinloom resource resolution is unavailable"));
    }
    const std::optional<PinloomHostDocument> document =
        callbacks_.resolve(*identity);
    if (!document.has_value()) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("resource_not_found"),
            QStringLiteral("Pinloom entry no longer exists"));
    }
    if (document->entry.deleted) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("resource_deleted"),
            QStringLiteral("Pinloom entry is deleted; restore it before opening"));
    }
    const QJsonObject model = pinloomHostDocumentToJson(*document);
    if (method == QStringLiteral("resource.resolve"))
        return SuiteApp::successResponse(request, model);

    if (method == QStringLiteral("action.invoke")) {
        if (params.value(QStringLiteral("actionId")).toString()
                != QString::fromLatin1(kOpenAction)
            || !callbacks_.open) {
            return SuiteApp::errorResponse(
                request, QStringLiteral("action_not_supported"),
                QStringLiteral("Unknown or unavailable Pinloom action"));
        }
        const auto open = callbacks_.open;
        const PinloomHostIdentity target = *identity;
        QTimer::singleShot(0, this, [open, target] {
            QString ignoredStatus;
            open(target, &ignoredStatus);
        });
        return SuiteApp::successResponse(
            request, {{QStringLiteral("accepted"), true}});
    }

    if (params.value(QStringLiteral("surfaceId")).toString()
        != QString::fromLatin1(kPreviewSurface)) {
        return SuiteApp::errorResponse(
            request, QStringLiteral("surface_not_supported"),
            QStringLiteral("Unknown Pinloom surface"));
    }
    if (method == QStringLiteral("surface.describe")) {
        return SuiteApp::successResponse(
            request,
            {{QStringLiteral("surfaceId"), QString::fromLatin1(kPreviewSurface)},
             {QStringLiteral("mode"), QStringLiteral("model")},
             {QStringLiteral("fallback"), QStringLiteral("external")},
             {QStringLiteral("model"), model}});
    }
    if (method == QStringLiteral("surface.open") && callbacks_.open) {
        const auto open = callbacks_.open;
        const PinloomHostIdentity target = *identity;
        QTimer::singleShot(0, this, [open, target] {
            QString ignoredStatus;
            open(target, &ignoredStatus);
        });
        return SuiteApp::successResponse(
            request, {{QStringLiteral("accepted"), true}});
    }
    return SuiteApp::errorResponse(
        request, QStringLiteral("method_not_supported"),
        QStringLiteral("Pinloom does not implement this provider method"));
}

} // namespace Pinloom
