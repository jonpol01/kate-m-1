/* This file is part of the KDE libraries
   Copyright (C) 2002, 2003 Anders Lund <anders.lund@lund.tdcadsl.dk>
   Copyright (C) 2001 Joseph Wenninger <jowenn@kde.org>

   Based on work of:
     Copyright (C) 1999 Jochen Wilhelmy <digisnap@cs.tu-berlin.de>

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public
   License version 2 as published by the Free Software Foundation.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public License
   along with this library; see the file COPYING.LIB.  If not, write to
   the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.
*/

//BEGIN Really??
// Copyright (c) 2000-2001 Charles Samuels <charles@kde.org>
// Copyright (c) 2000-2001 Neil Stevens <multivac@fcmail.com>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
// AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIAB\ILITY, WHETHER IN
// AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
//END

// $Id$

//BEGIN Includes
#include "katedialogs.h"
#include "katedialogs.moc"

#include "katesyntaxdocument.h"
#include "hlparamedit.h"
#include "katedocument.h"
#include "katehighlightdownload.h"
#include "attribeditor.h"
#include "katefactory.h"
#include "kateconfig.h"

#include <kapplication.h>
#include <kspell.h>
#include <kbuttonbox.h>
#include <kcharsets.h>
#include <kcolorcombo.h>
#include <kcolordialog.h>
#include <kcombobox.h>
#include <kdebug.h>
#include <kprocess.h>
#include <kglobal.h>
#include <kglobalsettings.h>
#include <kiconloader.h>
#include <klineedit.h>
#include <klocale.h>
#include <kmessagebox.h>
#include <knuminput.h>
#include <kpopupmenu.h>
#include <krun.h>
#include <kstandarddirs.h>

#include <qcheckbox.h>
#include <qcombobox.h>
#include <qgroupbox.h>
#include <qhbox.h>
#include <qheader.h>
#include <qhgroupbox.h>
#include <qlabel.h>
#include <qlayout.h>
#include <qlineedit.h>
#include <qlistview.h>
#include <qmap.h>
#include <qpainter.h>
#include <qpointarray.h>
#include <qpushbutton.h>
#include <qtoolbutton.h>
#include <qvbox.h>
#include <qvgroupbox.h>
#include <qwhatsthis.h>
#include <qwidgetstack.h>
//END

// FIXME THE isSomethingSet() calls should partly be replaced by itemSet(XYZ) and
// there is a need for an itemUnset(XYZ)
// a) is done. itemUnset(N) == !itemSet(N); right???
// there is a unsetItem(), just (not all logic) called "KateAttribute::clearAttribute(int)";

using namespace Kate;

#define TAG_DETECTCHAR "DetectChar"
#define TAG_DETECT2CHARS "Detect2Chars"
#define TAG_RANGEDETECT "RangeDetect"
#define TAG_STRINGDETECT "StringDetect"
#define TAG_ANYCHAR "AnyChar"
#define TAG_REGEXPR "RegExpr"
#define TAG_INT "Int"
#define TAG_FLOAT "Float"
#define TAG_KEYWORD "keyword"

//BEGIN PluginListItem
PluginListItem::PluginListItem(const bool _exclusive, bool _checked, PluginInfo *_info, QListView *_parent)
  : QCheckListItem(_parent, _info->service->name(), CheckBox)
  , mInfo(_info)
  , silentStateChange(false)
  , exclusive(_exclusive)
{
  setChecked(_checked);
  if(_checked) static_cast<PluginListView *>(listView())->count++;
}


void PluginListItem::setChecked(bool b)
{
  silentStateChange = true;
  setOn(b);
  silentStateChange = false;
}

void PluginListItem::stateChange(bool b)
{
  if(!silentStateChange)
    static_cast<PluginListView *>(listView())->stateChanged(this, b);
}

void PluginListItem::paintCell(QPainter *p, const QColorGroup &cg, int a, int b, int c)
{
  if(exclusive) myType = RadioButton;
  QCheckListItem::paintCell(p, cg, a, b, c);
  if(exclusive) myType = CheckBox;
}
//END

//BEGIN PluginListView
PluginListView::PluginListView(unsigned _min, unsigned _max, QWidget *_parent, const char *_name)
  : KListView(_parent, _name)
  , hasMaximum(true)
  , max(_max)
  , min(_min <= _max ? _min : _max)
  , count(0)
{
}

PluginListView::PluginListView(unsigned _min, QWidget *_parent, const char *_name)
  : KListView(_parent, _name)
  , hasMaximum(false)
  , min(_min)
  , count(0)
{
}

PluginListView::PluginListView(QWidget *_parent, const char *_name)
  : KListView(_parent, _name)
  , hasMaximum(false)
  , min(0)
  , count(0)
{
}

void PluginListView::clear()
{
  count = 0;
  KListView::clear();
}

void PluginListView::stateChanged(PluginListItem *item, bool b)
{
  if(b)
  {
    count++;
    emit stateChange(item, b);

    if(hasMaximum && count > max)
    {
      // Find a different one and turn it off

      QListViewItem *cur = firstChild();
      PluginListItem *curItem = dynamic_cast<PluginListItem *>(cur);

      while(cur == item || !curItem || !curItem->isOn())
      {
        cur = cur->nextSibling();
        curItem = dynamic_cast<PluginListItem *>(cur);
      }

      curItem->setOn(false);
    }
  }
  else
  {
    if(count == min)
    {
      item->setChecked(true);
    }
    else
    {
      count--;
      emit stateChange(item, b);
    }
  }
}
//END

//BEGIN PluginConfigPage
 PluginConfigPage::PluginConfigPage (QWidget *parent, KateDocument *doc) : Kate::ConfigPage (parent, "")
{
  m_doc = doc;

  // sizemanagment
  QGridLayout *grid = new QGridLayout( this, 1, 1 );

  PluginListView* listView = new PluginListView(0, this);
  listView->addColumn(i18n("Name"));
  listView->addColumn(i18n("Description"));
  listView->addColumn(i18n("Author"));
  listView->addColumn(i18n("License"));
  connect(listView, SIGNAL(stateChange(PluginListItem *, bool)), this, SLOT(stateChange(PluginListItem *, bool)));

  grid->addWidget( listView, 0, 0);

  for (uint i=0; i<m_doc->s_plugins.count(); i++)
  {
    PluginListItem *item = new PluginListItem(false, m_doc->s_plugins.at(i)->load, m_doc->s_plugins.at(i), listView);
    item->setText(0, m_doc->s_plugins.at(i)->service->name());
    item->setText(1, m_doc->s_plugins.at(i)->service->comment());
    item->setText(2, "");
    item->setText(3, "");
  }
}

PluginConfigPage::~PluginConfigPage ()
{
}


 void PluginConfigPage::stateChange(PluginListItem *item, bool b)
{
  if(b)
    loadPlugin(item);
  else
    unloadPlugin(item);
  emit changed();
}

void PluginConfigPage::loadPlugin (PluginListItem *item)
{
  item->info()->load = true;
  for (uint z=0; z < KateFactory::documents()->count(); z++)
    KateFactory::documents()->at(z)->loadAllEnabledPlugins ();

  item->setOn(true);
}

void PluginConfigPage::unloadPlugin (PluginListItem *item)
{
  item->info()->load = false;
  for (uint z=0; z < KateFactory::documents()->count(); z++)
    KateFactory::documents()->at(z)->loadAllEnabledPlugins ();

  item->setOn(false);
}
//END

