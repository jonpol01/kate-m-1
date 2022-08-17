/*
    SPDX-FileCopyrightText: 2021 Waqar Ahmed <waqar.17a@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#ifndef FILEHISTORYWIDGET_H
#define FILEHISTORYWIDGET_H

#include <QListView>
#include <QProcess>
#include <QPushButton>
#include <QWidget>

class FileHistoryWidget : public QWidget
{
    Q_OBJECT
public:
    explicit FileHistoryWidget(const QString &gitDir, const QString &file, QWidget *parent = nullptr);
    ~FileHistoryWidget() override;

private Q_SLOTS:
    void itemClicked(const QModelIndex &idx);

private:
    void getFileHistory(const QString &file);

    QPushButton m_backBtn;
    QListView *m_listView;
    QProcess m_git;
    QString m_gitDir;

Q_SIGNALS:
    void backClicked();
    void commitClicked(const QByteArray &contents);
    void errorMessage(const QString &msg, bool warn);
};

#endif // FILEHISTORYWIDGET_H
