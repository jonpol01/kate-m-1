/*
    SPDX-FileCopyrightText: 2021 Waqar Ahmed <waqar.17a@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "gitwidget.h"
#include "branchesdialog.h"
#include "git/gitdiff.h"
#include "gitcommitdialog.h"
#include "gitstatusmodel.h"
#include "kateproject.h"
#include "kateprojectpluginview.h"
#include "pushpulldialog.h"
#include "stashdialog.h"

#include <QContextMenuEvent>
#include <QCoreApplication>
#include <QDebug>
#include <QDialog>
#include <QEvent>
#include <QFileInfo>
#include <QHeaderView>
#include <QInputMethodEvent>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>
#include <QtConcurrentRun>

#include <KLocalizedString>
#include <KMessageBox>
#include <QPointer>

#include <KTextEditor/Application>
#include <KTextEditor/ConfigInterface>
#include <KTextEditor/Editor>
#include <KTextEditor/MainWindow>
#include <KTextEditor/Message>
#include <KTextEditor/View>

class NumStatStyle final : public QStyledItemDelegate
{
public:
    NumStatStyle(QObject *parent, KateProjectPlugin *p)
        : QStyledItemDelegate(parent)
        , m_plugin(p)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        if (!m_plugin->showGitStatusWithNumStat()) {
            return QStyledItemDelegate::paint(painter, option, index);
        }

        const auto strs = index.data().toString().split(QLatin1Char(' '));
        if (strs.count() < 3) {
            return QStyledItemDelegate::paint(painter, option, index);
        }

        QStyleOptionViewItem options = option;
        initStyleOption(&options, index);
        painter->save();

        // paint background
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, option.palette.highlight());
        } else {
            painter->fillRect(option.rect, option.palette.base());
        }

        options.text = QString(); // clear old text
        options.widget->style()->drawControl(QStyle::CE_ItemViewItem, &options, painter, options.widget);

        const QString add = strs.at(0) + QStringLiteral(" ");
        const QString sub = strs.at(1) + QStringLiteral(" ");
        const QString Status = strs.at(2);

        int ha = option.fontMetrics.horizontalAdvance(add);
        int hs = option.fontMetrics.horizontalAdvance(sub);
        int hS = option.fontMetrics.horizontalAdvance(Status);

        QRect r = option.rect;
        int mw = r.width() - (ha + hs + hS);
        r.setX(r.x() + mw);

        static constexpr auto red = QColor(237, 21, 21); // Breeze Danger Red
        static constexpr auto green = QColor(17, 209, 27); // Breeze Verdant Green

        painter->setPen(green);
        painter->drawText(r, Qt::AlignVCenter, add);
        r.setX(r.x() + ha);

        painter->setPen(red);
        painter->drawText(r, Qt::AlignVCenter, sub);
        r.setX(r.x() + hs);

        painter->setPen(index.data(Qt::ForegroundRole).value<QColor>());
        painter->drawText(r, Qt::AlignVCenter, Status);

        painter->restore();
    }

private:
    KateProjectPlugin *m_plugin;
};

class GitWidgetTreeView : public QTreeView
{
public:
    GitWidgetTreeView(QWidget *parent)
        : QTreeView(parent)
    {
    }

    // we want no branches!
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override
    {
    }
};

static QToolButton *toolButton(const QString &icon, const QString &tooltip, const QString &text = QString(), Qt::ToolButtonStyle t = Qt::ToolButtonIconOnly)
{
    auto tb = new QToolButton;
    tb->setIcon(QIcon::fromTheme(icon));
    tb->setToolTip(tooltip);
    tb->setText(text);
    tb->setAutoRaise(true);
    tb->setToolButtonStyle(t);
    tb->setSizePolicy(QSizePolicy::Minimum, tb->sizePolicy().verticalPolicy());
    return tb;
}

GitWidget::GitWidget(KateProject *project, KTextEditor::MainWindow *mainWindow, KateProjectPluginView *pluginView)
    : m_project(project)
    , m_mainWin(mainWindow)
    , m_pluginView(pluginView)
{
    setDotGitPath();

    m_treeView = new GitWidgetTreeView(this);

    buildMenu();
    m_menuBtn = toolButton(QStringLiteral("application-menu"), QString());
    m_menuBtn->setMenu(m_gitMenu);
    m_menuBtn->setArrowType(Qt::NoArrow);
    m_menuBtn->setStyleSheet(QStringLiteral("QToolButton::menu-indicator{ image: none; }"));
    connect(m_menuBtn, &QToolButton::clicked, this, [this](bool) {
        m_menuBtn->showMenu();
    });

    m_commitBtn = toolButton(QStringLiteral("svn-commit"), QString(), i18n("Commit"), Qt::ToolButtonTextBesideIcon);

    m_pushBtn = toolButton(QStringLiteral("arrow-up"), i18n("Git push"));
    connect(m_pushBtn, &QToolButton::clicked, this, [this]() {
        PushPullDialog ppd(m_mainWin->window(), m_gitPath);
        connect(&ppd, &PushPullDialog::runGitCommand, this, &GitWidget::runPushPullCmd);
        ppd.openDialog(PushPullDialog::Push);
    });

    m_pullBtn = toolButton(QStringLiteral("arrow-down"), i18n("Git pull"));
    connect(m_pullBtn, &QToolButton::clicked, this, [this]() {
        PushPullDialog ppd(m_mainWin->window(), m_gitPath);
        connect(&ppd, &PushPullDialog::runGitCommand, this, &GitWidget::runPushPullCmd);
        ppd.openDialog(PushPullDialog::Pull);
    });

    m_cancelBtn = toolButton(QStringLiteral("dialog-cancel"), i18n("Cancel Operation"));
    m_cancelBtn->setHidden(true);
    connect(m_cancelBtn, &QToolButton::clicked, this, [this] {
        if (m_cancelHandle) {
            // we don't want error occurred, this is intentional
            disconnect(m_cancelHandle, &QProcess::errorOccurred, nullptr, nullptr);
            const auto args = m_cancelHandle->arguments();
            m_cancelHandle->kill();
            sendMessage(QStringLiteral("git ") + args.join(QLatin1Char(' ')) + i18n(" canceled."), false);
            hideCancel();
        }
    });

    QVBoxLayout *layout = new QVBoxLayout;
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);

    QHBoxLayout *btnsLayout = new QHBoxLayout;
    btnsLayout->setContentsMargins(0, 0, 0, 0);

    for (auto *btn : {m_commitBtn, m_cancelBtn, m_pushBtn, m_pullBtn, m_menuBtn}) {
        btnsLayout->addWidget(btn);
    }
    btnsLayout->setStretch(0, 1);

    layout->addLayout(btnsLayout);
    layout->addWidget(m_treeView);

    m_model = new GitStatusModel(this);

    m_treeView->setUniformRowHeights(true);
    m_treeView->setHeaderHidden(true);
    m_treeView->setSelectionMode(QTreeView::ExtendedSelection);
    m_treeView->setModel(m_model);
    m_treeView->installEventFilter(this);
    m_treeView->setRootIsDecorated(false);

    if (m_treeView->style()) {
        auto indent = m_treeView->style()->pixelMetric(QStyle::PM_TreeViewIndentation, nullptr, m_treeView);
        m_treeView->setIndentation(indent / 4);
    }

    m_treeView->header()->setStretchLastSection(false);
    m_treeView->header()->setSectionResizeMode(0, QHeaderView::Stretch);

    m_treeView->setItemDelegateForColumn(1, new NumStatStyle(this, m_pluginView->plugin()));

    setLayout(layout);

    connect(&m_gitStatusWatcher, &QFutureWatcher<GitUtils::GitParsedStatus>::finished, this, &GitWidget::parseStatusReady);
    connect(m_commitBtn, &QPushButton::clicked, this, &GitWidget::opencommitChangesDialog);

    // single / double click
    connect(m_treeView, &QTreeView::clicked, this, &GitWidget::treeViewSingleClicked);
    connect(m_treeView, &QTreeView::doubleClicked, this, &GitWidget::treeViewDoubleClicked);
}

GitWidget::~GitWidget()
{
    if (m_cancelHandle) {
        m_cancelHandle->kill();
    }
}

void GitWidget::setDotGitPath()
{
    /* This call is intentionally blocking because we need git path for everything else */
    QProcess git;
    git.setProgram(QStringLiteral("git"));
    git.setWorkingDirectory(m_project->baseDir());
    git.setArguments({QStringLiteral("rev-parse"), QStringLiteral("--absolute-git-dir")});
    git.start(QProcess::ReadOnly);
    if (git.waitForStarted() && git.waitForFinished(-1)) {
        if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
            sendMessage(i18n("Failed to find .git directory, things may not work correctly: %1", QString::fromUtf8(git.readAllStandardError())), true);
            m_gitPath = m_project->baseDir();
            return;
        }
        m_gitPath = QString::fromUtf8(git.readAllStandardOutput());
        if (m_gitPath.endsWith(QLatin1String("\n"))) {
            m_gitPath.remove(QLatin1String(".git\n"));
        } else {
            m_gitPath.remove(QLatin1String(".git"));
        }
        // set once, use everywhere
        // This is per project
        git.setWorkingDirectory(m_gitPath);
    }
}