//BEGIN HlConfigPage
HlConfigPage::HlConfigPage (QWidget *parent, KateDocument *doc) : Kate::ConfigPage (parent, "")
{
  m_doc = doc;
  m_ready = false;
  m_hlNumber = doc->hlMode();
}

HlConfigPage::~HlConfigPage ()
{
}

void HlConfigPage::showEvent ( QShowEvent * )
{
  if (!m_ready)
  {
    QGridLayout *grid = new QGridLayout( this, 1, 1 );

    hlManager = HlManager::self();

    defaultStyleList.setAutoDelete(true);
    hlManager->getDefaults(defaultStyleList);

    hlDataDict.setAutoDelete( true );

    page = new HighlightDialogPage(hlManager, &defaultStyleList, &hlDataDict, m_hlNumber, this);
    grid->addWidget( page, 0, 0);
    connect( page, SIGNAL( configChanged() ), this, SLOT( slotChanged() ) );
    page->show ();

    m_ready = true;
  }

  QWidget::show ();
}

void HlConfigPage::apply ()
{
  if (m_ready)
  {
    kdDebug()<<"HlConfigPage::apply()"<<endl;
    page->saveData();
    hlManager->setDefaults(defaultStyleList);
// TODO anders: Make sure this works ;)
    QIntDictIterator<HlData> it( hlDataDict );
    for ( ; it.current(); ++it )
    {
      hlManager->getHl( it.currentKey() )->setData( it.current() );
      hlManager->emitChanged( (uint)it.currentKey() );
    }
  }
}

void HlConfigPage::reload ()
{
}
//END HlConfigPage

//BEGIN HighlightDialogPage
HighlightDialogPage::HighlightDialogPage(HlManager *_hlManager,
                              KateAttributeList *styleList,
                              HlDataDict* hlDataD,
                              int hlNumber,QWidget *parent, const char *name)
   :QTabWidget(parent,name),defaultItemStyleList(styleList),hlDataDict(hlDataD),hlData(0L),hlManager(_hlManager)

{

  // defaults =========================================================

  QVBox *page1 = new QVBox ( this );
  addTab(page1,i18n("&Default Styles"));
  page1->setSpacing( KDialog::spacingHint() );
  page1->setMargin( KDialog::marginHint() );

  QColor normalcol( defaultItemStyleList->at(0)->textColor() );
  StyleListView *lvDefStyles = new StyleListView( page1, false, normalcol );
//   for ( int i = 0; i < hlManager->defaultStyles(); i++ )
//   {
// //     kdDebug()<<i<<" itemsSet: "<<defaultItemStyleList->at( i )->itemsSet()<<endl;
//     lvDefStyles->insertItem( new StyleListItem( lvDefStyles, hlManager->defaultStyleName(i),
//                                                 defaultItemStyleList->at( i ) ) );
//   }
  // highlight modes =====================================================

  QVBox *page2 = new QVBox( this );
  addTab(page2,i18n("Highlight &Modes"));
  page2->setSpacing( KDialog::spacingHint() );
  page2->setMargin( KDialog::marginHint() );

  // hl chooser
  QHBox *hbHl = new QHBox( page2 );
  hbHl->setSpacing( KDialog::spacingHint() );
  QLabel *lHl = new QLabel( i18n("H&ighlight:"), hbHl );
  hlCombo = new QComboBox( false, hbHl );
  lHl->setBuddy( hlCombo );
  connect( hlCombo, SIGNAL(activated(int)),
           this, SLOT(hlChanged(int)) );
  for( int i = 0; i < hlManager->highlights(); i++) {
    if (hlManager->hlSection(i).length() > 0)
      hlCombo->insertItem(hlManager->hlSection(i) + QString ("/") + hlManager->hlName(i));
    else
      hlCombo->insertItem(hlManager->hlName(i));
  }
  hlCombo->setCurrentItem(0);
  QPushButton *btnEdit = new QPushButton( i18n("&Edit..."), hbHl );
  connect( btnEdit, SIGNAL(clicked()), this, SLOT(hlEdit()) );

  QGroupBox *gbProps = new QGroupBox( 1, Qt::Horizontal, i18n("Properties"), page2 );
  //gbProps->setSpacing( spacing );

  // file & mime types
  QHBox *hbFE = new QHBox( gbProps);
  QLabel *lFileExts = new QLabel( i18n("File e&xtensions:"), hbFE );
  wildcards  = new QLineEdit( hbFE );
  lFileExts->setBuddy( wildcards );
  connect( wildcards, SIGNAL( textChanged ( const QString & ) ), this, SLOT( slotChanged() ) );

  QHBox *hbMT = new QHBox( gbProps );
  QLabel *lMimeTypes = new QLabel( i18n("MIME &types:"), hbMT);
  mimetypes = new QLineEdit( hbMT );
  connect( mimetypes, SIGNAL( textChanged ( const QString & ) ), this, SLOT( slotChanged() ) );
  lMimeTypes->setBuddy( mimetypes );

  QHBox *hbMT2 = new QHBox( gbProps );
  QLabel *lprio = new QLabel( i18n("Prio&rity:"), hbMT2);
  priority = new KIntNumInput( hbMT2 );
  connect( priority, SIGNAL( valueChanged ( int ) ), this, SLOT( slotChanged() ) );
  lprio->setBuddy( priority );

  QToolButton *btnMTW = new QToolButton(hbMT);
  btnMTW->setIconSet(QIconSet(SmallIcon("wizard")));
  connect(btnMTW, SIGNAL(clicked()), this, SLOT(showMTDlg()));
  //hbmt->setStretchFactor(mimetypes, 1);

  // styles listview
  QLabel *lSt = new QLabel( i18n("Context &styles:"), gbProps );
  lvStyles = new StyleListView( gbProps, true, normalcol );
  lSt->setBuddy( lvStyles );

  lvStyles->hide();
  new QLabel("<b><font size=6>DISABLED</font></b>", gbProps);

  // download/new buttons
  QHBox *hbBtns = new QHBox( page2 );
  ((QBoxLayout*)hbBtns->layout())->addStretch(1); // hmm.
  hbBtns->setSpacing( KDialog::spacingHint() );
  QPushButton *btnDl = new QPushButton(i18n("Do&wnload..."), hbBtns);
  connect( btnDl, SIGNAL(clicked()), this, SLOT(hlDownload()) );
  QPushButton *btnNew = new QPushButton(i18n("&New..."), hbBtns);
  connect( btnNew, SIGNAL(clicked()), this, SLOT(hlNew()) );

  hlCombo->setCurrentItem( hlNumber );
  hlChanged(hlNumber);

  // jowenn, feel free to edit the below texts
  QWhatsThis::add( lvDefStyles, i18n("This list displays the default styles, used by all syntax highlight modes, and offers the means to edit them. The context name reflects the current style settings.<p>To edit using the keyboard, press <strong>&lt;SPACE&gt;</strong> and choose a property from the popup menu.<p>To edit the colors, click the colored squares, or select the color to edit from the popup menu.") );

  QWhatsThis::add( hlCombo,   i18n("Choose a <em>Syntax Highlight mode</em> from this list to view its properties below.") );
  QWhatsThis::add( btnEdit,   i18n("Click this button to edit the currently selected syntax highlight mode using the Highlight Mode Editor(tm)") );
  QWhatsThis::add( wildcards, i18n("The list of file extensions used to determine which files to highlight using the current syntax highlight mode.") );
  QWhatsThis::add( mimetypes, i18n("The list of Mime Types used to determine which files to highlight using the current highlight mode.<p>Click the wizard button on the left of the entry field to display the MimeType selection dialog.") );
  QWhatsThis::add( btnMTW,    i18n("Display a dialog with a list of all available mime types to choose from.<p>The <strong>File Extensions</strong> entry will automatically be edited as well.") );
  QWhatsThis::add( lvStyles,  i18n("This list displays the contexts of the current syntax highlight mode and offers the means to edit them. The context name reflects the current style settings.<p>To edit using the keyboard, press <strong>&lt;SPACE&gt;</strong> and choose a property from the popup menu.<p>To edit the colors, click the colored squares, or select the color to edit from the popup menu.") );
  QWhatsThis::add( btnDl,     i18n("Click this button to download new or updated syntax highlight descriptions from the Kate website.") );
  QWhatsThis::add( btnNew,    i18n("Click this button to create a new syntax highlight mode using the Highlight Mode Editor(tm)") );

  // not finished for 3.0
  btnNew->hide();
  btnEdit->hide();

}

