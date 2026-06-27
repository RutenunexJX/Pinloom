#pragma once

#include "pinloom/core/LibrarySource.h"

#include <QByteArray>
#include <QFileInfo>
#include <QUrl>
#include <functional>
#include <optional>

namespace Pinloom {

class DirectoryLibrarySource final : public ILibrarySource {
public:
    struct WebPageFetchResult {
        QUrl finalUrl;
        QByteArray body;
        QString contentType;
    };

    using WebPageFetcher = std::function<std::optional<WebPageFetchResult>(const QUrl &url, QString *errorMessage)>;

    explicit DirectoryLibrarySource(QString rootPath);

    QString rootPath() const;
    QString displayName() const override;
    QList<Resource> scan(QString *errorMessage = nullptr) const override;
    void setRemoteWebFetchingEnabled(bool enabled);
    bool remoteWebFetchingEnabled() const;
    void setWebPageFetcher(WebPageFetcher fetcher);

private:
    QList<Resource> resourcesFromFileInfo(const QFileInfo &fileInfo) const;
    Resource resourceFromFileInfo(const QFileInfo &fileInfo) const;
    QList<Resource> bookmarkResourcesFromHtmlFile(const QFileInfo &fileInfo) const;
    QList<Resource> browserBookmarkResourcesFromJsonFile(const QFileInfo &fileInfo) const;
    QList<Resource> opmlResourcesFromFile(const QFileInfo &fileInfo) const;
    QList<Resource> feedResourcesFromXmlFile(const QFileInfo &fileInfo) const;
    QList<Resource> sitemapResourcesFromXmlFile(const QFileInfo &fileInfo) const;
    void applyMarkdownMetadata(Resource &resource, const QFileInfo &fileInfo) const;
    void applyPdfMetadata(Resource &resource, const QFileInfo &fileInfo) const;
    void applyUrlMetadata(Resource &resource, const QFileInfo &fileInfo) const;
    void applyHtmlMetadata(Resource &resource, const QFileInfo &fileInfo) const;
    void applyPlainTextMetadata(Resource &resource, const QFileInfo &fileInfo) const;

    QString rootPath_;
    bool remoteWebFetchingEnabled_ = false;
    WebPageFetcher webPageFetcher_;
};

} // namespace Pinloom
