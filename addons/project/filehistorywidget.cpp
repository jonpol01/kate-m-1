#include "filehistorywidget.h"

#include <QDate>
#include <QDebug>
#include <QFileInfo>
#include <QPainter>
#include <QProcess>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <KLocalizedString>

// git log --format=%H%n%aN%n%aE%n%at%n%ct%n%P%n%B --author-date-order
static QList<QByteArray> getFileHistory(const QString &file)
{
    QProcess git;
    git.setWorkingDirectory(QFileInfo(file).absolutePath());
    QStringList args{QStringLiteral("log"),
                     QStringLiteral("--format=%H%n%aN%n%aE%n%at%n%ct%n%P%n%B"),
                     QStringLiteral("-z"),
                     QStringLiteral("--author-date-order"),
                     file};
    git.start(QStringLiteral("git"), args, QProcess::ReadOnly);
    if (git.waitForStarted() && git.waitForFinished(-1)) {
        return git.readAll().split(0x00);
    }
    return {};
}

struct Commit {
    QByteArray hash;
    QString authorName;
    QString email;
    qint64 authorDate;
    qint64 commitDate;
    QByteArray parentHash;
    QString msg;
};
Q_DECLARE_METATYPE(Commit)

static QVector<Commit> parseCommits(const QList<QByteArray> &raw)
{
    QVector<Commit> commits;
    commits.reserve(raw.size());
    std::transform(raw.cbegin(), raw.cend(), std::back_inserter(commits), [](const QByteArray &r) {
        const auto lines = r.split('\n');
        if (lines.length() < 7) {
            return Commit{};
        }
        auto hash = lines.at(0);
        //        qWarning() << hash;
        auto author = QString::fromUtf8(lines.at(1));
        //        qWarning() << author;
        auto email = QString::fromUtf8(lines.at(2));
        //        qWarning() << email;
        qint64 authorDate = lines.at(3).toLong();
        //        qWarning() << authorDate;
        qint64 commitDate = lines.at(4).toLong();
        //        qWarning() << commitDate;
        auto parent = lines.at(5);
        //        qWarning() << parent;
        auto msg = QString::fromUtf8(lines.at(6));
        //        qWarning() << msg;
        return Commit{hash, author, email, authorDate, commitDate, parent, msg};
    });

    return commits;
}

class CommitListModel : public QAbstractListModel
{
public:
    CommitListModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    enum Role { CommitRole = Qt::UserRole + 1, CommitHash };

    int rowCount(const QModelIndex &) const override
    {
        return m_rows.count();
    }
    QVariant data(const QModelIndex &index, int role) const override
    {
        if (!index.isValid()) {
            return {};
        }
        auto row = index.row();
        switch (role) {
        case Role::CommitRole: {
            QVariant v;
            v.setValue(m_rows[row]);
            return v;
        }
        case Role::CommitHash:
            return m_rows[row].hash;
        }

        return {};
    }

    void refresh(const QVector<Commit> &cmts)
    {
        beginResetModel();
        m_rows = cmts;
        endResetModel();
    }

private:
    QVector<Commit> m_rows;
};