void HighlightDialogPage::hlChanged(int z)
{
/*  writeback();

  if ( ! hlDataDict->find( z ) ) {
    kdDebug()<<"getting hl data ("<<z<<") - count is "<<hlDataDict->count()<<endl;
    hlDataDict->insert( z, hlManager->getHl( z )->getData() );}

  hlData = hlDataDict->find( z );
  wildcards->setText(hlData->wildcards);
  mimetypes->setText(hlData->mimetypes);
  priority->setValue(hlData->priority);

  lvStyles->clear();
  int xx(0);
  for (ItemData *itemData = hlData->itemDataList.first();
          itemData != 0L;
             itemData = hlData->itemDataList.next()) {
   kdDebug()<<xx++<<": "<<itemData->name.latin1()<<" :defStyleNum "<<itemData->defStyleNum<<endl;
    lvStyles->insertItem( new StyleListItem( lvStyles, i18n(itemData->name.latin1()),
                                 defaultItemStyleList->at(itemData->defStyleNum), itemData ) );
  }*/
}

void HighlightDialogPage::writeback() {
  if (hlData) {
    hlData->wildcards = wildcards->text();
    hlData->mimetypes = mimetypes->text();
    hlData->priority = priority->value();

    QListViewItemIterator it ( lvStyles );
    while( it.current() )
    {
      ((StyleListItem*)it.current())->updateStyle();
      kdDebug()<<"updated "<<it.current()->text(0)<<endl;
      ++it;
    }
  }
}

void HighlightDialogPage::saveData() {
  kdDebug(13010)<<"HighlightDialogPage::saveData()"<<endl;
  writeback();
}


void HighlightDialogPage::hlEdit() {
  HlEditDialog diag(0,this,"hlEdit", true,hlData);
  diag.exec();
}

void HighlightDialogPage::hlNew() {
  HlEditDialog diag(0,this,"hlEdit",true,0);
  diag.exec();
}


void HighlightDialogPage::hlDownload()
{
  HlDownloadDialog diag(this,"hlDownload",true);
  diag.exec();
}

void HighlightDialogPage::showMTDlg()
{

  QString text = i18n("Select the MimeTypes you want highlighted using the '%1' syntax highlight rules.\nPlease note that this will automatically edit the associated file extensions as well.").arg( hlCombo->currentText() );
  QStringList list = QStringList::split( QRegExp("\\s*;\\s*"), mimetypes->text() );
  KMimeTypeChooserDlg *d = new KMimeTypeChooserDlg( this, i18n("Select Mime Types"), text, list );
  if ( d->exec() == KDialogBase::Accepted ) {
    // do some checking, warn user if mime types or patterns are removed.
    // if the lists are empty, and the fields not, warn.
    wildcards->setText(d->patterns().join(";"));
    mimetypes->setText(d->mimeTypes().join(";"));
  }
}

void HighlightDialogPage::slotChanged()
{
  emit configChanged();
}
//END

//BEGIN HlEditDialog
/******************************************************************************/
/*                     HlEditDialog implementation                            */
/******************************************************************************/
HlEditDialog::HlEditDialog(HlManager *,QWidget *parent, const char *name, bool modal,HlData *data)
  :KDialogBase(KDialogBase::Swallow, i18n("Highlight Conditions"), Ok|Cancel, Ok, parent, name, modal)
{
  QTabWidget *tabWid=new QTabWidget(this);

/* attributes */
  tabWid->addTab(attrEd=new AttribEditor(tabWid),i18n("Attributes"));

/*Contextstructure */
  currentItem=0;
    transTableCnt=0;
  QHBox *wid=new QHBox(tabWid);
  QVBox *lbox=new QVBox(wid);
    contextList=new KListView(lbox);
    contextList->setRootIsDecorated(true);
    contextList->addColumn(i18n("Syntax Structure"));
    contextList->setSorting(-1);
    QHBox *bbox=new QHBox(lbox);
    QPushButton *addContext=new QPushButton(i18n("New Context"),bbox);
    QPushButton *addItem=new QPushButton(i18n("New Item"),bbox);
    QVGroupBox *opt  = new QVGroupBox( i18n("Options"), wid);
    stack=new QWidgetStack(opt);
    initContextOptions(contextOptions=new QVBox(stack));
    stack->addWidget(contextOptions,HlEContext);
    initItemOptions(itemOptions=new QVBox(stack));
    stack->addWidget(itemOptions,HlEItem);
    stack->raiseWidget(HlEContext);
    tabWid->addTab(wid,i18n("Structure"));
    setMainWidget(tabWid);
    if (data!=0) loadFromDocument(data);
    else newDocument();

/* context structure connects */
    connect(contextList,SIGNAL(currentChanged( QListViewItem*)),this,SLOT(currentSelectionChanged ( QListViewItem * )));
    connect(addContext,SIGNAL(clicked()),this,SLOT(contextAddNew()));
    connect(addItem,SIGNAL(clicked()),this,SLOT(ItemAddNew()));

/* attribute setting - connects */

    connect(tabWid,SIGNAL(currentChanged(QWidget*)),this,SLOT(pageChanged(QWidget*)));
    }

void HlEditDialog::pageChanged(QWidget *widget)
{
  if (widget/*==*/)
  {
      ContextAttribute->clear();
      ItemAttribute->clear();
      ContextAttribute->insertStringList(attrEd->attributeNames());
      ItemAttribute->insertStringList(attrEd->attributeNames());
  }
}

void HlEditDialog::newDocument()
{
  KStandardDirs *dirs = KGlobal::dirs();
  QStringList list=dirs->findAllResources("data","katepart/syntax/syntax.template",false,true);
  for ( QStringList::Iterator it = list.begin(); it != list.end(); ++it )
      {
        HlData data("","",*it,0);
        loadFromDocument(&data);
        return;
      }
  KMessageBox::error(this,i18n("Can't find template file"));
}


