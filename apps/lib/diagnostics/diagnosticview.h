/*
    SPDX-FileCopyrightText: 2019 Mark Nauwelaerts <mark.nauwelaerts@gmail.com>
    SPDX-FileCopyrightText: 2022 Waqar Ahmed <waqar.17a@gmail.com>
    SPDX-License-Identifier: MIT
*/
#pragma once

#include "kateprivate_export.h"

#include "diagnostic_types.h"

#include <QJsonObject>
#include <QStandardItemModel>
#include <QUrl>
#include <QWidget>

#include <KTextEditor/MarkInterface>
#include <KTextEditor/MovingRange>
#include <KTextEditor/Range>

class KConfigGroup;
class SessionDiagnosticSuppressions;
class KateMainWindow;
class QSortFilterProxyModel;
class KateTextHintProvider;

class KATE_PRIVATE_EXPORT DiagnosticsProvider : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticsProvider(KTextEditor::MainWindow *mainWindow, QObject *parent = nullptr);

    // Get suppressions
    // e.g json object
    /*
     * "suppressions": {
     *     "rulename": ["filename_regex", "message regexp", "code regexp"],
     *  }
     */
    virtual QJsonObject suppressions(KTextEditor::Document *) const
    {
        return {};
    }

    /**
     * If @p filterTo is a valid provider than DiagnosticView will
     * filter out all diagnostics that are not from @p filterTo.
     */
    void showDiagnosticsView(DiagnosticsProvider *filterTo = nullptr);

    /**
     * If @p filterTo is a valid provider, then DiagnosticView will
     * filter out all diagnostics that are not from @p filterTo.
     */
    void filterDiagnosticsViewTo(DiagnosticsProvider *filterTo);

    /**
     * Whether diagnostics of this provider should be automatically cleared
     * when a document is closed
     */
    void setPersistentDiagnostics(bool p)
    {
        m_persistentDiagnostics = p;
    }

    QString name;

Q_SIGNALS:
    /// emitted by provider when diags are available
    void diagnosticsAdded(const FileDiagnostics &);

    /// emitted by provider
    /// DiagnosticView will remove diagnostics where provider is @p provider
    void requestClearDiagnostics(DiagnosticsProvider *provider);

    /// Request fixes for given diagnostic
    /// @p data must be passed back when fixes are sent back via fixesAvailable()
    /// emitted by DiagnosticView
    void requestFixes(const QUrl &, const Diagnostic &, const QVariant &data);

    /// emitted by provider when fixes are available
    void fixesAvailable(const QVector<DiagnosticFix> &fixes, const QVariant &data);

    /// emitted by provider to clear suppressions
    /// (as some state that the previously provided ones depend on may have changed
    void requestClearSuppressions(DiagnosticsProvider *provider);

private:
    friend class DiagnosticsView;
    class DiagnosticsView *diagnosticView;
    bool m_persistentDiagnostics = false;
};

class DiagnosticsView : public QWidget
{
    Q_OBJECT
    friend class ForwardingTextHintProvider;

public:
    explicit DiagnosticsView(QWidget *parent, KateMainWindow *mainWindow, QWidget *tabButton);
    ~DiagnosticsView();

    void registerDiagnosticsProvider(DiagnosticsProvider *provider);
    void unregisterDiagnosticsProvider(DiagnosticsProvider *provider);

    void readSessionConfig(const KConfigGroup &config);
    void writeSessionConfig(KConfigGroup &config);

    void onTextHint(KTextEditor::View *view, const KTextEditor::Cursor &position) const;

    void showToolview(DiagnosticsProvider *filterTo = nullptr);
    void filterViewTo(DiagnosticsProvider *provider);

protected:
    void showEvent(QShowEvent *e) override;

private:
    void onFixesAvailable(const QVector<DiagnosticFix> &fixes, const QVariant &data);
    void showFixesInMenu(const QVector<DiagnosticFix> &fixes);
    void quickFix();
    void moveDiagnosticsSelection(bool forward);
    void nextItem();
    void previousItem();
    void onDiagnosticsAdded(const FileDiagnostics &diagnostics);
    void clearDiagnosticsFromProvider(DiagnosticsProvider *provider)
    {
        clearDiagnosticsForStaleDocs({}, provider);
    }
    void clearDiagnosticsForStaleDocs(const QVector<QString> &filesToKeep, DiagnosticsProvider *provider);
    void clearSuppressionsFromProvider(DiagnosticsProvider *provider);
    void onDocumentUrlChanged();
    void updateDiagnosticsState(QStandardItem *&topItem);
    void updateMarks(const QList<QUrl> &urls = {});
    void goToItemLocation(QModelIndex index);

    void onViewChanged(KTextEditor::View *v);

    void onDoubleClicked(const QModelIndex &index, bool quickFix = false);

    void addMarks(KTextEditor::Document *doc, QStandardItem *item);
    void addMarksRec(KTextEditor::Document *doc, QStandardItem *item);
    void addMarks(KTextEditor::Document *doc);

    Q_SLOT void clearAllMarks(KTextEditor::Document *doc);
    Q_SLOT void onMarkClicked(KTextEditor::Document *document, KTextEditor::Mark mark, bool &handled);

    bool syncDiagnostics(KTextEditor::Document *document, int line, bool allowTop, bool doShow);
    void updateDiagnosticsSuppression(QStandardItem *topItem, KTextEditor::Document *doc, bool force = false);

    void onContextMenuRequested(const QPoint &pos);

    void setupDiagnosticViewToolbar(class QVBoxLayout *mainLayout);

    KateMainWindow *const m_mainWindow;
    class QTreeView *const m_diagnosticsTree;
    class QToolButton *const m_clearButton;
    class QLineEdit *const m_filterLineEdit;
    class QComboBox *const m_providerCombo;
    class QToolButton *const m_errFilterBtn;
    class QToolButton *const m_warnFilterBtn;

    class ProviderListModel *m_providerModel;

    QStandardItemModel m_model;
    QSortFilterProxyModel *const m_proxy;
    QVector<DiagnosticsProvider *> m_providers;
    std::unique_ptr<SessionDiagnosticSuppressions> m_sessionDiagnosticSuppressions;

    QHash<KTextEditor::Document *, QVector<KTextEditor::MovingRange *>> m_diagnosticsRanges;
    // applied marks
    QSet<KTextEditor::Document *> m_diagnosticsMarks;

    class DiagTabOverlay *const m_tabButtonOverlay;

    QMetaObject::Connection posChangedConnection;
    QTimer *const m_posChangedTimer;
    QTimer *const m_filterChangedTimer;
    std::unique_ptr<KateTextHintProvider> m_textHintProvider;
};