void GitWidget::sendMessage(const QString &plainText, bool warn)
{
    // use generic output view
    QVariantMap genericMessage;
    genericMessage.insert(QStringLiteral("type"), warn ? QStringLiteral("Error") : QStringLiteral("Info"));
    genericMessage.insert(QStringLiteral("category"), i18n("Git"));
    genericMessage.insert(QStringLiteral("categoryIcon"), QIcon(QStringLiteral(":/icons/icons/sc-apps-git.svg")));
    genericMessage.insert(QStringLiteral("text"), plainText);
    Q_EMIT m_pluginView->message(genericMessage);
}

KTextEditor::MainWindow *GitWidget::mainWindow()
{
    return m_mainWin;
}

std::vector<GitWidget::TempFileViewPair> *GitWidget::tempFilesVector()
{
    return &m_tempFiles;
}

QProcess *GitWidget::gitp()
{
    auto git = new QProcess(this);
    git->setProgram(QStringLiteral("git"));
    git->setWorkingDirectory(m_gitPath);
    connect(git, &QProcess::errorOccurred, this, [this, git](QProcess::ProcessError) {
        sendMessage(git->errorString(), true);
        git->deleteLater();
    });
    return git;
}

void GitWidget::getStatus(bool untracked, bool submodules)
{
    auto git = gitp();
    connect(git, &QProcess::finished, this, [this, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            // no error on status failure
            //            sendMessage(QString::fromUtf8(git->readAllStandardError()), true);
        } else {
            auto future = QtConcurrent::run(GitUtils::parseStatus, git->readAllStandardOutput());
            m_gitStatusWatcher.setFuture(future);
        }
        git->deleteLater();
    });

    auto args = QStringList{QStringLiteral("status"), QStringLiteral("-z")};
    if (!untracked) {
        args.append(QStringLiteral("-uno"));
    } else {
        args.append(QStringLiteral("-u"));
    }
    if (!submodules) {
        args.append(QStringLiteral("--ignore-submodules"));
    }
    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