class CommitDelegate : public QStyledItemDelegate
{
public:
    CommitDelegate(QObject *parent)
        : QStyledItemDelegate(parent)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &opt, const QModelIndex &index) const override
    {
        auto commit = index.data(CommitListModel::CommitRole).value<Commit>();
        if (commit.hash.isEmpty()) {
            return;
        }

        QStyleOptionViewItem options = opt;
        initStyleOption(&options, index);

        options.text = QString();
        QStyledItemDelegate::paint(painter, options, index);

        constexpr int lineHeight = 2;
        QFontMetrics fm = opt.fontMetrics;

        QRect prect = opt.rect;

        const int ascent = (opt.fontMetrics.ascent() / 2);

        // draw line
        //        prect.setX(prect.x() + ascent + 2);
        //        auto sp = painter->pen();
        //        auto p = painter->pen();
        //        p.setWidth(2);
        //        painter->setPen(p);

        //        auto p1 = prect.bottomLeft();
        //        int w = opt.fontMetrics.ascent();
        //        int h = opt.rect.height();
        //        int r = w / 3;
        //        p1.ry() -= (h / 2) - r;

        //        painter->setRenderHint(QPainter::Antialiasing, true);

        //        QPoint pp = p1;
        //        pp.ry() -= 3 + 1;
        //        painter->drawLine(prect.topLeft(), pp);
        //        painter->drawEllipse(p1, r, r);
        //        auto p2 = p1;
        //        p2.ry() += r + 1;
        //        painter->drawLine(p2, prect.bottomLeft());

        //        painter->setRenderHint(QPainter::Antialiasing, false);

        //        painter->setPen(sp);

        // padding
        prect.setX(prect.x() + 5);
        prect.setY(prect.y() + lineHeight);

        // draw author on left
        QFont f = opt.font;
        f.setBold(true);
        painter->setFont(f);
        painter->drawText(prect, Qt::AlignLeft, commit.authorName);
        painter->setFont(opt.font);

        // draw author on right
        auto dt = QDateTime::fromSecsSinceEpoch(commit.authorDate);
        QString timestamp =
            (dt.date() == QDate::currentDate()) ? dt.time().toString(Qt::DefaultLocaleShortDate) : dt.date().toString(Qt::DefaultLocaleShortDate);
        painter->drawText(prect, Qt::AlignRight, timestamp);

        // draw commit hash
        auto fg = painter->pen();
        painter->setPen(Qt::gray);
        prect.setY(prect.y() + fm.height() + lineHeight);
        painter->drawText(prect, Qt::AlignLeft, QString::fromUtf8(commit.hash.left(7)));
        painter->setPen(fg);

        // draw msg
        prect.setY(prect.y() + fm.height() + lineHeight);
        auto elidedMsg = opt.fontMetrics.elidedText(commit.msg, Qt::ElideRight, prect.width());
        painter->drawText(prect, Qt::AlignLeft, elidedMsg);

        // draw separator
        painter->setPen(opt.palette.button().color());
        painter->drawLine(opt.rect.bottomLeft(), opt.rect.bottomRight());
        painter->setPen(fg);
    }

    QSize sizeHint(const QStyleOptionViewItem &opt, const QModelIndex &) const override
    {
        auto height = opt.fontMetrics.height();
        return QSize(0, height * 3 + (3 * 2));
    }
};

FileHistoryWidget::FileHistoryWidget(const QString &file, QWidget *parent)
    : QWidget(parent)
    , m_file(file)
{
    setLayout(new QVBoxLayout);

    m_backBtn.setText(i18n("Back"));
    m_backBtn.setIcon(QIcon::fromTheme(QStringLiteral("draw-arrow-back.svg")));
    connect(&m_backBtn, &QPushButton::clicked, this, &FileHistoryWidget::backClicked);
    layout()->addWidget(&m_backBtn);

    m_listView = new QListView;
    layout()->addWidget(m_listView);

    auto model = new CommitListModel(this);
    model->refresh(parseCommits(getFileHistory(file)));

    m_listView->setModel(model);
    connect(m_listView, &QListView::clicked, this, &FileHistoryWidget::itemClicked);

    m_listView->setItemDelegate(new CommitDelegate(this));
}

void FileHistoryWidget::itemClicked(const QModelIndex &idx)
{
    QProcess git;
    QFileInfo fi(m_file);
    git.setWorkingDirectory(fi.absolutePath());

    const auto commit = idx.data(CommitListModel::CommitRole).value<Commit>();

    QStringList args{QStringLiteral("diff"), QString::fromUtf8(commit.hash), QStringLiteral("--"), m_file};
    git.start(QStringLiteral("git"), args, QProcess::ReadOnly);
    if (git.waitForStarted() && git.waitForFinished(-1)) {
        if (git.exitStatus() != QProcess::NormalExit || git.exitCode() != 0) {
            return;
        }
        QByteArray contents = commit.msg.toUtf8();
        contents.append("\n\n");
        contents.append(git.readAllStandardOutput());
        // we send this signal to the parent, which will pass it on to
        // the GitWidget from where a temporary file is opened
        Q_EMIT commitClicked(fi.fileName(), /*QStringLiteral("XXXXXX %1.diff"),*/ contents);
    }
}