void HlEditDialog::initContextOptions(QVBox *co)
{
  if( co!=0)
    {
        QHBox *tmp = new QHBox(co);
        (void) new QLabel(i18n("Description:"),tmp);
        ContextDescr=new QLineEdit(tmp);
        tmp= new QHBox(co);
        (void) new QLabel(i18n("Attribute:"),tmp);
        ContextAttribute=new QComboBox(tmp);
        tmp= new QHBox(co);
        (void) new QLabel(i18n("LineEnd:"),tmp);
        ContextLineEnd = new QComboBox(tmp);
  (ContextPopCount=new KIntNumInput(tmp))->setRange(1,20,1,false);
  connect(ContextDescr,SIGNAL(textChanged(const QString&)),this,SLOT(contextDescrChanged(const QString&)));
        connect(ContextLineEnd,SIGNAL(activated(int)),this,SLOT(contextLineEndChanged(int)));
        connect(ContextAttribute,SIGNAL(activated(int)),this,SLOT(contextAttributeChanged(int)));
  connect(ContextPopCount,SIGNAL(valueChanged(int)),this,SLOT(ContextPopCountChanged(int)));
    }
   else
     kdDebug(13010)<<"initContextOptions: Widget is 0"<<endl;
}


void HlEditDialog::insertTranslationList(QString tag, QString trans,int length)
  {
    ItemInfo data(trans,length);
    id2tag.insert(transTableCnt,tag);
    id2info.insert(transTableCnt,data);
    tag2id.insert(tag,transTableCnt);
    transTableCnt++;
  }


void HlEditDialog::initItemOptions(QVBox *co)
{
  if (co!=0)
    {
        QHBox *tmp = new QHBox(co);
        (void) new QLabel(i18n("Type:"),tmp);
        ItemType = new QComboBox(tmp);
        tmp= new QHBox(co);
        (void) new QLabel(i18n("Parameter:"),tmp);
        ItemParameter=  new HLParamEdit(tmp);
        tmp= new QHBox(co);
        (void) new QLabel(i18n("Attribute:"),tmp);
        ItemAttribute= new QComboBox(tmp);
        (void) new QLabel(i18n("Context switch:"),tmp);
        ItemContext = new QComboBox(tmp);
  (ItemPopCount=new KIntNumInput(tmp))->setRange(1,20,1,false);
  //ItemPopCount->hide();

        co->setSpacing(15);
        new QPushButton(i18n("Delete This Item"),co);

  /* init translation lists */
  insertTranslationList("DetectChar","DetectChar",1);
        insertTranslationList("Detect2Chars","Detect2Chars",2);
  insertTranslationList("RangeDetect","RangeDetect",2);
  insertTranslationList("StringDetect","StringDetect",-1);
  insertTranslationList("AnyChar","AnyChar",-1);
  insertTranslationList("RegExpr","RegExpr",-1);
  insertTranslationList("Int","Int",0);
  insertTranslationList("Float","Float",0);
  insertTranslationList("keyword","keyword",0);
        ItemType->clear();
        for (int i=0; i<transTableCnt; i++) ItemType->insertItem(id2info[i].trans_i18n);
        connect(ItemType,SIGNAL(activated(int)),this,SLOT(ItemTypeChanged(int)));
        connect(ItemParameter,SIGNAL(textChanged(const QString&)),this,SLOT(ItemParameterChanged(const QString&)));
        connect(ItemAttribute,SIGNAL(activated(int)),this,SLOT(ItemAttributeChanged(int)));
        connect(ItemContext,SIGNAL(activated(int)),this,SLOT(ItemContextChanged(int)));
  connect(ItemPopCount,SIGNAL(valueChanged(int)),this,SLOT(ItemPopCountChanged(int)));
    }
  else
    kdDebug(13010)<<"initItemOptions: Widget is 0"<<endl;
}

void HlEditDialog::loadFromDocument(HlData *hl)
{
  struct syntaxContextData *data;
  QListViewItem *last=0,*lastsub=0;

  HlManager::self()->syntax->setIdentifier(hl->identifier);
  data=HlManager::self()->syntax->getGroupInfo("highlighting","context");
  int i=0;
  if (data)
    {
      while (HlManager::self()->syntax->nextGroup(data)) //<context tags>
        {
        kdDebug(13010)<< "Adding context to list"<<endl;
          last= new QListViewItem(contextList,last,
                 HlManager::self()->syntax->groupData(data,QString("name")),
                 QString("%1").arg(i),
                 HlManager::self()->syntax->groupData(data,QString("attribute")),
                 HlManager::self()->syntax->groupData(data,QString("lineEndContext")));
          i++;
          lastsub=0;
          while (HlManager::self()->syntax->nextItem(data))
              {
                kdDebug(13010)<< "Adding item to list"<<endl;
                lastsub=addContextItem(last,lastsub,data);
              }


   }
       if (data) HlManager::self()->syntax->freeGroupInfo(data);
   }
  attrEd->load(HlManager::self()->syntax);
}

QListViewItem *HlEditDialog::addContextItem(QListViewItem *_parent,QListViewItem *prev,struct syntaxContextData *data)
  {

                kdDebug(13010)<<HlManager::self()->syntax->groupItemData(data,QString("name")) << endl;

                QString dataname=HlManager::self()->syntax->groupItemData(data,QString(""));
                QString attr=(HlManager::self()->syntax->groupItemData(data,QString("attribute")));
                QString context=(HlManager::self()->syntax->groupItemData(data,QString("context")));
    char chr;
                if (! HlManager::self()->syntax->groupItemData(data,QString("char")).isEmpty())
      chr= (HlManager::self()->syntax->groupItemData(data,QString("char")).latin1())[0];
    else
                  chr=0;
    QString stringdata=HlManager::self()->syntax->groupItemData(data,QString("String"));
                char chr1;
                if (! HlManager::self()->syntax->groupItemData(data,QString("char1")).isEmpty())
      chr1= (HlManager::self()->syntax->groupItemData(data,QString("char1")).latin1())[0];
    else
                  chr1=0;
    // not used at the mom, fix warning
    //bool insensitive=(HlManager::self()->syntax->groupItemData(data,QString("insensitive"))==QString("TRUE"));
                QString param("");
                if ((dataname=="keyword") /*|| (dataname=="dataType")*/) param=stringdata;
                  else if (dataname=="DetectChar") param=chr;
                    else if ((dataname=="Detect2Chars") || (dataname=="RangeDetect")) {param=QString("%1%2").arg(chr).arg(chr1);}
                      else if ((dataname=="StringDetect") || (dataname=="AnyChar") || (dataname=="RegExpr")) param=stringdata;
                        else                     kdDebug(13010)<<"***********************************"<<endl<<"Unknown entry for Context:"<<dataname<<endl;
                kdDebug(13010)<<dataname << endl;
                return new QListViewItem(_parent,prev,i18n(dataname.latin1())+" "+param,dataname,attr,context,param);
 }


void HlEditDialog::currentSelectionChanged ( QListViewItem *it)
  {
     kdDebug(13010)<<"Update data view"<<endl<<"Depth:"<<it->depth()<<endl;
     currentItem=it;
     if (it->depth()==0) showContext();
        else showItem();
  }


/****************************************************************************/
/*                              CONTEXTS                                    */
/****************************************************************************/


void HlEditDialog::showContext()
  {
    stack->raiseWidget(HlEContext);
    ContextDescr->setText(currentItem->text(0));
    ContextAttribute->setCurrentItem(currentItem->text(2).toInt());
    ContextLineEnd->clear();
    ContextLineEnd->insertItem("#pop");
    ContextLineEnd->insertItem("#stay");
    for (QListViewItem *it=contextList->firstChild();it;it=it->nextSibling())
        ContextLineEnd->insertItem(it->text(0));
    ContextLineEnd->setCurrentItem(currentItem->text(3).startsWith("#pop")?0:(currentItem->text(3).contains("#stay")?1:currentItem->text(3).toInt()+2));
    if (currentItem->text(3).startsWith("#pop"))
      {
    QString tmp=currentItem->text(3);
    int tmpPop;
    for (tmpPop=0;tmp.startsWith("#pop");tmpPop++,tmp.remove(0,4));
    ContextPopCount->setValue(tmpPop);
    ContextPopCount->show();
    //Do something
  } else ContextPopCount->hide();
  }