void GitWidget::runGitCmd(const QStringList &args, const QString &i18error)
{
    auto git = gitp();
    connect(git, &QProcess::finished, this, [this, i18error, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(i18error + QStringLiteral(": ") + QString::fromUtf8(git->readAllStandardError()), true);
        } else {
            getStatus();
        }
        git->deleteLater();
    });
    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

void GitWidget::runPushPullCmd(const QStringList &args)
{
    auto git = gitp();
    git->setProcessChannelMode(QProcess::MergedChannels);

    connect(git, &QProcess::finished, this, [this, args, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(QStringLiteral("git ") + args.first() + i18n(" error: %1", QString::fromUtf8(git->readAll())), true);
        } else {
            auto gargs = args;
            gargs.push_front(QStringLiteral("git"));
            QString cmd = gargs.join(QStringLiteral(" "));
            QString out = QString::fromUtf8(git->readAll());
            sendMessage(i18n("\"%1\" executed successfully: %2", cmd, out), false);
            getStatus();
        }
        hideCancel();
        git->deleteLater();
    });

    enableCancel(git);
    git->setArguments(args);
    git->start(QProcess::ReadOnly);

    // kill after 40 seconds
    QTimer::singleShot(40000, this, [git, this] {
        if (git->state() == QProcess::Running) {
            sendMessage(i18n("Git operation timed-out. Killing..."), true);
            git->kill();
            git->deleteLater();
            hideCancel();
        }
    });
}

void GitWidget::stage(const QStringList &files, bool)
{
    if (files.isEmpty()) {
        return;
    }

    auto args = QStringList{QStringLiteral("add"), QStringLiteral("-A"), QStringLiteral("--")};
    args.append(files);

    runGitCmd(args, i18n("Failed to stage file. Error:"));
}

void GitWidget::unstage(const QStringList &files)
{
    if (files.isEmpty()) {
        return;
    }

    // git reset -q HEAD --
    auto args = QStringList{QStringLiteral("reset"), QStringLiteral("-q"), QStringLiteral("HEAD"), QStringLiteral("--")};
    args.append(files);

    runGitCmd(args, i18n("Failed to unstage file. Error:"));
}

void GitWidget::discard(const QStringList &files)
{
    if (files.isEmpty()) {
        return;
    }
    // discard=>git checkout -q -- xx.cpp
    auto args = QStringList{QStringLiteral("checkout"), QStringLiteral("-q"), QStringLiteral("--")};
    args.append(files);
    runGitCmd(args, i18n("Failed to discard changes. Error:"));
}

