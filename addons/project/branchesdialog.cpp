/*
    SPDX-FileCopyrightText: 2021 Waqar Ahmed <waqar.17a@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "branchesdialog.h"
#include "branchesdialogmodel.h"
#include "kateprojectpluginview.h"

#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QPainter>
#include <QSortFilterProxyModel>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QTreeView>
#include <QVBoxLayout>
#include <QWidget>
#include <QtConcurrentRun>

#include <KTextEditor/MainWindow>
#include <KTextEditor/Message>
#include <KTextEditor/View>

#include <KLocalizedString>

#include <drawing_utils.h>
#include <kfts_fuzzy_match.h>

class StyleDelegate : public HUDStyleDelegate
{
public:
    using HUDStyleDelegate::HUDStyleDelegate;

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QStyleOptionViewItem options = option;
        initStyleOption(&options, index);

        auto name = index.data().toString();

        QVector<QTextLayout::FormatRange> formats;
        QTextCharFormat fmt;
        fmt.setForeground(options.palette.link());
        fmt.setFontWeight(QFont::Bold);

        const auto itemType = (BranchesDialogModel::ItemType)index.data(BranchesDialogModel::ItemTypeRole).toInt();
        const bool branchItem = itemType == BranchesDialogModel::BranchItem;
        const int offset = branchItem ? 0 : 2;

        formats = kfts::get_fuzzy_match_formats(m_filterString, name, offset, fmt);

        if (!branchItem) {
            name = QStringLiteral("+ ") + name;
        }

        const int nameLen = name.length();
        int len = 6;
        if (branchItem) {
            const auto refType = (GitUtils::RefType)index.data(BranchesDialogModel::RefType).toInt();
            using RefType = GitUtils::RefType;
            if (refType == RefType::Head) {
                name.append(QStringLiteral(" local"));
            } else if (refType == RefType::Remote) {
                name.append(QStringLiteral(" remote"));
                len = 7;
            }
        }
        QTextCharFormat lf;
        lf.setFontItalic(true);
        lf.setForeground(Qt::gray);
        formats.append({nameLen, len, lf});

        painter->save();

        auto *style = options.widget->style();
        options.text = QString(); // clear old text
        style->drawControl(QStyle::CE_ItemViewItem, &options, painter, options.widget);

        // leave space for icon
        const int hMargin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, &option, option.widget);
        const int iconWidth = option.decorationSize.width() + (hMargin * 2);
        if (itemType == BranchesDialogModel::BranchItem) {
            options.rect.adjust(iconWidth, 0, 0, 0);
        } else {
            options.rect.adjust((hMargin * 2), 0, 0, 0);
        }
        Utils::paintItemViewText(painter, name, options, formats);

        painter->restore();
    }
};

BranchesDialog::BranchesDialog(QWidget *window, KateProjectPluginView *pluginView, QString projectPath)
    : HUDDialog(nullptr, window)
    , m_model(new BranchesDialogModel(this))
    , m_projectPath(projectPath)
    , m_pluginView(pluginView)
{
    setModel(m_model, FilterType::ScoredFuzzy, 0, Qt::DisplayRole, BranchesDialogModel::FuzzyScore);
    setDelegate(new StyleDelegate(this));
}

void BranchesDialog::openDialog(GitUtils::RefType r)
{
    m_lineEdit.setPlaceholderText(i18n("Select Branch..."));

    QVector<GitUtils::Branch> branches = GitUtils::getAllBranchesAndTags(m_projectPath, r);
    m_model->refresh(branches);

    reselectFirst();
    exec();
}

void BranchesDialog::slotReturnPressed(const QModelIndex &index)
{
    const auto branch = index.data().toString();
    const auto itemType = (BranchesDialogModel::ItemType)index.data(BranchesDialogModel::ItemTypeRole).toInt();
    Q_ASSERT(itemType == BranchesDialogModel::BranchItem);

    m_branch = branch;
    Q_EMIT branchSelected(branch);
}

void BranchesDialog::sendMessage(const QString &plainText, bool warn)
{
    // use generic output view
    QVariantMap genericMessage;
    genericMessage.insert(QStringLiteral("type"), warn ? QStringLiteral("Error") : QStringLiteral("Info"));
    genericMessage.insert(QStringLiteral("category"), i18n("Git"));
    genericMessage.insert(QStringLiteral("categoryIcon"), QIcon(QStringLiteral(":/icons/icons/sc-apps-git.svg")));
    genericMessage.insert(QStringLiteral("text"), plainText);
    Q_EMIT m_pluginView->message(genericMessage);
}