void HlEditDialog::contextDescrChanged(const QString& name)
  {
    if (currentItem)
      {
        currentItem->setText(0,name);
        ContextLineEnd->changeItem(name,currentItem->text(3).toInt()+2);
      }
  }

void HlEditDialog::contextAttributeChanged(int id)
{
  if (currentItem)
     {
     currentItem->setText(2,QString("%1").arg(id));
     }
}

void HlEditDialog::contextLineEndChanged(int id)
{
  kdDebug(13010)<<"contextLineEndChanged"<<endl;
  if (currentItem)
     {
     if (id==0)
     {
       currentItem->setText(3,"#pop"); // do something
     }
     else
       if (id==1) currentItem->setText(3,"#stay");
  else
         currentItem->setText(3,QString("%1").arg(id-2));
     }
}

void HlEditDialog::ContextPopCountChanged(int count)
{
  if (currentItem)
  {
    if (currentItem->text(3).startsWith("#pop"))
    {
      QString tmp="";
      for (int i=0;i<count;i++) tmp=tmp+"#pop";
      currentItem->setText(3,tmp);
    }
  }
}



void HlEditDialog::contextAddNew()
{
  QListViewItem *it=contextList->firstChild();
  for (;it->nextSibling()!=0;it=it->nextSibling());
  it=new QListViewItem(contextList,it,i18n("New Context"),QString("%1").arg(it->text(1).toInt()),"0","0");
  contextList->setSelected(it,true);
}

/****************************************************************************/
/*                              ITEMS                                       */
/****************************************************************************/

void HlEditDialog::showItem()
  {
    stack->raiseWidget(HlEItem);
    ItemContext->clear();
    ItemContext->insertItem("#pop");
    ItemContext->insertItem("#stay");
    for (QListViewItem *it=contextList->firstChild();it;it=it->nextSibling())
        ItemContext->insertItem(it->text(0));
    uint tmpCtx;
    ItemContext->setCurrentItem(tmpCtx=(currentItem->text(3).startsWith("#pop")?0:(currentItem->text(3).contains("#stay")?1:currentItem->text(3).toInt()+2)));
    kdDebug(13000)<<QString("showItem(): tmpCtx=%1").arg(tmpCtx)<<endl;
    if (tmpCtx==0)
    {
      kdDebug(13000)<<"Showing ItempPopCount"<<endl;
      ItemPopCount->show();
  QString tmp=currentItem->text(3);
  for (tmpCtx=0;tmp.startsWith("#pop");tmpCtx++,tmp.remove(0,4));
  ItemPopCount->setValue(tmpCtx);
    }
    else ItemPopCount->hide();
    ItemAttribute->setCurrentItem(currentItem->text(2).toInt());
    if (currentItem->text(1)==TAG_KEYWORD)
    {
  ItemParameter->ListParameter(currentItem->text(4));
  ItemParameter->show();
    }
    else
    {
      QMap<QString,int>::Iterator iter=tag2id.find(currentItem->text(1));
      if (iter==tag2id.end())
          kdDebug(13010)<<"Oops, unknown itemtype in showItem: "<<currentItem->text(1)<<endl;
      else
        {
          ItemType->setCurrentItem(*iter);
          if (id2info[*iter].length==0) ItemParameter->hide();
            else
             {
                 ItemParameter->TextParameter(id2info[*iter].length,currentItem->text(4));
                 ItemParameter->show();
             }
        }
     }

  }

void HlEditDialog::ItemTypeChanged(int id)
{
  if (currentItem)
     {
     currentItem->setText(1,id2tag[id]);
     ItemParameter->TextParameter(id2info[id].length,"");
     ItemParameterChanged(ItemParameter->text());
     }
}

void HlEditDialog::ItemParameterChanged(const QString& name)
{
  if (currentItem)
    {
      currentItem->setText(2,name);
      currentItem->setText(0,id2info[ItemType->currentItem()].trans_i18n+" "+currentItem->text(2));
    }
}

void HlEditDialog::ItemAttributeChanged(int attr)
{
   if (currentItem)
     {
       currentItem->setText(2,QString("%1").arg(attr));
     }
}

void HlEditDialog::ItemContextChanged(int cont)
{
   if (currentItem)
     {
       if (cont>1)
  {
    currentItem->setText(3,QString("%1").arg(cont-2));
    ItemPopCount->hide();
  }
       else
  {
    if (cont==0)
    {
      ItemPopCount->setValue(1);
      currentItem->setText(3,"#pop");
      ItemPopCount->show();
    }
    else
    {
      ItemPopCount->hide();
      currentItem->setText(3,"#push");
    }
  }
     }
}

void HlEditDialog::ItemPopCountChanged(int count)
{
  if (currentItem)
  {
    if (currentItem->text(3).startsWith("#pop"))
    {
      QString tmp="";
      for (int i=0;i<count;i++) tmp=tmp+"#pop";
      currentItem->setText(3,tmp);
    }
  }
}

void HlEditDialog::ItemAddNew()
{
  QListViewItem *it;
  if (currentItem)
    {
      if (currentItem->depth()==0) it=currentItem->firstChild();
        else
          it=currentItem;
      if (it) for (;it->nextSibling();it=it->nextSibling());
      (void) new QListViewItem(it ? it->parent() : currentItem,it,"StringDetect "+i18n("New Item"),"StringDetect",i18n("New Item"),0,it ? it->parent()->text(1) : currentItem->text(1));
    }
}
//END

//BEGIN StyleListView
/*********************************************************************/
/*                  StyleListView Implementation                     */
/*********************************************************************/
StyleListView::StyleListView( QWidget *parent, bool showUseDefaults, QColor textcol )
    : QListView( parent ),
      normalcol( textcol )
{
  addColumn( i18n("Context") );
  addColumn( SmallIconSet("text_bold"), QString::null/*i18n("Bold")*/ );
  addColumn( SmallIconSet("text_italic"), QString::null/*i18n("Italic")*/ );
  addColumn( SmallIconSet("text_under"), QString::null );
  addColumn( SmallIconSet("text_strike"), QString::null );
  addColumn( i18n("Normal") );
  addColumn( i18n("Selected") );
  addColumn( i18n("Background") );
  addColumn( i18n("Background Selected") );
  if ( showUseDefaults )
    addColumn( i18n("Use Default Style") );
  connect( this, SIGNAL(mouseButtonPressed(int, QListViewItem*, const QPoint&, int)),
           this, SLOT(slotMousePressed(int, QListViewItem*, const QPoint&, int)) );
  connect( this, SIGNAL(spacePressed(QListViewItem*)),
           this, SLOT(showPopupMenu(QListViewItem*)) );
  // grap the bg color, selected color and default font
  bgcol = *KateRendererConfig::global()->backgroundColor();
  selcol = *KateRendererConfig::global()->selectionColor();
  docfont = *KateRendererConfig::global()->font();

  viewport()->setPaletteBackgroundColor( bgcol );
}