void GitWidget::clean(const QStringList &files)
{
    if (files.isEmpty()) {
        return;
    }
    // discard=>git clean -q -f -- xx.cpp
    auto args = QStringList{QStringLiteral("clean"), QStringLiteral("-q"), QStringLiteral("-f"), QStringLiteral("--")};
    args.append(files);
    runGitCmd(args, i18n("Failed to remove. Error:"));
}

void GitWidget::openAtHEAD(const QString &file)
{
    if (file.isEmpty()) {
        return;
    }

    auto git = gitp();
    auto args = QStringList{QStringLiteral("show"), QStringLiteral("--textconv")};
    args.append(QStringLiteral(":") + file);
    git->setArguments(args);
    git->start(QProcess::ReadOnly);

    connect(git, &QProcess::finished, this, [this, file, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(i18n("Failed to open file at HEAD: %1", QString::fromUtf8(git->readAllStandardError())), true);
        } else {
            openTempFile(QFileInfo(file).fileName(), QStringLiteral("XXXXXX - (HEAD) - %1"), git->readAllStandardOutput());
        }
        git->deleteLater();
    });

    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

void GitWidget::showDiff(const QString &file, bool staged)
{
    auto args = QStringList{QStringLiteral("diff")};
    if (staged) {
        args.append(QStringLiteral("--staged"));
    }

    if (!file.isEmpty()) {
        args.append(QStringLiteral("--"));
        args.append(file);
    }

    auto git = gitp();
    connect(git, &QProcess::finished, this, [this, file, staged, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(i18n("Failed to get Diff of file: %1", QString::fromUtf8(git->readAllStandardError())), true);
        } else {
            const QString filename = file.isEmpty() ? QString() : QFileInfo(file).fileName();

            openTempFile(filename, QStringLiteral("XXXXXX %1.diff"), git->readAllStandardOutput());

            if (m_tempFiles.empty()) {
                return;
            }
            auto v = m_tempFiles.back().second;
            if (!v || !v->document()) {
                return;
            }
            auto m = v->contextMenu();

            if (!staged) {
                QMenu *menu = new QMenu(v);
                auto sh = menu->addAction(i18n("Stage Hunk"));
                auto sl = menu->addAction(i18n("Stage Lines"));
                menu->addActions(m->actions());
                v->setContextMenu(menu);

                connect(sh, &QAction::triggered, v, [=] {
                    applyDiff(file, false, true, v);
                });
                connect(sl, &QAction::triggered, v, [=] {
                    applyDiff(file, false, false, v);
                });
            } else {
                QMenu *menu = new QMenu(v);
                auto ush = menu->addAction(i18n("Unstage Hunk"));
                auto usl = menu->addAction(i18n("Unstage Lines"));
                menu->addActions(m->actions());
                v->setContextMenu(menu);

                connect(ush, &QAction::triggered, v, [=] {
                    applyDiff(file, true, true, v);
                });
                connect(usl, &QAction::triggered, v, [=] {
                    applyDiff(file, true, false, v);
                });
            }
        }
        git->deleteLater();
    });

    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

void GitWidget::launchExternalDiffTool(const QString &file, bool staged)
{
    if (file.isEmpty()) {
        return;
    }

    auto args = QStringList{QStringLiteral("difftool"), QStringLiteral("-y")};
    if (staged) {
        args.append(QStringLiteral("--staged"));
    }
    args.append(file);

    QProcess git;
    git.startDetached(QStringLiteral("git"), args, m_gitPath);
}

void GitWidget::commitChanges(const QString &msg, const QString &desc, bool signOff)
{
    auto args = QStringList{QStringLiteral("commit")};
    if (signOff) {
        args.append(QStringLiteral("-s"));
    }
    args.append(QStringLiteral("-m"));
    args.append(msg);
    if (!desc.isEmpty()) {
        args.append(QStringLiteral("-m"));
        args.append(desc);
    }

    auto git = gitp();

    connect(git, &QProcess::finished, this, [this, git](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(i18n("Failed to commit: %1", QString::fromUtf8(git->readAllStandardError())), true);
        } else {
            m_commitMessage.clear();
            getStatus();
            sendMessage(i18n("Changes committed successfully."), false);
        }
        git->deleteLater();
    });
    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

