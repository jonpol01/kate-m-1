/* This file is part of the KDE project
 *
 *  SPDX-FileCopyrightText: 2014 Gregor Mi <codestruct@posteo.org>
 *
 *  SPDX-License-Identifier: LGPL-2.0-or-later
 */

#pragma once

#include <QObject>

class Test1 : public QObject
{
    Q_OBJECT

public Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();

private Q_SLOTS:
    void testCommonParent();
    void testShellCheckParsing();
};

// kate: space-indent on; indent-width 4; replace-tabs on;