void StyleListView::showPopupMenu( StyleListItem *i, const QPoint &globalPos, bool showtitle )
{
  KPopupMenu m( this );
  KateAttribute *is = i->style();
  int id;
  // the title is used, because the menu obscures the context name when
  // displayed on behalf of spacePressed().
  QPixmap cl(16,16);
  cl.fill( i->style()->textColor() );
  QPixmap scl(16,16);
  scl.fill( i->style()->selectedTextColor() );
  if ( showtitle )
    m.insertTitle( i->contextName(), StyleListItem::ContextName );
  id = m.insertItem( i18n("&Bold"), this, SLOT(mSlotPopupHandler(int)), 0, StyleListItem::Bold );
  m.setItemChecked( id, is->bold() );
  id = m.insertItem( i18n("&Italic"), this, SLOT(mSlotPopupHandler(int)), 0, StyleListItem::Italic );
  m.setItemChecked( id, is->italic() );
  m.insertItem( QIconSet(cl), i18n("Normal &Color..."), this, SLOT(mSlotPopupHandler(int)), 0, StyleListItem::Color );
  m.insertItem( QIconSet(scl), i18n("&Selected Color..."), this, SLOT(mSlotPopupHandler(int)), 0, StyleListItem::SelColor );
  if ( ! i->isDefault() ) {
    id = m.insertItem( i18n("Use &Default Style"), this, SLOT(mSlotPopupHandler(int)), 0, StyleListItem::UseDefStyle );
    m.setItemChecked( id, i->defStyle() );
  }
  m.exec( globalPos );
}

void StyleListView::showPopupMenu( QListViewItem *i )
{
  showPopupMenu( (StyleListItem*)i, viewport()->mapToGlobal(itemRect(i).topLeft()), true );
}

void StyleListView::mSlotPopupHandler( int z )
{
  ((StyleListItem*)currentItem())->changeProperty( (StyleListItem::Property)z );
}

// Because QListViewItem::activatePos() is going to become deprecated,
// and also because this attempt offers more control, I connect mousePressed to this.
void StyleListView::slotMousePressed(int btn, QListViewItem* i, const QPoint& pos, int c)
{
  if ( i ) {
    if ( btn == Qt::RightButton ) {
      showPopupMenu( (StyleListItem*)i, /*mapToGlobal(*/pos/*)*/ );
    }
    else if ( btn == Qt::LeftButton && c > 0 ) {
      // map pos to item/column and call StyleListItem::activate(col, pos)
      ((StyleListItem*)i)->activate( c, viewport()->mapFromGlobal( pos ) - QPoint( 0, itemRect(i).top() ) );
    }
  }
}

/* broken ?!
#include <kstdaccel.h>
#include <qcursor.h>
void StyleListView::keyPressEvent( QKeyEvent *e )
{
  if ( ! currentItem() ) return;
  if ( isVisible() && KStdAccel::isEqual( e, KStdAccel::key(KStdAccel::PopupMenuContext) ) ) {
    QPoint p = QCursor::pos();
    if( ! itemRect( currentItem() ).contains( mapFromGlobal(p)  ) )
      p = viewport()->mapToGlobal( itemRect(currentItem()).topLeft() );
    showPopupMenu( (StyleListItem*)currentItem(), p, true );
  }
  else
    QListView::keyPressEvent( e );
}
*/
//END

//BEGIN StyleListItem
/*********************************************************************/
/*                  StyleListItem Implementation                     */
/*********************************************************************/
static const int BoxSize = 16;
static const int ColorBtnWidth = 32;

StyleListItem::StyleListItem( QListView *parent, const QString & stylename,
                              KateAttribute *style, ItemData *data )
        : QListViewItem( parent, stylename ),
          /*styleName( stylename ),*/
          ds( style ),
          st( data )
{
  if ( st )
  {
    KateAttribute shit( *ds );
    is = new KateAttribute( shit += *dynamic_cast<KateAttribute*>(st) );
  }
  else
    is = ds;
}

void StyleListItem::updateStyle()
{
  //kdDebug()<<text(0)<<": set: "<<st->itemsSet()<<endl;
  /*
  save a atom if
      it is set and has changed and
      the setting is different from the default
  */
  if ( is->itemSet(KateAttribute::Weight) )
  {
    if ( is->weight() != st->weight() &&
        ( !ds->itemSet(KateAttribute::Weight) || is->weight() != ds->weight() ) )
      st->setWeight( is->weight() );
    else
      st->clearAttribute(KateAttribute::Weight);
  }
  if ( is->itemSet(KateAttribute::Italic) )
  {
    if ( is->italic() != st->italic() &&
        ( !ds->itemSet(KateAttribute::Italic) || is->italic() != ds->italic() ) )
      st->setItalic( is->italic() );
    else
      st->clearAttribute(KateAttribute::Italic);
  }
  if ( is->itemSet(KateAttribute::StrikeOut) )
  {
    if ( is->strikeOut() != st->strikeOut() &&
        ( !ds->itemSet(KateAttribute::StrikeOut) || is->strikeOut() != ds->strikeOut() ) )
      st->setStrikeOut( is->strikeOut() );
    else
      st->clearAttribute(KateAttribute::StrikeOut);
  }
  if ( is->itemSet(KateAttribute::Underline) )
  {
    if ( is->underline() != st->underline() &&
        ( !ds->itemSet(KateAttribute::Underline) || is->underline() != ds->underline() ) )
      st->setUnderline( is->underline() );
    else
      st->clearAttribute(KateAttribute::Underline);
  }
  if ( is->itemSet(KateAttribute::Outline) )
  {
    if ( is->outline() != st->outline() &&
        ( !ds->itemSet(KateAttribute::Outline) || is->outline() != ds->outline() ) )
      st->setOutline( is->outline() );
    else
      st->clearAttribute(KateAttribute::Outline);
  }

  if ( is->itemSet(KateAttribute::TextColor) )
  {
    if ( is->textColor() != st->textColor() &&
        is->textColor() != ds->textColor() )
      st->setTextColor( is->textColor() );
    else
      st->clearAttribute(KateAttribute::TextColor);
  }
  if ( is->itemSet(KateAttribute::SelectedTextColor) )
  {
    if ( is->selectedTextColor() != st->selectedTextColor() &&
        is->selectedTextColor() != ds->selectedTextColor() )
      st->setSelectedTextColor( is->selectedTextColor() );
    else
      st->clearAttribute(KateAttribute::SelectedTextColor);
  }
  if ( is->itemSet(KateAttribute::BGColor) )
  {
    if ( is->bgColor() != st->bgColor() &&
        ( !ds->itemSet(KateAttribute::BGColor) || is->bgColor() != ds->bgColor() ) )
      st->setBGColor( is->bgColor() );
    else
      st->clearAttribute(KateAttribute::BGColor);
  }
  if ( is->itemSet(KateAttribute::SelectedBGColor) )
  {
    if ( is->selectedBGColor() != st->selectedBGColor() &&
        ( ! ds->itemSet(KateAttribute::SelectedBGColor) || is->selectedBGColor() != ds->selectedBGColor() ) )
      st->setSelectedBGColor( is->selectedBGColor() );
    else
      st->clearAttribute(KateAttribute::SelectedBGColor);
  }
  //kdDebug()<<"after update: "<<st->itemsSet()<<endl;
  //kdDebug()<<"bold: "<<st->bold()<<" ("<<is->bold()<<")"<<endl;
}

/* only true for a hl mode item using it's default style */
bool StyleListItem::defStyle() { return st && st->isSomethingSet(); }