QString GitWidget::getDiff(KTextEditor::View *v, bool hunk, bool alreadyStaged)
{
    auto range = v->selectionRange();
    int startLine = range.start().line();
    int endLine = range.end().line();
    if (range.isEmpty() || hunk) {
        startLine = endLine = v->cursorPosition().line();
    }

    VcsDiff full;
    full.setDiff(v->document()->text());
    auto p = QUrl::fromUserInput(m_gitPath);
    full.setBaseDiff(QUrl::fromUserInput(m_gitPath));

    const auto dir = alreadyStaged ? VcsDiff::Reverse : VcsDiff::Forward;

    VcsDiff selected = hunk ? full.subDiffHunk(startLine, dir) : full.subDiff(startLine, endLine, dir);
    return selected.diff();
}

void GitWidget::applyDiff(const QString &fileName, bool staged, bool hunk, KTextEditor::View *v)
{
    if (!v) {
        return;
    }

    const QString diff = getDiff(v, hunk, staged);
    if (diff.isEmpty()) {
        return;
    }

    QTemporaryFile *file = new QTemporaryFile(this);
    if (!file->open()) {
        sendMessage(i18n("Failed to stage selection"), true);
        return;
    }
    file->write(diff.toUtf8());
    file->close();

    auto git = gitp();
    QStringList args{QStringLiteral("apply"), QStringLiteral("--index"), QStringLiteral("--cached"), file->fileName()};

    connect(git, &QProcess::finished, this, [=](int exitCode, QProcess::ExitStatus es) {
        if (es != QProcess::NormalExit || exitCode != 0) {
            sendMessage(i18n("Failed to stage: %1", QString::fromUtf8(git->readAllStandardError())), true);
        } else {
            // close and reopen doc to show updated diff
            if (v && v->document()) {
                KTextEditor::Editor::instance()->application()->closeDocument(v->document());
                showDiff(fileName, staged);
            }
            // must come at the end
            QTimer::singleShot(100, this, [this] {
                getStatus();
            });
        }
        delete file;
        git->deleteLater();
    });
    git->setArguments(args);
    git->start(QProcess::ReadOnly);
}

void GitWidget::opencommitChangesDialog()
{
    if (m_model->stagedFiles().isEmpty()) {
        return sendMessage(i18n("Nothing to commit. Please stage your changes first."), true);
    }

    auto ciface = qobject_cast<KTextEditor::ConfigInterface *>(m_mainWin->activeView());
    QFont font;
    if (ciface) {
        font = ciface->configValue(QStringLiteral("font")).value<QFont>();
    } else {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }

    GitCommitDialog dialog(m_commitMessage, font);
    dialog.setWindowFlags(Qt::WindowSystemMenuHint | Qt::WindowTitleHint);

    int res = dialog.exec();
    if (res == QDialog::Accepted) {
        if (dialog.subject().isEmpty()) {
            return sendMessage(i18n("Commit message cannot be empty."), true);
        }
        m_commitMessage = dialog.subject() + QStringLiteral("[[\n\n]]") + dialog.description();
        commitChanges(dialog.subject(), dialog.description(), dialog.signoff());
    }
}

void GitWidget::handleClick(const QModelIndex &idx, ClickAction clickAction)
{
    auto type = idx.data(GitStatusModel::TreeItemType);
    if (type != GitStatusModel::NodeFile) {
        return;
    }

    if (clickAction == ClickAction::NoAction) {
        return;
    }

    const QString file = m_gitPath + idx.data(GitStatusModel::FileNameRole).toString();
    bool staged = idx.internalId() == GitStatusModel::NodeStage;

    if (clickAction == ClickAction::StageUnstage) {
        if (staged) {
            return unstage({file});
        }
        return stage({file});
    }

    if (clickAction == ClickAction::ShowDiff) {
        showDiff(file, staged);
    }

    if (clickAction == ClickAction::OpenFile) {
        m_mainWin->openUrl(QUrl::fromLocalFile(file));
    }
}

void GitWidget::treeViewSingleClicked(const QModelIndex &idx)
{
    handleClick(idx, m_pluginView->plugin()->singleClickAcion());
}

void GitWidget::treeViewDoubleClicked(const QModelIndex &idx)
{
    handleClick(idx, m_pluginView->plugin()->doubleClickAcion());
}

void GitWidget::hideEmptyTreeNodes()
{
    const auto emptyRows = m_model->emptyRows();
    m_treeView->expand(m_model->getModelIndex((GitStatusModel::NodeStage)));
    // 1 because "Staged" will always be visible
    for (int i = 1; i < 4; ++i) {
        if (emptyRows.contains(i)) {
            m_treeView->setRowHidden(i, QModelIndex(), true);
        } else {
            m_treeView->setRowHidden(i, QModelIndex(), false);
            if (i != GitStatusModel::NodeUntrack) {
                m_treeView->expand(m_model->getModelIndex((GitStatusModel::ItemType)i));
            }
        }
    }

    m_treeView->resizeColumnToContents(0);
    m_treeView->resizeColumnToContents(1);
}

