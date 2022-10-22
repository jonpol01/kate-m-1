/*
    SPDX-FileCopyrightText: 2022 Waqar Ahmed <waqar.17a@gmail.com>

    SPDX-License-Identifier: LGPL-2.0-or-later
*/
#include "kate_view_mgmt_tests.h"
#include "katemainwindow.h"
#include "kateviewspace.h"
#include "ktexteditor_utils.h"

#include <KTextEditor/Editor>

#include <QCommandLineParser>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>

QTEST_MAIN(KateViewManagementTests)

static bool viewspaceContainsView(KateViewSpace *vs, KTextEditor::View *v)
{
    return vs->hasDocument(v->document());
}

KateViewManagementTests::KateViewManagementTests(QObject *)
{
    m_tempdir = new QTemporaryDir;
    QVERIFY(m_tempdir->isValid());

    // ensure we use some dummy config
    KConfig::setMainConfigName(m_tempdir->path() + QStringLiteral("/testconfigfilerc"));

    // create KWrite variant to avoid plugin loading!
    static QCommandLineParser parser;
    app = std::make_unique<KateApp>(parser, KateApp::ApplicationKWrite, m_tempdir->path());
}

void KateViewManagementTests::testSingleViewspaceDoesntCloseWhenLastViewClosed()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // Test that if we have 1 viewspaces then
    // closing the last view doesn't close the viewspace

    auto mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();

    // Initially we have one viewspace and one view
    QCOMPARE(vm->m_views.size(), 1);
    QCOMPARE(vm->m_viewSpaceList.size(), 1);

    // close active view
    vm->closeView(vm->activeView());

    // still same
    QCOMPARE(vm->m_views.size(), 0);
    QCOMPARE(vm->m_viewSpaceList.size(), 1);
}

void KateViewManagementTests::testViewspaceClosesWhenLastViewClosed()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // Test that if we have greater than 1 viewspaces then
    // closing the last view in a viewspace closes that view
    // space

    auto mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();

    // Initially we have one viewspace
    QCOMPARE(vm->m_viewSpaceList.size(), 1);

    vm->slotSplitViewSpaceVert();

    // Now we have two
    QCOMPARE(vm->m_viewSpaceList.size(), 2);
    // and two views, one in each viewspace
    QCOMPARE(vm->m_views.size(), 2);

    // close active view
    vm->closeView(vm->activeView());

    QCOMPARE(vm->m_views.size(), 1);
    QCOMPARE(vm->m_viewSpaceList.size(), 1);
}

void KateViewManagementTests::testViewspaceClosesWhenThereIsWidget()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // Test that if we have greater than 1 viewspaces then
    // closing the last view in a viewspace closes that view
    // space

    KateMainWindow *mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();
    QCOMPARE(vm->m_viewSpaceList.size(), 1);

    vm->slotSplitViewSpaceVert();
    // Now we have two viewspaces
    QCOMPARE(vm->m_viewSpaceList.size(), 2);
    auto *leftVS = vm->m_viewSpaceList[0];
    auto *rightVS = vm->m_viewSpaceList[1];
    QCOMPARE(rightVS, vm->activeViewSpace());

    // add a widget
    QPointer<QWidget> widget = new QWidget;
    Utils::addWidget(widget, app->activeMainWindow());
    // active viewspace remains the same
    QCOMPARE(vm->m_viewSpaceList[1], vm->activeViewSpace());
    // the widget should be active in activeViewSpace
    QCOMPARE(vm->activeViewSpace()->currentWidget(), widget);
    // activeView() should be nullptr
    QVERIFY(!vm->activeView());

    // there should still be 2 views
    // widget is not counted in views
    QCOMPARE(vm->m_views.size(), 2);

    const auto sortedViews = vm->views();
    QCOMPARE(sortedViews.size(), 2);

    // ensure we know where both of the views are and
    // we close the right one
    QVERIFY(viewspaceContainsView(rightVS, sortedViews.at(0)));
    QVERIFY(viewspaceContainsView(leftVS, sortedViews.at(1)));

    // Make the KTE::view in right viewspace active
    vm->activateView(sortedViews.at(0));
    QVERIFY(vm->activeView() == sortedViews.at(0));

    // close active view
    vm->closeView(vm->activeView());

    // one view left, but two viewspaces
    QCOMPARE(vm->m_views.size(), 1);
    QCOMPARE(vm->m_viewSpaceList.size(), 2);

    // close the widget!
    QVERIFY(vm->removeWidget(widget));
    QTest::qWait(100);
    QVERIFY(!widget); // widget should be gone

    // only one viewspace should be left now
    QCOMPARE(vm->m_views.size(), 1);
    QCOMPARE(vm->m_viewSpaceList.size(), 1);
}