/* true for default styles */
bool StyleListItem::isDefault() { return st ? false : true; }

int StyleListItem::width( const QFontMetrics & /*fm*/, const QListView * lv, int col ) const
{
  int m = lv->itemMargin() * 2;
  switch ( col ) {
    case ContextName:
      // FIXME: width for name column should reflect bold/italic
      // (relevant for non-fixed fonts only - nessecary?)
      return QFontMetrics( ((StyleListView*)lv)->docfont).width( text(0) ) + m;
    case Bold:
    case Italic:
    case UseDefStyle:
      return BoxSize + m;
    case Color:
    case SelColor:
    case BgColor:
    case SelBgColor:
      return ColorBtnWidth +m;
    default:
      return 0;
  }
}

void StyleListItem::activate( int column, const QPoint &localPos )
{
  QListView *lv = listView();
  int x = 0;
  for( int c = 0; c < column-1; c++ )
    x += lv->columnWidth( c );
  int w;
  switch( column ) {
    case Bold:
    case Italic:
    case Underline:
    case Strikeout:
    case UseDefStyle:
      w = BoxSize;
      break;
    case Color:
    case SelColor:
    case BgColor:
    case SelBgColor:
      w = ColorBtnWidth;
      break;
    default:
      return;
  }
  if ( !QRect( x, 0, w, BoxSize ).contains( localPos ) )
  changeProperty( (Property)column );
}

void StyleListItem::changeProperty( Property p )
{
  bool ch( true );
  if ( p == Bold )
    is->setBold( ! is->bold() );
  else if ( p == Italic )
    is->setItalic( ! is->italic() );
  else if ( p == Underline )
    is->setUnderline( ! is->underline() );
  else if ( p == Strikeout )
    is->setStrikeOut( ! is->strikeOut() );
  else if ( p == UseDefStyle )
    toggleDefStyle();
  else
  {
    setColor( p );
    ch = false;
  }
  if ( ch )
    ((StyleListView*)listView())->emitChanged();
}

void StyleListItem::toggleDefStyle()
{
  if ( *is == *ds ) {
    KMessageBox::information( listView(),
         i18n("\"Use Default Style\" will be automatically unset when you change any style properties."),
         i18n("Kate Styles"),
         "Kate hl config use defaults" );
  }
  else {
    delete is;
    is = new KateAttribute( *ds );
    repaint();
  }
}

void StyleListItem::setColor( int column )
{
  QColor c;
  if ( column == Color) c = is->textColor();
  else if ( column == SelColor ) c = is->selectedTextColor();
  else if ( column == BgColor ) c = is->bgColor();
  else if ( column == SelBgColor ) c = is->selectedBGColor();

  if ( KColorDialog::getColor( c, listView() ) != QDialog::Accepted) return;

  if (st && st->isSomethingSet()) setCustStyle();

  if ( column == Color) is->setTextColor( c );
  else if ( column == SelColor ) is->setSelectedTextColor( c );
  else if ( column == BgColor ) is->setBGColor( c );
  else if ( column == SelBgColor ) is->setSelectedBGColor( c );

  repaint();
}

void StyleListItem::setCustStyle()
{
//   is = st;
//   *is += *ds;
//  st->defStyle = 0;
}

void StyleListItem::paintCell( QPainter *p, const QColorGroup& cg, int col, int width, int align )
{

  if ( !p )
    return;

  QListView *lv = listView();
  if ( !lv )
    return;
  Q_ASSERT( lv ); //###

  p->fillRect( 0, 0, width, height(), QBrush( ((StyleListView*)lv)->bgcol ) );
  int marg = lv->itemMargin();

  // use a provate color group and set the text/highlighted text colors
  QColorGroup mcg = cg;
  QColor c;

  switch ( col )
  {
    case ContextName:
    {
      mcg.setColor(QColorGroup::Text, is->textColor());
      mcg.setColor(QColorGroup::HighlightedText, is->selectedTextColor());
      QFont f ( ((StyleListView*)lv)->docfont );
      p->setFont( is->font(f) );
      // FIXME - repainting when text is cropped, and the column is enlarged is buggy.
      // Maybe I need painting the string myself :(
      QListViewItem::paintCell( p, mcg, col, width, align );
    }
    break;
    case Bold:
    case Italic:
    case Underline:
    case Strikeout:
    case UseDefStyle:
    {
      // Bold/Italic/use default checkboxes
      // code allmost identical to QCheckListItem
      // I use the text color of defaultStyles[0], normalcol in parent listview
      mcg.setColor( QColorGroup::Text, ((StyleListView*)lv)->normalcol );
      int x = 0;
      if ( align == AlignCenter ) {
        QFontMetrics fm( lv->font() );
        x = (width - BoxSize - fm.width(text(0)))/2;
      }
      int y = (height() - BoxSize) / 2;

      if ( isEnabled() )
        p->setPen( QPen( mcg.text(), 2 ) );
      else
        p->setPen( QPen( lv->palette().color( QPalette::Disabled, QColorGroup::Text ), 2 ) );

      if ( isSelected() && lv->header()->mapToSection( 0 ) != 0 )
      {
        p->fillRect( 0, 0, x + marg + BoxSize + 4, height(),
              mcg.brush( QColorGroup::Highlight ) );
        if ( isEnabled() )
          p->setPen( QPen( mcg.highlightedText(), 2 ) ); // FIXME! - use defaultstyles[0].selecol. luckily not used :)
      }
      p->drawRect( x+marg, y+2, BoxSize-4, BoxSize-4 );
      x++;
      y++;
      if ( (col == Bold && is->bold()) ||
          (col == Italic && is->italic()) ||
          (col == Underline && is->underline()) ||
          (col == Strikeout && is->strikeOut()) ||
          (col == UseDefStyle && *is == *ds ) )
      {
        QPointArray a( 7*2 );
        int i, xx, yy;
        xx = x+1+marg;
        yy = y+5;
        for ( i=0; i<3; i++ ) {
          a.setPoint( 2*i,   xx, yy );
          a.setPoint( 2*i+1, xx, yy+2 );
          xx++; yy++;
        }
        yy -= 2;
        for ( i=3; i<7; i++ ) {
          a.setPoint( 2*i,   xx, yy );
          a.setPoint( 2*i+1, xx, yy+2 );
          xx++; yy--;
        }
        p->drawLineSegments( a );
      }
    }
    break;
    case Color:
    case SelColor:
    case BgColor:
    case SelBgColor:
    {
      if ( col == Color) c = is->textColor();
      else if ( col == SelColor ) c = is->selectedTextColor();
      else if ( col == BgColor ) c = is->itemSet(KateAttribute::BGColor) ? is->bgColor() : ((StyleListView*)lv)->bgcol;
      else if ( col == SelBgColor ) c = is->itemSet(KateAttribute::SelectedBGColor) ? is->selectedBGColor(): ((StyleListView*)lv)->bgcol;
      // color "buttons"
      mcg.setColor( QColorGroup::Text, ((StyleListView*)lv)->normalcol );
      int x = 0;
      int y = (height() - BoxSize) / 2;
      if ( isEnabled() )
        p->setPen( QPen( mcg.text(), 2 ) );
      else
        p->setPen( QPen( lv->palette().color( QPalette::Disabled, QColorGroup::Text ), 2 ) );

      p->drawRect( x+marg, y+2, ColorBtnWidth-4, BoxSize-4 );
      p->fillRect( x+marg+1,y+3,ColorBtnWidth-7,BoxSize-7,QBrush( c ) );
    }
    //case default: // no warning...
  }
}
//END