void GitWidget::parseStatusReady()
{
    GitUtils::GitParsedStatus s = m_gitStatusWatcher.result();

    if (m_pluginView->plugin()->showGitStatusWithNumStat()) {
        numStatForStatus(s.changed, true);
        numStatForStatus(s.staged, false);
    }

    m_model->addItems(std::move(s), m_pluginView->plugin()->showGitStatusWithNumStat());
    hideEmptyTreeNodes();
}

void GitWidget::numStatForStatus(QVector<GitUtils::StatusItem> &list, bool modified)
{
    const auto args = modified ? QStringList{QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("-z")}
                               : QStringList{QStringLiteral("diff"), QStringLiteral("--numstat"), QStringLiteral("--staged"), QStringLiteral("-z")};

    QProcess git;
    git.setWorkingDirectory(m_gitPath);
    git.start(QStringLiteral("git"), args, QProcess::ReadOnly);
    if (git.waitForStarted() && git.waitForFinished(-1)) {
        if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
            return;
        }
    }

    GitUtils::parseDiffNumStat(list, git.readAllStandardOutput());
}

bool GitWidget::eventFilter(QObject *o, QEvent *e)
{
    if (e->type() == QEvent::ContextMenu) {
        if (o != m_treeView)
            return QWidget::eventFilter(o, e);
        QContextMenuEvent *cme = static_cast<QContextMenuEvent *>(e);
        treeViewContextMenuEvent(cme);
    }
    return QWidget::eventFilter(o, e);
}

void GitWidget::buildMenu()
{
    m_gitMenu = new QMenu(this);
    m_gitMenu->addAction(i18n("Refresh"), this, [this] {
        if (m_project) {
            getStatus();
        }
    });
    m_gitMenu->addAction(i18n("Checkout Branch"), this, [this] {
        BranchesDialog bd(m_mainWin->window(), m_pluginView, m_project->baseDir());
        bd.openDialog();
    });

    m_gitMenu->addAction(i18n("Stash"))->setMenu(stashMenu());
}

void GitWidget::createStashDialog(StashMode m, const QString &gitPath)
{
    auto stashDialog = new StashDialog(this, mainWindow()->window(), gitPath);
    connect(stashDialog, &StashDialog::message, this, &GitWidget::sendMessage);
    connect(stashDialog, &StashDialog::openTempFile, this, [this](const QString &t, const QByteArray &r) {
        openTempFile(QString(), t, r);
    });
    connect(stashDialog, &StashDialog::done, this, [this, stashDialog] {
        getStatus();
        stashDialog->deleteLater();
    });
    stashDialog->openDialog(m);
}

void GitWidget::enableCancel(QProcess *git)
{
    m_cancelHandle = git;
    m_pushBtn->hide();
    m_cancelBtn->show();
}

void GitWidget::hideCancel()
{
    m_cancelBtn->hide();
    m_pushBtn->show();
}

QMenu *GitWidget::stashMenu()
{
    QMenu *menu = new QMenu(this);
    auto stashAct = menu->addAction(i18n("Stash"));
    auto popLastAct = menu->addAction(i18n("Pop Last Stash"));
    auto popAct = menu->addAction(i18n("Pop Stash"));
    auto applyLastAct = menu->addAction(i18n("Apply Last Stash"));
    auto stashKeepStagedAct = menu->addAction(i18n("Stash (Keep Staged)"));
    auto stashUAct = menu->addAction(i18n("Stash (Include Untracked)"));
    auto applyStashAct = menu->addAction(i18n("Apply Stash"));
    auto dropAct = menu->addAction(i18n("Drop Stash"));
    auto showStashAct = menu->addAction(i18n("Show Stash Content"));

    connect(stashAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::Stash, m_gitPath);
    });
    connect(stashUAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashUntrackIncluded, m_gitPath);
    });
    connect(stashKeepStagedAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashKeepIndex, m_gitPath);
    });
    connect(popAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashPop, m_gitPath);
    });
    connect(applyStashAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashApply, m_gitPath);
    });
    connect(dropAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashDrop, m_gitPath);
    });
    connect(popLastAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashPopLast, m_gitPath);
    });
    connect(applyLastAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::StashApplyLast, m_gitPath);
    });
    connect(showStashAct, &QAction::triggered, this, [this] {
        createStashDialog(StashMode::ShowStashContent, m_gitPath);
    });

    return menu;
}