void KateViewManagementTests::testMoveViewBetweenViewspaces()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    KateMainWindow *mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();
    vm->slotSplitViewSpaceVert();

    // we have two viewspaces with 2 views
    QCOMPARE(vm->m_viewSpaceList.size(), 2);
    QCOMPARE(vm->m_views.size(), 2);

    auto src = vm->activeViewSpace();
    auto dest = vm->m_viewSpaceList.front();
    QVERIFY(src != dest);
    vm->moveViewToViewSpace(dest, src, vm->activeView()->document());
    QTest::qWait(100);

    // after moving we should have 2 views but only one viewspace left
    QCOMPARE(vm->m_viewSpaceList.size(), 1);
    QCOMPARE(vm->m_views.size(), 2);
}

void KateViewManagementTests::testTwoMainWindowsCloseInitialDocument1()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // get first main window
    KateMainWindow *first = app->activeKateMainWindow();
    QVERIFY(first);

    // create a second one
    KateMainWindow *second = app->newMainWindow();
    QVERIFY(second);

    // close the initial document
    QVERIFY(app->closeDocument(first->viewManager()->activeView()->document()));

    // create a new document, this did crash due to empty view space
    second->viewManager()->slotDocumentNew();
}

void KateViewManagementTests::testTwoMainWindowsCloseInitialDocument2()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // get first main window
    KateMainWindow *first = app->activeKateMainWindow();
    QVERIFY(first);

    // create a second one
    KateMainWindow *second = app->newMainWindow();
    QVERIFY(second);

    // close the initial document tab in second window
    second->viewManager()->activeViewSpace()->closeDocument(first->viewManager()->activeView()->document());

    // create a new document, this did crash due to empty view space
    second->viewManager()->slotDocumentNew();
}

void KateViewManagementTests::testTwoMainWindowsCloseInitialDocument3()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // get first main window
    KateMainWindow *first = app->activeKateMainWindow();
    QVERIFY(first);

    // create a second one
    KateMainWindow *second = app->newMainWindow();
    QVERIFY(second);

    // close the initial document tab in second window
    second->viewManager()->closeView(second->viewManager()->activeView());

    // create a new document, this did crash due to empty view space
    second->viewManager()->slotDocumentNew();
}

void KateViewManagementTests::testTabLRUWithWidgets()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();

    // get first main window
    KateMainWindow *mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();
    auto vs = vm->activeViewSpace();

    auto view1 = vm->createView(nullptr);
    auto view2 = vm->createView(nullptr);

    QCOMPARE(vs->m_registeredDocuments.size(), 3);
    // view2 should be active
    QCOMPARE(vm->activeView(), view2);

    // Add a widget
    QWidget *widget = new QWidget;
    widget->setObjectName(QStringLiteral("widget"));
    Utils::addWidget(widget, app->activeMainWindow());
    QCOMPARE(vs->currentWidget(), widget);
    // There should be no activeView
    QVERIFY(!vm->activeView());

    QCOMPARE(vs->m_registeredDocuments.size(), 4);
    // activate view1
    vm->activateView(view1->document());
    QVERIFY(vm->activeView());

    // activate widget again
    QCOMPARE(vs->currentWidget(), nullptr);
    vm->activateWidget(widget);
    QCOMPARE(vs->currentWidget(), widget);
    QVERIFY(!vm->activeView());

    // close "widget"
    vm->slotDocumentClose();

    // on closing the widget we should fallback to view1
    // as it was the last visited view
    QCOMPARE(vs->m_registeredDocuments.size(), 3);
    QCOMPARE(vm->activeView(), view1);
    vm->slotDocumentClose();
    // and view2 after closing view1
    QCOMPARE(vs->m_registeredDocuments.size(), 2);
    QCOMPARE(vm->activeView(), view2);
}

