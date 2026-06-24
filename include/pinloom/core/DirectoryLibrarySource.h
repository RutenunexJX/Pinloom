#pragma once

#include "pinloom/core/LibrarySource.h"

#include <QFileInfo>

namespace Pinloom {

class DirectoryLibrarySource final : public ILibrarySource {
public:
    explicit DirectoryLibrarySource(QString rootPath);

    QString rootPath() const;
    QString displayName() const override;
    QList<Resource> scan(QString *errorMessage = nullptr) const override;

private:
    Resource resourceFromFileInfo(const QFileInfo &fileInfo) const;
    QList<Anchor> markdownAnchorsForFile(const QFileInfo &fileInfo) const;

    QString rootPath_;
};

} // namespace Pinloom
