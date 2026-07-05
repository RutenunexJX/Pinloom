#include "pinloom/core/SqliteLibraryRepository.h"
#include "pinloom/core/ExplorerFileSelection.h"
#include "pinloom/core/InboxFileCapture.h"
#include "pinloom/core/PdfXChangeForegroundCapture.h"
#include "pinloom/widgets/ClipResidentHost.h"
#include "pinloom/widgets/MainPanelHotkey.h"
#include "pinloom/widgets/PinloomCommandPanel.h"
#include "pinloom/widgets/PinloomMainWindow.h"
#include "pinloom/widgets/PinloomPanel.h"

#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDebug>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFormLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QStandardPaths>
#include <algorithm>
#include <memory>
#include <optional>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Pinloom"));
    QApplication::setOrganizationName(QStringLiteral("Pinloom"));
    app.setQuitOnLastWindowClosed(false);

    QString appDataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataPath.isEmpty()) {
        appDataPath = QDir::home().filePath(QStringLiteral(".pinloom"));
    }
    if (!QDir().mkpath(appDataPath)) {
        QMessageBox::critical(nullptr, QStringLiteral("Pinloom"), QStringLiteral("Unable to create app data directory."));
        return 1;
    }

    Pinloom::SqliteLibraryRepository repository;
    const QString databasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom.sqlite3"));
    if (!repository.open(databasePath) || !repository.initialize()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Pinloom"),
                              QStringLiteral("Unable to initialize Pinloom database:\n%1").arg(repository.lastError()));
        return 1;
    }

    Pinloom::ClipResidentRuntimeFactory clipFactory;
    Pinloom::ClipResidentRuntimeFactoryOptions clipOptions;
    clipOptions.repositoryKind = Pinloom::ClipResidentRepositoryKind::SQLite;
    clipOptions.sqliteDatabasePath = QDir(appDataPath).filePath(QStringLiteral("pinloom_clip.sqlite3"));
    clipOptions.runtimeOptions.pickerSearchOptions.includeTemporary = true;
    clipOptions.runtimeOptions.registerHotkeyOnStart = false;

    Pinloom::ClipResidentHostResult clipHostResult = clipFactory.createDefaultPlatformHost(clipOptions);
    std::unique_ptr<Pinloom::ClipResidentHost> clipHost;
    if (clipHostResult.succeeded()) {
        clipHost = std::move(clipHostResult.host);
        QObject::connect(clipHost.get(), &Pinloom::ClipResidentHost::quitRequested, &app, &QApplication::quit);
    } else {
        QMessageBox::warning(nullptr,
                             QStringLiteral("Pinloom Clip"),
                             QStringLiteral("Pinloom Clip could not initialize:\n%1").arg(clipHostResult.error));
    }

    Pinloom::PinloomMainWindow window;
    window.setWindowTitle(QStringLiteral("Pinloom"));
    window.setMinimumWidth(560);
    window.resize(760, 72);

    Pinloom::PinloomPanelOptions panelOptions;
    panelOptions.clipSearchHandler =
        [&clipHost](const QString &query, const Pinloom::ClipSearchOptions &options) -> QList<Pinloom::ClipSearchResult> {
        if (!clipHost || !clipHost->runtime()) {
            return {};
        }
        return clipHost->runtime()->searchService().search(query, options);
    };
    panelOptions.clipInsertionHandler = [&clipHost](const QString &clipId, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        const Pinloom::ClipInsertionResult result = clipHost->runtime()->insertionService().insertClip(clipId);
        if (!result.inserted() && error) {
            *error = result.error;
        }
        return result.inserted();
    };
    auto clipSaveHandler = [&clipHost](const Pinloom::PinloomClipSaveRequest &request, QString *error) {
        if (!clipHost || !clipHost->runtime()) {
            if (error) {
                *error = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!clipHost->runtime()->searchService().saveClip(request.clipId,
                                                           request.name,
                                                           request.aliases,
                                                           request.tags,
                                                           request.pinned)) {
            if (error) {
                const QString repositoryError = clipHost->runtime()->searchService().lastError().trimmed();
                *error = repositoryError.isEmpty()
                    ? QStringLiteral("Unable to save clip")
                    : repositoryError;
            }
            return false;
        }
        if (error) {
            error->clear();
        }
        return true;
    };

    Pinloom::ForegroundAppWindowContext lastForegroundContext;
    QMainWindow *commandWindowForForegroundCapture = nullptr;
    Pinloom::PdfXChangeForegroundCaptureProvider foregroundPdfCaptureProvider(repository);

    panelOptions.foregroundPdfAnchorCaptureRequestProvider =
        [&foregroundPdfCaptureProvider, &lastForegroundContext, &commandWindowForForegroundCapture](QString *status)
        -> std::optional<Pinloom::ManualPdfAnchorCreationRequest> {
        const bool useLastForegroundContext =
            commandWindowForForegroundCapture
            && commandWindowForForegroundCapture->isVisible()
            && lastForegroundContext.isValid();
        const Pinloom::ForegroundAppWindowContext context =
            useLastForegroundContext
                ? lastForegroundContext
                : Pinloom::currentForegroundAppWindowContext();
        const Pinloom::PdfXChangeForegroundCaptureResult result =
            foregroundPdfCaptureProvider.capture(context);
        if (status) {
            *status = result.status;
        }
        if (!result.success()) {
            return std::nullopt;
        }
        return result.request;
    };
    auto *panel = new Pinloom::PinloomPanel(repository, panelOptions, &window);
    window.setCentralWidget(panel);

    QMainWindow commandWindow;
    commandWindowForForegroundCapture = &commandWindow;
    commandWindow.setWindowTitle(QStringLiteral("Pinloom Command"));
    commandWindow.setMinimumWidth(560);
    commandWindow.resize(760, 300);

    Pinloom::PinloomCommandPanelOptions commandOptions;
    commandOptions.unifiedEntrySearchHandler = [panel](const QString &query) {
        panel->setSearchText(query);
        return panel->currentEntries();
    };
    commandOptions.unifiedSearchHandler = [panel](const QString &query) {
        panel->setSearchText(query);
        return panel->currentResults();
    };
    const auto activateOpenTargetFromPanel =
        [panel](const Pinloom::PinloomOpenTarget &target, QString *status) {
        const bool activated = panel->activateOpenTarget(target);
        if (status) {
            *status = panel->statusText();
        }
        return activated;
    };
    commandOptions.anchorJumpHandler = activateOpenTargetFromPanel;
    commandOptions.resourceOpenHandler = activateOpenTargetFromPanel;
    commandOptions.clipSearchHandler = panelOptions.clipSearchHandler;
    commandOptions.clipInsertionHandler = panelOptions.clipInsertionHandler;
    commandOptions.clipSaveHandler = clipSaveHandler;
    commandOptions.anchorCaptureHandler = [panel](QString *status) {
        const bool captured = panel->captureForegroundPdfAnchor();
        if (status) {
            *status = panel->statusText();
        }
        return captured;
    };
    commandOptions.inboxSelectionProvider =
        [&lastForegroundContext, &commandWindowForForegroundCapture](QString *status) -> QStringList {
        const bool useLastForegroundContext =
            commandWindowForForegroundCapture
            && commandWindowForForegroundCapture->isVisible()
            && lastForegroundContext.isValid();
        const Pinloom::ForegroundAppWindowContext context =
            useLastForegroundContext
                ? lastForegroundContext
                : Pinloom::currentForegroundAppWindowContext();
        const Pinloom::ExplorerFileSelectionResult result =
            Pinloom::captureExplorerFileSelection(context);
        if (status) {
            *status = result.status;
        }
        return result.filePaths;
    };
    commandOptions.inboxSaveHandler = [&repository](const Pinloom::InboxFileSaveRequest &request, QString *status) {
        const Pinloom::InboxFileSaveResult result = Pinloom::saveInboxFile(repository, request);
        if (status) {
            *status = result.status;
        }
        return result.success();
    };
    commandOptions.searchWindowHandler = [&window, panel](const QString &query) {
        Pinloom::showMainPanelForHotkey(window, *panel);
        const QString trimmedQuery = query.trimmed();
        if (!trimmedQuery.isEmpty()) {
            panel->setSearchText(trimmedQuery);
        }
    };
    const auto targetTitle = [](const Pinloom::PinloomOpenTarget &target) {
        if (target.anchor.has_value()) {
            const QString anchorName = target.anchor->name.trimmed();
            if (!anchorName.isEmpty()) {
                return anchorName;
            }
            const QString anchorTarget = target.anchor->target.trimmed();
            if (!anchorTarget.isEmpty()) {
                return anchorTarget;
            }
        }
        if (!target.title.trimmed().isEmpty()) {
            return target.title.trimmed();
        }
        if (!target.location.trimmed().isEmpty()) {
            return target.location.trimmed();
        }
        return target.clipId.trimmed();
    };
    const auto cleanTag = [](QString tag) {
        tag = tag.trimmed();
        while (tag.startsWith(QLatin1Char('#'))) {
            tag.remove(0, 1);
            tag = tag.trimmed();
        }
        return tag;
    };
    const auto appendUniqueValue = [](QStringList &values, const QString &value) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !values.contains(trimmed, Qt::CaseInsensitive)) {
            values.append(trimmed);
        }
    };
    const auto valuesFromCommaText = [&appendUniqueValue, &cleanTag](const QString &text, bool tags) {
        QStringList values;
        for (const QString &value : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
            appendUniqueValue(values, tags ? cleanTag(value) : value);
        }
        return values;
    };
    struct MetadataEdit {
        QString name;
        QStringList aliases;
        QStringList tags;
        bool pinned = false;
    };
    const auto promptMetadataEdit =
        [&valuesFromCommaText](QWidget *parent,
                               const QString &windowTitle,
                               const QString &name,
                               const QStringList &aliases,
                               const QStringList &tags,
                               bool pinned) -> std::optional<MetadataEdit> {
        QDialog dialog(parent);
        dialog.setWindowTitle(windowTitle);
        auto *form = new QFormLayout(&dialog);
        auto *nameEdit = new QLineEdit(name, &dialog);
        auto *aliasesEdit = new QLineEdit(aliases.join(QStringLiteral(", ")), &dialog);
        auto *tagsEdit = new QLineEdit(tags.join(QStringLiteral(", ")), &dialog);
        auto *pinnedCheck = new QCheckBox(QStringLiteral("Pinned"), &dialog);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        pinnedCheck->setChecked(pinned);
        form->addRow(QStringLiteral("Name"), nameEdit);
        form->addRow(QStringLiteral("Aliases"), aliasesEdit);
        form->addRow(QStringLiteral("Tags"), tagsEdit);
        form->addRow(QString(), pinnedCheck);
        form->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        MetadataEdit edit;
        edit.name = nameEdit->text().trimmed();
        edit.aliases = valuesFromCommaText(aliasesEdit->text(), false);
        edit.tags = valuesFromCommaText(tagsEdit->text(), true);
        edit.pinned = pinnedCheck->isChecked();
        return edit;
    };
    const auto findClip = [&clipHost](const QString &clipId) -> std::optional<Pinloom::Clip> {
        if (!clipHost || !clipHost->runtime()) {
            return std::nullopt;
        }
        return clipHost->runtime()->searchService().findClip(clipId);
    };
    const auto saveClipMetadata =
        [&clipHost](const Pinloom::Clip &clip,
                    const QString &name,
                    const QStringList &aliases,
                    const QStringList &tags,
                    bool pinned,
                    QString *status) {
        if (!clipHost || !clipHost->runtime()) {
            if (status) {
                *status = QStringLiteral("Pinloom Clip is not running");
            }
            return false;
        }
        if (!clipHost->runtime()->searchService().saveClip(clip.id, name, aliases, tags, pinned)) {
            if (status) {
                const QString error = clipHost->runtime()->searchService().lastError().trimmed();
                *status = error.isEmpty() ? QStringLiteral("Unable to update clip") : error;
            }
            return false;
        }
        return true;
    };
    const auto selectPanelTarget = [panel](const Pinloom::PinloomOpenTarget &target) {
        if (target.resultRow >= 0 && panel->selectResultAt(target.resultRow)) {
            const Pinloom::PinloomOpenTarget current = panel->currentOpenTarget();
            if (!target.clipId.isEmpty()) {
                return current.clipId == target.clipId;
            }
            if (current.resourceId == target.resourceId) {
                const QString currentAnchorId = current.anchor.has_value() ? current.anchor->id : QString();
                const QString targetAnchorId = target.anchor.has_value() ? target.anchor->id : QString();
                return targetAnchorId.isEmpty() || currentAnchorId == targetAnchorId;
            }
        }
        return !target.resourceId.isEmpty() && panel->selectResultResource(target.resourceId);
    };
    commandOptions.unifiedActionProvider =
        [&repository, &findClip](const Pinloom::PinloomOpenTarget &target) {
        QList<Pinloom::PinloomCommandResultAction> actions;
        const auto addAction = [&actions](const QString &id,
                                          const QString &label,
                                          const QString &detail,
                                          bool enabled = true,
                                          const QString &disabledReason = QString()) {
            Pinloom::PinloomCommandResultAction action;
            action.id = id;
            action.label = label;
            action.detail = detail;
            action.enabled = enabled;
            action.disabledReason = disabledReason;
            actions.append(action);
        };

        if (!target.clipId.trimmed().isEmpty()) {
            const std::optional<Pinloom::Clip> clip = findClip(target.clipId);
            const bool savedClip = clip.has_value() && clip->state == Pinloom::ClipState::Saved;
            addAction(QStringLiteral("primary"), QStringLiteral("Insert"), QStringLiteral("Insert this Saved Clip"));
            addAction(clip.has_value() && clip->pinned ? QStringLiteral("unpin") : QStringLiteral("pin"),
                      clip.has_value() && clip->pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"),
                      QStringLiteral("Change Saved Clip pinned state"),
                      savedClip,
                      QStringLiteral("Pin requires a Saved Clip"));
            addAction(QStringLiteral("add_alias"),
                      QStringLiteral("Add alias"),
                      QStringLiteral("Add an alias to this Saved Clip"),
                      savedClip,
                      QStringLiteral("Aliases require a Saved Clip"));
            addAction(QStringLiteral("add_tag"),
                      QStringLiteral("Add tag"),
                      QStringLiteral("Add a tag to this Saved Clip"),
                      savedClip,
                      QStringLiteral("Tags require a Saved Clip"));
            addAction(QStringLiteral("edit_metadata"),
                      QStringLiteral("Edit name/metadata"),
                      QStringLiteral("Edit Saved Clip name, aliases, tags, and pinned state"),
                      savedClip,
                      QStringLiteral("Metadata editing requires a Saved Clip"));
            addAction(QStringLiteral("remove"),
                      QStringLiteral("Delete / Remove"),
                      QStringLiteral("Archive this Saved Clip inside Pinloom"),
                      savedClip,
                      QStringLiteral("Delete requires a Saved Clip"));
            return actions;
        }

        if (target.anchor.has_value()) {
            const bool pinned = target.anchor->pinned;
            addAction(QStringLiteral("primary"), QStringLiteral("Jump"), QStringLiteral("Jump to this anchor"));
            addAction(pinned ? QStringLiteral("unpin") : QStringLiteral("pin"),
                      pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"),
                      QStringLiteral("Change anchor pinned state"));
            addAction(QStringLiteral("add_alias"), QStringLiteral("Add alias"), QStringLiteral("Add an alias to this anchor"));
            addAction(QStringLiteral("add_tag"), QStringLiteral("Add tag"), QStringLiteral("Add a tag to this anchor"));
            addAction(QStringLiteral("edit_metadata"),
                      QStringLiteral("Edit name/metadata"),
                      QStringLiteral("Edit anchor name, aliases, tags, and pinned state"));
            addAction(QStringLiteral("remove"),
                      QStringLiteral("Delete / Remove"),
                      QStringLiteral("Delete this Pinloom anchor without deleting the target file"));
            return actions;
        }

        const std::optional<Pinloom::Resource> resource = target.resourceId.trimmed().isEmpty()
            ? std::nullopt
            : repository.findResource(target.resourceId);
        const std::optional<Pinloom::ResourceUsage> usage = target.resourceId.trimmed().isEmpty()
            ? std::nullopt
            : repository.resourceUsage(target.resourceId);
        const bool pinned = usage.has_value() && usage->pinned;
        const bool hasResource = resource.has_value();
        addAction(QStringLiteral("primary"),
                  QStringLiteral("Open"),
                  Pinloom::isInboxResourceId(target.resourceId)
                      ? QStringLiteral("Open this Inbox file")
                      : QStringLiteral("Open this file/resource"));
        addAction(pinned ? QStringLiteral("unpin") : QStringLiteral("pin"),
                  pinned ? QStringLiteral("Unpin") : QStringLiteral("Pin"),
                  QStringLiteral("Change resource pinned state"),
                  hasResource,
                  QStringLiteral("Pin requires a saved resource"));
        addAction(QStringLiteral("add_alias"),
                  QStringLiteral("Add alias"),
                  QStringLiteral("Add an alias to this resource"),
                  hasResource,
                  QStringLiteral("Aliases require a saved resource"));
        addAction(QStringLiteral("add_tag"),
                  QStringLiteral("Add tag"),
                  QStringLiteral("Add a tag to this resource"),
                  hasResource,
                  QStringLiteral("Tags require a saved resource"));
        addAction(QStringLiteral("edit_metadata"),
                  QStringLiteral("Edit name/metadata"),
                  QStringLiteral("Edit resource name, aliases, tags, and pinned state"),
                  hasResource,
                  QStringLiteral("Metadata editing requires a saved resource"));
        addAction(QStringLiteral("remove"),
                  QStringLiteral("Delete / Remove"),
                  Pinloom::isInboxResourceId(target.resourceId)
                      ? QStringLiteral("Remove this Inbox file from Pinloom without deleting the original file")
                      : QStringLiteral("Remove this resource from Pinloom without deleting the original file"),
                  hasResource,
                  QStringLiteral("Remove requires a saved resource"));
        return actions;
    };
    commandOptions.unifiedActionHandler =
        [&repository,
         &clipHost,
         panel,
         &targetTitle,
         &cleanTag,
         &appendUniqueValue,
         &promptMetadataEdit,
         &findClip,
         &saveClipMetadata,
         &selectPanelTarget,
         &activateOpenTargetFromPanel](QWidget *parent,
                                        const Pinloom::PinloomOpenTarget &target,
                                        const Pinloom::PinloomCommandResultAction &action,
                                        QString *status) {
        const QString actionId = action.id.trimmed();
        if (actionId == QLatin1String("primary")) {
            return activateOpenTargetFromPanel(target, status);
        }

        if (!target.clipId.trimmed().isEmpty()) {
            const std::optional<Pinloom::Clip> clip = findClip(target.clipId);
            if (!clip.has_value() || clip->state != Pinloom::ClipState::Saved) {
                if (status) {
                    *status = QStringLiteral("Action requires a Saved Clip");
                }
                return false;
            }

            if (actionId == QLatin1String("remove")) {
                const QMessageBox::StandardButton choice = QMessageBox::question(
                    parent,
                    QStringLiteral("Delete Saved Clip"),
                    QStringLiteral("Remove \"%1\" from ordinary Pinloom search?\n\n"
                                   "This archives the Saved Clip inside Pinloom and does not delete files or external clipboard data.")
                        .arg(targetTitle(target)));
                if (choice != QMessageBox::Yes) {
                    if (status) {
                        *status = QStringLiteral("Remove canceled");
                    }
                    return false;
                }
                if (!clipHost || !clipHost->runtime()
                    || !clipHost->runtime()->searchService().softDeleteClip(clip->id)) {
                    if (status) {
                        const QString error = clipHost && clipHost->runtime()
                            ? clipHost->runtime()->searchService().lastError().trimmed()
                            : QString();
                        *status = error.isEmpty() ? QStringLiteral("Unable to remove Saved Clip") : error;
                    }
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Removed Saved Clip from Pinloom");
                }
                return true;
            }

            if (actionId == QLatin1String("pin") || actionId == QLatin1String("unpin")) {
                const bool pinned = actionId == QLatin1String("pin");
                if (!saveClipMetadata(clip.value(), clip->name, clip->aliases, clip->tags, pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = pinned ? QStringLiteral("Pinned clip") : QStringLiteral("Unpinned clip");
                }
                return true;
            }
            if (actionId == QLatin1String("add_alias")) {
                bool accepted = false;
                const QString alias = QInputDialog::getText(parent,
                                                            QStringLiteral("Add Alias"),
                                                            QStringLiteral("Alias"),
                                                            QLineEdit::Normal,
                                                            QString(),
                                                            &accepted).trimmed();
                if (!accepted) {
                    if (status) {
                        *status = QStringLiteral("Add alias canceled");
                    }
                    return false;
                }
                QStringList aliases = clip->aliases;
                appendUniqueValue(aliases, alias);
                if (aliases == clip->aliases) {
                    if (status) {
                        *status = QStringLiteral("Clip alias already exists or is empty");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, aliases, clip->tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Added clip alias \"%1\"").arg(alias);
                }
                return true;
            }
            if (actionId == QLatin1String("add_tag")) {
                bool accepted = false;
                const QString tag = cleanTag(QInputDialog::getText(parent,
                                                                   QStringLiteral("Add Tag"),
                                                                   QStringLiteral("Tag"),
                                                                   QLineEdit::Normal,
                                                                   QString(),
                                                                   &accepted));
                if (!accepted) {
                    if (status) {
                        *status = QStringLiteral("Add tag canceled");
                    }
                    return false;
                }
                QStringList tags = clip->tags;
                appendUniqueValue(tags, tag);
                if (tags == clip->tags) {
                    if (status) {
                        *status = QStringLiteral("Clip tag already exists or is empty");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), clip->name, clip->aliases, tags, clip->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Added clip tag \"%1\"").arg(tag);
                }
                return true;
            }
            if (actionId == QLatin1String("edit_metadata")) {
                const std::optional<MetadataEdit> edit =
                    promptMetadataEdit(parent,
                                       QStringLiteral("Edit Clip"),
                                       clip->name,
                                       clip->aliases,
                                       clip->tags,
                                       clip->pinned);
                if (!edit.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit canceled");
                    }
                    return false;
                }
                if (!saveClipMetadata(clip.value(), edit->name, edit->aliases, edit->tags, edit->pinned, status)) {
                    return false;
                }
                if (status) {
                    *status = QStringLiteral("Updated clip \"%1\"").arg(edit->name);
                }
                return true;
            }
        }

        if (!selectPanelTarget(target)) {
            if (status) {
                *status = QStringLiteral("Selected result is no longer available");
            }
            return false;
        }

        if (actionId == QLatin1String("remove")) {
            const QString title = target.anchor.has_value()
                ? QStringLiteral("Delete Anchor")
                : Pinloom::isInboxResourceId(target.resourceId)
                      ? QStringLiteral("Remove Inbox File")
                      : QStringLiteral("Remove Resource");
            const QString body = target.anchor.has_value()
                ? QStringLiteral("Delete \"%1\" from Pinloom?\n\nThis only removes the Pinloom anchor. It will not delete the target file.")
                      .arg(targetTitle(target))
                : QStringLiteral("Remove \"%1\" from Pinloom?\n\nThis hides Pinloom's record only. The original file is not deleted.")
                      .arg(targetTitle(target));
            const QMessageBox::StandardButton choice = QMessageBox::question(parent, title, body);
            if (choice != QMessageBox::Yes) {
                if (status) {
                    *status = QStringLiteral("Remove canceled");
                }
                return false;
            }

            const bool removed = target.anchor.has_value()
                ? repository.softDeleteAnchor(target.resourceId, target.anchor.value())
                : repository.softDeleteResource(target.resourceId);
            if (!removed) {
                if (status) {
                    *status = target.anchor.has_value()
                        ? QStringLiteral("Unable to delete anchor")
                        : QStringLiteral("Unable to remove resource from Pinloom");
                }
                return false;
            }

            panel->setSearchText(panel->searchText());
            if (status) {
                *status = target.anchor.has_value()
                    ? QStringLiteral("Deleted anchor from Pinloom")
                    : Pinloom::isInboxResourceId(target.resourceId)
                          ? QStringLiteral("Removed Inbox file from Pinloom; original file was not deleted")
                          : QStringLiteral("Removed resource from Pinloom; original file was not deleted");
            }
            return true;
        }

        if (actionId == QLatin1String("pin") || actionId == QLatin1String("unpin")) {
            const bool pinned = actionId == QLatin1String("pin");
            const bool updated = target.anchor.has_value()
                ? panel->setSelectedAnchorPinned(pinned)
                : panel->setResourcePinnedById(target.resourceId, pinned);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
        }
        if (actionId == QLatin1String("add_alias")) {
            bool accepted = false;
            const QString alias = QInputDialog::getText(parent,
                                                        QStringLiteral("Add Alias"),
                                                        QStringLiteral("Alias"),
                                                        QLineEdit::Normal,
                                                        QString(),
                                                        &accepted).trimmed();
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Add alias canceled");
                }
                return false;
            }
            const bool updated = panel->addAliasToSelectedTarget(alias);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
        }
        if (actionId == QLatin1String("add_tag")) {
            bool accepted = false;
            const QString tag = cleanTag(QInputDialog::getText(parent,
                                                               QStringLiteral("Add Tag"),
                                                               QStringLiteral("Tag"),
                                                               QLineEdit::Normal,
                                                               QString(),
                                                               &accepted));
            if (!accepted) {
                if (status) {
                    *status = QStringLiteral("Add tag canceled");
                }
                return false;
            }
            const bool updated = panel->addTagToSelectedTarget(tag);
            if (status) {
                *status = panel->statusText();
            }
            return updated;
        }
        if (actionId == QLatin1String("edit_metadata")) {
            if (target.anchor.has_value()) {
                const std::optional<MetadataEdit> edit =
                    promptMetadataEdit(parent,
                                       QStringLiteral("Edit Anchor"),
                                       targetTitle(target),
                                       target.anchor->aliases,
                                       target.anchor->tags,
                                       target.anchor->pinned);
                if (!edit.has_value()) {
                    if (status) {
                        *status = QStringLiteral("Edit canceled");
                    }
                    return false;
                }
                if (!panel->editSelectedAnchor(edit->name, edit->aliases, edit->tags)) {
                    if (status) {
                        *status = panel->statusText();
                    }
                    return false;
                }
                if (target.anchor->pinned != edit->pinned
                    && !panel->setSelectedAnchorPinned(edit->pinned)) {
                    if (status) {
                        *status = panel->statusText();
                    }
                    return false;
                }
                if (status) {
                    *status = panel->statusText();
                }
                return true;
            }

            std::optional<Pinloom::Resource> resource = repository.findResource(target.resourceId);
            if (!resource.has_value()) {
                if (status) {
                    *status = QStringLiteral("Resource no longer exists");
                }
                return false;
            }
            const std::optional<Pinloom::ResourceUsage> usage = repository.resourceUsage(target.resourceId);
            const bool pinned = usage.has_value() && usage->pinned;
            const std::optional<MetadataEdit> edit =
                promptMetadataEdit(parent,
                                   QStringLiteral("Edit Resource"),
                                   resource->title,
                                   resource->aliases,
                                   resource->tags,
                                   pinned);
            if (!edit.has_value()) {
                if (status) {
                    *status = QStringLiteral("Edit canceled");
                }
                return false;
            }
            resource->title = edit->name;
            resource->aliases = edit->aliases;
            resource->tags = edit->tags;
            resource->updatedAt = QDateTime::currentDateTimeUtc();
            if (!repository.upsertResource(resource.value())) {
                if (status) {
                    *status = QStringLiteral("Unable to update resource");
                }
                return false;
            }
            if (pinned != edit->pinned && !repository.setResourcePinned(resource->id, edit->pinned)) {
                if (status) {
                    *status = QStringLiteral("Unable to update pinned resource");
                }
                return false;
            }
            panel->setSearchText(panel->searchText());
            panel->selectResultResource(resource->id);
            if (status) {
                *status = QStringLiteral("Updated resource \"%1\"").arg(resource->title);
            }
            return true;
        }

        if (status) {
            *status = QStringLiteral("Action \"%1\" is not implemented").arg(action.label);
        }
        return false;
    };

    auto *commandPanel = new Pinloom::PinloomCommandPanel(commandOptions, &commandWindow);
    commandWindow.setCentralWidget(commandPanel);

    if (clipHost && clipHost->runtime()) {
        clipHost->runtime()->trayController().setShowPickerHandler([&commandWindow, commandPanel]() {
            Pinloom::showCommandPanelForHotkey(commandWindow, *commandPanel);
            commandPanel->openClipSearch();
        });
        if (!clipHost->start()) {
            QMessageBox::warning(&window,
                                 QStringLiteral("Pinloom Clip"),
                                 QStringLiteral("Pinloom Clip could not start:\n%1").arg(clipHost->lastError()));
        }
    }

    std::unique_ptr<Pinloom::ClipHotkeyBackend> mainPanelHotkeyBackend = Pinloom::createMainPanelHotkeyBackend();
    Pinloom::ClipHotkeyService mainPanelHotkeyService(Pinloom::defaultMainPanelHotkeyConfig(),
                                                      mainPanelHotkeyBackend.get(),
                                                      &app);
    Pinloom::MainPanelHotkeyController mainPanelHotkeyController(mainPanelHotkeyService,
                                                                 [&commandWindow,
                                                                  commandPanel,
                                                                  &lastForegroundContext]() {
                                                                     lastForegroundContext =
                                                                         Pinloom::currentForegroundAppWindowContext();
                                                                     Pinloom::showCommandPanelForHotkey(commandWindow,
                                                                                                        *commandPanel);
                                                                 },
                                                                 &app);
    window.show();

    if (!mainPanelHotkeyService.start()) {
        const QString error = QStringLiteral("Pinloom main hotkey %1 could not be registered:\n%2\n\n"
                                             "Ctrl+Space may be reserved by an IME or another application.")
                                  .arg(mainPanelHotkeyService.displayText(), mainPanelHotkeyService.lastError());
        qWarning().noquote() << error;
        QMessageBox::warning(&window, QStringLiteral("Pinloom"), error);
    }

    return app.exec();
}