//BEGIN KMimeTypeChooser
/*********************************************************************/
/*               KMimeTypeChooser Implementation                     */
/*********************************************************************/
KMimeTypeChooser::KMimeTypeChooser( QWidget *parent, const QString &text, const QStringList &selectedMimeTypes, bool editbutton, bool showcomment, bool showpatterns)
    : QVBox( parent )
{
  setSpacing( KDialogBase::spacingHint() );

/* HATE!!!! geometry management seems BADLY broken :(((((((((((
   Problem: if richtext is used (or Qt::WordBreak is on in the label),
   the list view is NOT resized when the parent box is.
   No richtext :(((( */
  if ( !text.isEmpty() ) {
    new QLabel( text, this );
  }

  lvMimeTypes = new QListView( this );
  lvMimeTypes->addColumn( i18n("Mime Type") );
  if ( showcomment )
    lvMimeTypes->addColumn( i18n("Comment") );
  if ( showpatterns )
    lvMimeTypes->addColumn( i18n("Patterns") );
  lvMimeTypes->setRootIsDecorated( true );

  //lvMimeTypes->clear(); WHY?!
  QMap<QString,QListViewItem*> groups;
  // thanks to kdebase/kcontrol/filetypes/filetypesview
  KMimeType::List mimetypes = KMimeType::allMimeTypes();
  QValueListIterator<KMimeType::Ptr> it(mimetypes.begin());

  QListViewItem *groupItem;
  bool agroupisopen = false;
  QListViewItem *idefault = 0; //open this, if all other fails
  for (; it != mimetypes.end(); ++it) {
    QString mimetype = (*it)->name();
    int index = mimetype.find("/");
    QString maj = mimetype.left(index);
    QString min = mimetype.right(mimetype.length() - (index+1));

    QMapIterator<QString,QListViewItem*> mit = groups.find( maj );
    if ( mit == groups.end() ) {
        groupItem = new QListViewItem( lvMimeTypes, maj );
        groups.insert( maj, groupItem );
        if (maj == "text")
          idefault = groupItem;
    }
    else
        groupItem = mit.data();

    QCheckListItem *item = new QCheckListItem( groupItem, min, QCheckListItem::CheckBox );
    item->setPixmap( 0, SmallIcon( (*it)->icon(QString::null,false) ) );
    int cl = 1;
    if ( showcomment ) {
      item->setText( cl, (*it)->comment(QString::null, false) );
      cl++;
    }
    if ( showpatterns )
      item->setText( cl, (*it)->patterns().join("; ") );
    if ( selectedMimeTypes.contains(mimetype) ) {
      item->setOn( true );
      groupItem->setOpen( true );
      agroupisopen = true;
      lvMimeTypes->ensureItemVisible( item );// actually, i should do this for the first item only.
    }
  }

  if (! agroupisopen)
    idefault->setOpen( true );

  if (editbutton) {
    QHBox *btns = new QHBox( this );
    ((QBoxLayout*)btns->layout())->addStretch(1); // hmm.
    btnEditMimeType = new QPushButton( i18n("&Edit..."), btns );
    connect( btnEditMimeType, SIGNAL(clicked()), this, SLOT(editMimeType()) );
    btnEditMimeType->setEnabled( false );
    connect( lvMimeTypes, SIGNAL( doubleClicked ( QListViewItem * )), this, SLOT( editMimeType()));
    connect( lvMimeTypes, SIGNAL(currentChanged(QListViewItem*)), this, SLOT(slotCurrentChanged(QListViewItem*)) );
    QWhatsThis::add( btnEditMimeType, i18n("Click this button to display the familiar KDE File Type Editor.<p><strong>Warning:</strong> if you change the file extensions, you need to restart this dialog, as it will not be aware that they have changed.") );
  }
}

void KMimeTypeChooser::editMimeType()
{
  if ( !(lvMimeTypes->currentItem() && (lvMimeTypes->currentItem())->parent()) ) return;
  QString mt = (lvMimeTypes->currentItem()->parent())->text( 0 ) + "/" + (lvMimeTypes->currentItem())->text( 0 );
  // thanks to libkonq/konq_operations.cc
  QString keditfiletype = QString::fromLatin1("keditfiletype");
  KRun::runCommand( keditfiletype + " " + KProcess::quote(mt),
                    keditfiletype, keditfiletype /*unused*/);
}

void KMimeTypeChooser::slotCurrentChanged(QListViewItem* i)
{
  btnEditMimeType->setEnabled( i->parent() );
}

QStringList KMimeTypeChooser::selectedMimeTypesStringList()
{
  QStringList l;
  QListViewItemIterator it( lvMimeTypes );
  for (; it.current(); ++it) {
    if ( it.current()->parent() && ((QCheckListItem*)it.current())->isOn() )
      l << it.current()->parent()->text(0) + "/" + it.current()->text(0); // FIXME: uncecked, should be Ok unless someone changes mimetypes during this!
  }
  return l;
}

QStringList KMimeTypeChooser::patterns()
{
  QStringList l;
  KMimeType::Ptr p;
  QString defMT = KMimeType::defaultMimeType();
  QListViewItemIterator it( lvMimeTypes );
  for (; it.current(); ++it) {
    if ( it.current()->parent() && ((QCheckListItem*)it.current())->isOn() ) {
      p = KMimeType::mimeType( it.current()->parent()->text(0) + "/" + it.current()->text(0) );
      if ( p->name() != defMT )
        l += p->patterns();
    }
  }
  return l;
}
//END

//BEGIN KMimeTypeChooserDlg
/*********************************************************************/
/*               KMimeTypeChooserDlg Implementation                  */
/*********************************************************************/
KMimeTypeChooserDlg::KMimeTypeChooserDlg(QWidget *parent,
                         const QString &caption, const QString& text,
                         const QStringList &selectedMimeTypes,
                         bool editbutton, bool showcomment, bool showpatterns )
    : KDialogBase(parent, 0, true, caption, Cancel|Ok, Ok)
{
  KConfig *config = kapp->config();

  chooser = new KMimeTypeChooser( this, text, selectedMimeTypes, editbutton, showcomment, showpatterns);
  setMainWidget(chooser);

  config->setGroup("KMimeTypeChooserDlg");
  resize( config->readSizeEntry("size", new QSize(400,300)) );
}

KMimeTypeChooserDlg::~KMimeTypeChooserDlg()
{
  KConfig *config = kapp->config();
  config->setGroup("KMimeTypeChooserDlg");
  config->writeEntry("size", size());
}

QStringList KMimeTypeChooserDlg::mimeTypes()
{
  return chooser->selectedMimeTypesStringList();
}

QStringList KMimeTypeChooserDlg::patterns()
{
  return chooser->patterns();
}

SpellConfigPage::SpellConfigPage( QWidget* parent )
  : Kate::ConfigPage( parent)
{
  QVBoxLayout* l = new QVBoxLayout( this );
  cPage = new KSpellConfig( this, 0L, 0L, false );
  l->addWidget( cPage );
  connect( cPage, SIGNAL( configChanged() ), this, SLOT( slotChanged() ) );
}

void SpellConfigPage::apply ()
{
  // kspell
  cPage->writeGlobalSettings ();
}
//END
// kate: space-indent on; indent-width 2; replace-tabs on;