void GitWidget::clearTempFile(KTextEditor::Document *document)
{
    m_tempFiles.erase(std::remove_if(m_tempFiles.begin(),
                                     m_tempFiles.end(),
                                     [document](const GitWidget::TempFileViewPair &tf) {
                                         if (tf.second && tf.second->document() == document) {
                                             return true;
                                         }
                                         return false;
                                     }),
                      m_tempFiles.end());
}

void GitWidget::openTempFile(const QString &file, const QString &templatString, const QByteArray &contents)
{
    if (contents.isEmpty()) {
        return;
    }

    std::unique_ptr<QTemporaryFile> f(new QTemporaryFile);
    if (!templatString.isEmpty() && !file.isEmpty()) {
        f->setFileTemplate(templatString.arg(file));
    } else if (!templatString.isEmpty() && file.isEmpty()) {
        f->setFileTemplate(templatString);
    }
    if (!f->open()) {
        sendMessage(i18n("Failed to open temporary file for diff: %1", f->errorString()), true);
        return;
    }

    f->write(contents);
    f->flush();

    const QUrl tempFileUrl(QUrl::fromLocalFile(f->fileName()));
    QPointer<KTextEditor::View> v = m_mainWin->openUrl(tempFileUrl);

    if (!v || !v->document()) {
        return;
    }

    TempFileViewPair tfvp = std::make_pair(std::move(f), v);
    m_tempFiles.push_back(std::move(tfvp));
    connect(v->document(), &KTextEditor::Document::aboutToClose, this, &GitWidget::clearTempFile);
}

static KMessageBox::ButtonCode confirm(GitWidget *_this, const QString &text)
{
    return KMessageBox::questionYesNo(_this, text, {}, KStandardGuiItem::yes(), KStandardGuiItem::no(), {}, KMessageBox::Dangerous);
}

void GitWidget::treeViewContextMenuEvent(QContextMenuEvent *e)
{
    if (auto selModel = m_treeView->selectionModel()) {
        if (selModel->selectedRows().count() > 1) {
            return selectedContextMenu(e);
        }
    }

    auto idx = m_model->index(m_treeView->currentIndex().row(), 0, m_treeView->currentIndex().parent());
    auto type = idx.data(GitStatusModel::TreeItemType);

    if (type == GitStatusModel::NodeChanges || type == GitStatusModel::NodeUntrack) {
        QMenu menu;
        auto stageAct = menu.addAction(i18n("Stage All"));
        bool untracked = type == GitStatusModel::NodeUntrack;
        auto discardAct = untracked ? menu.addAction(i18n("Remove All")) : menu.addAction(i18n("Discard All"));
        auto ignoreAct = untracked ? menu.addAction(i18n("Open .gitignore")) : nullptr;
        auto diff = !untracked ? menu.addAction(i18n("Show diff")) : nullptr;
        // get files
        const QVector<GitUtils::StatusItem> &items = untracked ? m_model->untrackedFiles() : m_model->changedFiles();
        QStringList files;
        files.reserve(items.size());
        std::transform(items.begin(), items.end(), std::back_inserter(files), [](const GitUtils::StatusItem &i) {
            return QString::fromUtf8(i.file);
        });
        // execute action
        auto act = menu.exec(m_treeView->viewport()->mapToGlobal(e->pos()));
        if (act == stageAct) {
            stage(files, type == GitStatusModel::NodeUntrack);
        } else if (act == discardAct && !untracked) {
            auto ret = confirm(this, i18n("Are you sure you want to remove these files?"));
            if (ret == KMessageBox::Yes) {
                discard(files);
            }
        } else if (act == discardAct && untracked) {
            auto ret = confirm(this, i18n("Are you sure you want to discard all changes?"));
            if (ret == KMessageBox::Yes) {
                clean(files);
            }
        } else if (untracked && act == ignoreAct) {
            const auto files = m_project->files();
            const auto it = std::find_if(files.cbegin(), files.cend(), [](const QString &s) {
                if (s.contains(QStringLiteral(".gitignore"))) {
                    return true;
                }
                return false;
            });
            if (it != files.cend()) {
                m_mainWin->openUrl(QUrl::fromLocalFile(*it));
            }
        } else if (!untracked && act == diff) {
            showDiff(QString(), false);
        }
    } else if (type == GitStatusModel::NodeFile) {
        QMenu menu;
        bool staged = idx.internalId() == GitStatusModel::NodeStage;
        bool untracked = idx.internalId() == GitStatusModel::NodeUntrack;

        auto openFile = menu.addAction(i18n("Open file"));
        auto showDiffAct = untracked ? nullptr : menu.addAction(i18n("Show raw diff"));
        auto launchDifftoolAct = untracked ? nullptr : menu.addAction(i18n("Show in external git diff tool"));
        auto openAtHead = untracked ? nullptr : menu.addAction(i18n("Open at HEAD"));
        auto stageAct = staged ? menu.addAction(i18n("Unstage file")) : menu.addAction(i18n("Stage file"));
        auto discardAct = staged ? nullptr : untracked ? menu.addAction(i18n("Remove")) : menu.addAction(i18n("Discard"));

        auto act = menu.exec(m_treeView->viewport()->mapToGlobal(e->pos()));
        const QString file = m_gitPath + idx.data(GitStatusModel::FileNameRole).toString();
        if (act == stageAct) {
            if (staged) {
                return unstage({file});
            }
            return stage({file});
        } else if (act == discardAct && !untracked) {
            auto ret = confirm(this, i18n("Are you sure you want to discard the changes in this file?"));
            if (ret == KMessageBox::Yes) {
                discard({file});
            }
        } else if (act == openAtHead && !untracked) {
            openAtHEAD(idx.data(GitStatusModel::FileNameRole).toString());
        } else if (act == showDiffAct && !untracked) {
            showDiff(file, staged);
        } else if (act == discardAct && untracked) {
            auto ret = confirm(this, i18n("Are you sure you want to remove this file?"));
            if (ret == KMessageBox::Yes) {
                clean({file});
            }
        } else if (act == launchDifftoolAct) {
            launchExternalDiffTool(idx.data(GitStatusModel::FileNameRole).toString(), staged);
        } else if (act == openFile) {
            m_mainWin->openUrl(QUrl::fromLocalFile(file));
        }
    } else if (type == GitStatusModel::NodeStage) {
        QMenu menu;
        auto stage = menu.addAction(i18n("Unstage All"));
        auto diff = menu.addAction(i18n("Show diff"));
        auto act = menu.exec(m_treeView->viewport()->mapToGlobal(e->pos()));

        // git reset -q HEAD --
        if (act == stage) {
            const QVector<GitUtils::StatusItem> &items = m_model->stagedFiles();
            QStringList files;
            files.reserve(items.size());
            std::transform(items.begin(), items.end(), std::back_inserter(files), [](const GitUtils::StatusItem &i) {
                return QString::fromUtf8(i.file);
            });
            unstage(files);
        } else if (act == diff) {
            showDiff(QString(), true);
        }
    }
}