void KateViewManagementTests::testViewChangedEmittedOnAddWidget()
{
    auto kmw = app->activeMainWindow();
    QSignalSpy spy(kmw, &KTextEditor::MainWindow::viewChanged);
    Utils::addWidget(new QWidget, kmw);
    spy.wait();
    QVERIFY(spy.count() == 1);
}

void KateViewManagementTests::testWidgetAddedEmittedOnAddWidget()
{
    QSignalSpy spy(app->activeMainWindow()->window(), SIGNAL(widgetAdded(QWidget *)));
    Utils::addWidget(new QWidget, app->activeMainWindow());
    spy.wait();
    QVERIFY(spy.count() == 1);
}

void KateViewManagementTests::testWidgetRemovedEmittedOnRemoveWidget()
{
    auto mw = app->activeMainWindow()->window();
    QSignalSpy spy(mw, SIGNAL(widgetRemoved(QWidget *)));
    auto w = new QWidget;
    Utils::addWidget(w, app->activeMainWindow());
    QMetaObject::invokeMethod(mw, "removeWidget", Q_ARG(QWidget *, w));
    spy.wait();
    QVERIFY(spy.count() == 1);
}

void KateViewManagementTests::testActivateNotAddedWidget()
{
    auto kmw = app->activeMainWindow();
    auto mw = app->activeMainWindow()->window();
    QSignalSpy spy(mw, SIGNAL(widgetAdded(QWidget *)));
    QSignalSpy spy1(kmw, &KTextEditor::MainWindow::viewChanged);
    auto w = new QWidget;
    QMetaObject::invokeMethod(mw, "activateWidget", Q_ARG(QWidget *, w));
    spy.wait();
    spy1.wait();
    QVERIFY(spy.count() == 1);
    QVERIFY(spy1.count() == 1);
}

void KateViewManagementTests::testBug460613()
{
    // See the bug for details
    // This test basically splits into two viewspaces
    // Both viewspaces have 1 view
    // Adds a new doc to first viewsace and activates
    // and then adds the same doc to second viewspace
    // without activation
    // TEST: closing the doc should only close it
    // in the first viewspace, not second as well!
    // TEST: closing the doc without view should work

    app->sessionManager()->sessionNew();
    KateMainWindow *mw = app->activeKateMainWindow();
    auto vm = mw->viewManager();
    vm->createView(nullptr);

    vm->slotSplitViewSpaceVert();
    QCOMPARE(vm->m_viewSpaceList.size(), 2);

    auto vs1 = *vm->m_viewSpaceList.begin();
    auto vs2 = *(vm->m_viewSpaceList.begin() + 1);

    vs1->createNewDocument();
    QCOMPARE(vm->activeView(), vs1->currentView());

    KTextEditor::Document *doc = vm->activeView()->document();
    vs2->registerDocument(doc); // registered, but has no view yet

    vm->slotDocumentClose();

    // The doc should still be there in the second viewspace
    QVERIFY(vs2->m_registeredDocuments.contains(doc));

    // Try to close the doc in second viewspace
    vs2->closeDocument(doc);
    QVERIFY(!vs2->hasDocument(doc));
}

void KateViewManagementTests::testWindowsClosesDocuments()
{
    app->sessionManager()->sessionNew();
    app->activeKateMainWindow()->viewManager()->slotDocumentNew();
    QCOMPARE(app->documentManager()->documentList().size(), 1);

    // get first main window
    KateMainWindow *first = app->activeKateMainWindow();
    QVERIFY(first);

    // create a second one, shall not create a new document
    KateMainWindow *second = app->newMainWindow();
    QCOMPARE(app->documentManager()->documentList().size(), 1);
    QVERIFY(second);

    // second window shall have a new document
    second->viewManager()->slotDocumentNew();
    QCOMPARE(app->documentManager()->documentList().size(), 2);

    // if we close the second window, the second document shall be gone
    delete second;
    QCOMPARE(app->documentManager()->documentList().size(), 1);
}
