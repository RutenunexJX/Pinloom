#pragma once

#include "pinloom/core/LibraryRepository.h"
#include "pinloom/core/ManualPdfAnchorCreation.h"

#include <QString>
#include <QStringList>

namespace Pinloom {

struct AnchorCaptureDraft {
    QString targetApp;
    QString targetFile;
    QString targetUri;
    QString locatorType;
    QString locatorJson;
    QString suggestedName;
    QStringList aliases;
    QStringList tags;
    bool pinned = false;
    QString provenance;
    bool mutationRequired = false;
    bool mutationOptional = false;
    bool mutationAuthorized = false;
    QString mutationKind;
    QString mutationLabel;
    QString mutationPayloadJson;

    bool isValid(QString *error = nullptr) const;
};

struct AnchorCaptureCommitResult {
    Resource resource;
    Anchor anchor;
    QString error;

    bool success() const;
};

AnchorCaptureDraft anchorCaptureDraftFromPdfRequest(
    const ManualPdfAnchorCreationRequest &request);

class AnchorCaptureCommitService {
public:
    explicit AnchorCaptureCommitService(ILibraryRepository &repository);

    AnchorCaptureCommitResult commit(const AnchorCaptureDraft &draft);

private:
    ILibraryRepository &repository_;
};

} // namespace Pinloom