void GitWidget::selectedContextMenu(QContextMenuEvent *e)
{
    QStringList files;

    bool selectionHasStagedItems = false;
    bool selectionHasChangedItems = false;
    bool selectionHasUntrackedItems = false;

    if (auto selModel = m_treeView->selectionModel()) {
        const auto idxList = selModel->selectedIndexes();
        for (const auto &idx : idxList) {
            if (idx.internalId() == GitStatusModel::NodeStage) {
                selectionHasStagedItems = true;
            } else if (!idx.parent().isValid()) {
                // can't allow main nodes to be selected
                return;
            } else if (idx.internalId() == GitStatusModel::NodeUntrack) {
                selectionHasUntrackedItems = true;
            } else if (idx.internalId() == GitStatusModel::NodeChanges) {
                selectionHasChangedItems = true;
            }
            files.append(idx.data(GitStatusModel::FileNameRole).toString());
        }
    }

    const bool selHasUnstagedItems = selectionHasUntrackedItems || selectionHasChangedItems;

    // cant allow both
    if (selHasUnstagedItems && selectionHasStagedItems) {
        return;
    }

    QMenu menu;
    auto stageAct = selectionHasStagedItems ? menu.addAction(i18n("Unstage Selected Files")) : menu.addAction(i18n("Stage Selected Files"));
    auto discardAct = selectionHasChangedItems && !selectionHasUntrackedItems ? menu.addAction(i18n("Discard Selected Files")) : nullptr;
    auto removeAct = !selectionHasChangedItems && selectionHasUntrackedItems ? menu.addAction(i18n("Remove Selected Files")) : nullptr;
    auto execAct = menu.exec(m_treeView->viewport()->mapToGlobal(e->pos()));

    if (execAct == stageAct) {
        if (selectionHasChangedItems) {
            stage(files);
        } else {
            unstage(files);
        }
    } else if (selectionHasChangedItems && !selectionHasUntrackedItems && execAct == discardAct) {
        auto ret = confirm(this, i18n("Are you sure you want to discard the changes?"));
        if (ret == KMessageBox::Yes) {
            discard(files);
        }
    } else if (!selectionHasChangedItems && selectionHasUntrackedItems && execAct == removeAct) {
        auto ret = confirm(this, i18n("Are you sure you want to remove these untracked changes?"));
        if (ret == KMessageBox::Yes) {
            clean(files);
        }
    }
}
