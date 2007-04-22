/* This file is part of the KDE libraries
   Copyright (C) 2003, 2004 Anders Lund <anders@alweb.dk>
   Copyright (C) 2003 Hamish Rodda <rodda@kde.org>
   Copyright (C) 2001,2002 Joseph Wenninger <jowenn@kde.org>
   Copyright (C) 2001 Christoph Cullmann <cullmann@kde.org>
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
   the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA 02110-1301, USA.
*/

//BEGIN INCLUDES
#include "katehighlight.h"
#include "katehighlight.moc"

#include "katetextline.h"
#include "katedocument.h"
#include "katesyntaxdocument.h"
#include "katerenderer.h"
#include "kateglobal.h"
#include "kateschema.h"
#include "kateconfig.h"
#include "kateextendedattribute.h"

#include <kconfig.h>
#include <kglobal.h>
#include <kcomponentdata.h>
#include <kmimetype.h>
#include <klocale.h>
#include <kmenu.h>
#include <kglobalsettings.h>
#include <kdebug.h>
#include <kstandarddirs.h>
#include <kmessagebox.h>
#include <kapplication.h>

#include <QSet>
#include <QAction>
#include <QtCore/QStringList>
#include <QtCore/QTextIStream>
//END

//BEGIN defines
// same as in kmimemagic, no need to feed more data
#define KATE_HL_HOWMANY 1024

// min. x seconds between two dynamic contexts reset
static const int KATE_DYNAMIC_CONTEXTS_RESET_DELAY = 30 * 1000;

// x is a QString. if x is "true" or "1" this expression returns "true"
#define IS_TRUE(x) x.toLower() == QLatin1String("true") || x.toInt() == 1
//END defines

//BEGIN  Prviate HL classes

class KateHlItem
{
  public:
    KateHlItem(int attribute, KateHlContextModification context,signed char regionId, signed char regionId2);
    virtual ~KateHlItem();

  public:
    // caller must keep in mind: LEN > 0 is a must !!!!!!!!!!!!!!!!!!!!!1
    // Now, the function returns the offset detected, or 0 if no match is found.
    // bool linestart isn't needed, this is equivalent to offset == 0.
    virtual int checkHgl(const QString& text, int offset, int len) = 0;

    virtual bool lineContinue(){return false;}

    virtual QStringList *capturedTexts() {return 0;}
    virtual KateHlItem *clone(const QStringList *) {return this;}

    static void dynamicSubstitute(QString& str, const QStringList *args);

    QVector<KateHlItem*> subItems;
    int attr;
    KateHlContextModification ctx;
    signed char region;
    signed char region2;

    bool lookAhead;

    bool dynamic;
    bool dynamicChild;
    bool firstNonSpace;
    bool onlyConsume;
    int column;

    // start enable flags, nicer than the virtual methodes
    // saves function calls
    bool alwaysStartEnable;
    bool customStartEnable;
};

class KateHlContext
{
  public:
    KateHlContext(const QString &_hlId, int attribute, KateHlContextModification _lineEndContext,KateHlContextModification _lineBeginContext,
                  bool _fallthrough, KateHlContextModification _fallthroughContext, bool _dynamic,bool _noIndentationBasedFolding);
    virtual ~KateHlContext();
    KateHlContext *clone(const QStringList *args);

    QVector<KateHlItem*> items;
    QString hlId; ///< A unique highlight identifier. Used to look up correct properties.
    int attr;
    KateHlContextModification lineEndContext;
    KateHlContextModification lineBeginContext;
    /** @internal anders: possible escape if no rules matches.
       false unless 'fallthrough="1|true"' (insensitive)
       if true, go to ftcxt w/o eating of string.
       ftctx is "fallthroughContext" in xml files, valid values are int or #pop[..]
       see in KateHighlighting::doHighlight */
    bool fallthrough;
    KateHlContextModification ftctx; // where to go after no rules matched

    bool dynamic;
    bool dynamicChild;
    bool noIndentationBasedFolding;
};

class KateHlIncludeRule
{
  public:
    KateHlIncludeRule(int ctx_=0, uint pos_=0, const QString &incCtxN_="", bool incAttrib=false)
      : ctx(ctx_)
      , pos( pos_)
      , incCtxN( incCtxN_ )
      , includeAttrib( incAttrib )
    {
      incCtx=-1;
    }
    //KateHlIncludeRule(int ctx_, uint  pos_, bool incAttrib) {ctx=ctx_;pos=pos_;incCtx=-1;incCtxN="";includeAttrib=incAttrib}

  public:
    int ctx;
    uint pos;
    KateHlContextModification incCtx;
    QString incCtxN;
    bool includeAttrib;
};

class KateHlCharDetect : public KateHlItem
{
  public:
    KateHlCharDetect(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2, QChar);

    virtual int checkHgl(const QString& text, int offset, int len);
    virtual KateHlItem *clone(const QStringList *args);

  private:
    QChar sChar;
};

class KateHl2CharDetect : public KateHlItem
{
  public:
    KateHl2CharDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2,  QChar ch1, QChar ch2);
    KateHl2CharDetect(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2,  const QChar *ch);

    virtual int checkHgl(const QString& text, int offset, int len);
    virtual KateHlItem *clone(const QStringList *args);

  private:
    QChar sChar1;
    QChar sChar2;
};

class KateHlStringDetect : public KateHlItem
{
  public:
    KateHlStringDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, const QString &, bool inSensitive=false);

    virtual int checkHgl(const QString& text, int offset, int len);
    virtual KateHlItem *clone(const QStringList *args);

  private:
    const QString str;
    const int strLen;
    const bool _inSensitive;
};

class KateHlRangeDetect : public KateHlItem
{
  public:
    KateHlRangeDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, QChar ch1, QChar ch2);

    virtual int checkHgl(const QString& text, int offset, int len);

  private:
    QChar sChar1;
    QChar sChar2;
};

class KateHlKeyword : public KateHlItem
{
  public:
    KateHlKeyword(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2, bool insensitive, const QString& delims);
    virtual ~KateHlKeyword ();

    void addList(const QStringList &);
    virtual int checkHgl(const QString& text, int offset, int len);

  private:
    QVector< QSet<QString>* > dict;
    bool _insensitive;
    const QString& deliminators;
    int minLen;
    int maxLen;
};

class KateHlInt : public KateHlItem
{
  public:
    KateHlInt(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlFloat : public KateHlItem
{
  public:
    KateHlFloat(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);
    virtual ~KateHlFloat () {}

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlCFloat : public KateHlFloat
{
  public:
    KateHlCFloat(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
    int checkIntHgl(const QString& text, int offset, int len);
};

class KateHlCOct : public KateHlItem
{
  public:
    KateHlCOct(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlCHex : public KateHlItem
{
  public:
    KateHlCHex(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlLineContinue : public KateHlItem
{
  public:
    KateHlLineContinue(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual bool endEnable(QChar c) {return c == '\0';}
    virtual int checkHgl(const QString& text, int offset, int len);
    virtual bool lineContinue(){return true;}
};

class KateHlCStringChar : public KateHlItem
{
  public:
    KateHlCStringChar(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlCChar : public KateHlItem
{
  public:
    KateHlCChar(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2);

    virtual int checkHgl(const QString& text, int offset, int len);
};

class KateHlAnyChar : public KateHlItem
{
  public:
    KateHlAnyChar(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, const QString& charList);

    virtual int checkHgl(const QString& text, int offset, int len);

  private:
    const QString _charList;
};

class KateHlRegExpr : public KateHlItem
{
  public:
    KateHlRegExpr(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2 ,const QString &expr, bool insensitive, bool minimal);
    ~KateHlRegExpr() { delete Expr; }

    virtual int checkHgl(const QString& text, int offset, int len);
    virtual QStringList *capturedTexts();
    virtual KateHlItem *clone(const QStringList *args);

  private:
    QRegExp *Expr;
    bool handlesLinestart;
    QString _regexp;
    bool _insensitive;
    bool _minimal;
};

class KateHlDetectSpaces : public KateHlItem
{
  public:
    KateHlDetectSpaces (int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
      : KateHlItem(attribute,context,regionId,regionId2) {}

    virtual int checkHgl(const QString& text, int offset, int len)
    {
      int len2 = offset + len;
      while ((offset < len2) && text[offset].isSpace()) offset++;
      return offset;
    }
};

class KateHlDetectIdentifier : public KateHlItem
{
  public:
    KateHlDetectIdentifier (int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
      : KateHlItem(attribute,context,regionId,regionId2) { alwaysStartEnable = false; }

    virtual int checkHgl(const QString& text, int offset, int len)
    {
      // first char should be a letter or underscore
      if ( text[offset].isLetter() || text[offset] == QChar ('_') )
      {
        // memorize length
        int len2 = offset+len;

        // one char seen
        offset++;

        // now loop for all other thingies
        while (
               (offset < len2)
               && (text[offset].isLetterOrNumber() || (text[offset] == QChar ('_')))
              )
          offset++;

        return offset;
      }

      return 0;
    }
};

//END

//BEGIN STATICS
static const QString stdDeliminator = QString (" \t.():!+,-<=>%&*/;?[]^{|}~\\");
//END

//BEGIN KateHlItem
KateHlItem::KateHlItem(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
  : attr(attribute),
    ctx(context),
    region(regionId),
    region2(regionId2),
    lookAhead(false),
    dynamic(false),
    dynamicChild(false),
    firstNonSpace(false),
    onlyConsume(false),
    column (-1),
    alwaysStartEnable (true),
    customStartEnable (false)
{
}

KateHlItem::~KateHlItem()
{
  //kDebug(13010)<<"In hlItem::~KateHlItem()"<<endl;
  for (int i=0; i < subItems.size(); i++)
    delete subItems[i];
}

void KateHlItem::dynamicSubstitute(QString &str, const QStringList *args)
{
  for (int i = 0; i < str.length() - 1; ++i)
  {
    if (str[i] == '%')
    {
      char c = str[i + 1].toLatin1();
      if (c == '%')
        str.replace(i, 1, "");
      else if (c >= '0' && c <= '9')
      {
        if ((int)(c - '0') < args->size())
        {
          str.replace(i, 2, (*args)[c - '0']);
          i += ((*args)[c - '0']).length() - 1;
        }
        else
        {
          str.replace(i, 2, "");
          --i;
        }
      }
    }
  }
}
//END

//BEGIN KateHlCharDetect
KateHlCharDetect::KateHlCharDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, QChar c)
  : KateHlItem(attribute,context,regionId,regionId2)
  , sChar(c)
{
}

int KateHlCharDetect::checkHgl(const QString& text, int offset, int /*len*/)
{
  if (text[offset] == sChar)
    return offset + 1;

  return 0;
}

KateHlItem *KateHlCharDetect::clone(const QStringList *args)
{
  char c = sChar.toLatin1();

  if (c < '0' || c > '9' || (c - '0') >= args->size())
    return this;

  KateHlCharDetect *ret = new KateHlCharDetect(attr, ctx, region, region2, (*args)[c - '0'][0]);
  ret->dynamicChild = true;
  return ret;
}
//END

//BEGIN KateHl2CharDetect
KateHl2CharDetect::KateHl2CharDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, QChar ch1, QChar ch2)
  : KateHlItem(attribute,context,regionId,regionId2)
  , sChar1 (ch1)
  , sChar2 (ch2)
{
}

int KateHl2CharDetect::checkHgl(const QString& text, int offset, int len)
{
  if ((len >= 2) && text[offset++] == sChar1 && text[offset++] == sChar2)
    return offset;

  return 0;
}

KateHlItem *KateHl2CharDetect::clone(const QStringList *args)
{
  char c1 = sChar1.toLatin1();
  char c2 = sChar2.toLatin1();

  if (c1 < '0' || c1 > '9' || (c1 - '0') >= args->size())
    return this;

  if (c2 < '0' || c2 > '9' || (c2 - '0') >= args->size())
    return this;

  KateHl2CharDetect *ret = new KateHl2CharDetect(attr, ctx, region, region2, (*args)[c1 - '0'][0], (*args)[c2 - '0'][0]);
  ret->dynamicChild = true;
  return ret;
}
//END

//BEGIN KateHlStringDetect
KateHlStringDetect::KateHlStringDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2,const QString &s, bool inSensitive)
  : KateHlItem(attribute, context,regionId,regionId2)
  , str(inSensitive ? s.toUpper() : s)
  , strLen (str.length())
  , _inSensitive(inSensitive)
{
}

int KateHlStringDetect::checkHgl(const QString& text, int offset, int len)
{
  if (len < strLen)
    return 0;

  if (_inSensitive)
  {
    for (int i=0; i < strLen; i++)
      if (text[offset++].toUpper() != str[i])
        return 0;

    return offset;
  }
  else
  {
    for (int i=0; i < strLen; i++)
      if (text[offset++] != str[i])
        return 0;

    return offset;
  }

  return 0;
}

KateHlItem *KateHlStringDetect::clone(const QStringList *args)
{
  QString newstr = str;

  dynamicSubstitute(newstr, args);

  if (newstr == str)
    return this;

  KateHlStringDetect *ret = new KateHlStringDetect(attr, ctx, region, region2, newstr, _inSensitive);
  ret->dynamicChild = true;
  return ret;
}
//END

//BEGIN KateHlRangeDetect
KateHlRangeDetect::KateHlRangeDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, QChar ch1, QChar ch2)
  : KateHlItem(attribute,context,regionId,regionId2)
  , sChar1 (ch1)
  , sChar2 (ch2)
{
}

int KateHlRangeDetect::checkHgl(const QString& text, int offset, int len)
{
  if (text[offset] == sChar1)
  {
    do
    {
      offset++;
      len--;
      if (len < 1) return 0;
    }
    while (text[offset] != sChar2);

    return offset + 1;
  }
  return 0;
}
//END

//BEGIN KateHlKeyword
KateHlKeyword::KateHlKeyword (int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, bool insensitive, const QString& delims)
  : KateHlItem(attribute,context,regionId,regionId2)
  , _insensitive(insensitive)
  , deliminators(delims)
  , minLen (0xFFFFFF)
  , maxLen (0)
{
  alwaysStartEnable = false;
  customStartEnable = true;
}

KateHlKeyword::~KateHlKeyword ()
{
  for (int i=0; i < dict.size(); ++i)
    delete dict[i];
}

void KateHlKeyword::addList(const QStringList& list)
{
  for(int i=0; i < list.count(); ++i)
  {
    int len = list[i].length();

    if (minLen > len)
      minLen = len;

    if (maxLen < len)
      maxLen = len;

    if (len >= dict.size())
    {
      uint oldSize = dict.size();
      dict.resize (len+1);

      for (int m=oldSize; m < dict.size(); ++m)
        dict[m] = 0;
    }

    if (!dict[len])
      dict[len] = new QSet<QString> ();

    if (!_insensitive)
      dict[len]->insert(list[i]);
    else
      dict[len]->insert(list[i].toLower());
  }
}

int KateHlKeyword::checkHgl(const QString& text, int offset, int len)
{
  int offset2 = offset;
  int wordLen = 0;

  while ((len > wordLen) && !deliminators.contains(text[offset2]))
  {
    offset2++;
    wordLen++;

    if (wordLen > maxLen) return 0;
  }

  if (wordLen < minLen || !dict[wordLen]) return 0;

  if (!_insensitive)
  {
    if (dict[wordLen]->contains(QString::fromRawData(text.unicode() + offset, wordLen)) )
      return offset2;
  }
  else
  {
    if (dict[wordLen]->contains(QString::fromRawData(text.unicode() + offset, wordLen).toLower()) )
      return offset2;
  }

  return 0;
}
//END

//BEGIN KateHlInt
KateHlInt::KateHlInt(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2)
{
  alwaysStartEnable = false;
}

int KateHlInt::checkHgl(const QString& text, int offset, int len)
{
  int offset2 = offset;

  while ((len > 0) && text[offset2].isDigit())
  {
    offset2++;
    len--;
  }

  if (offset2 > offset)
  {
    if (len > 0)
    {
      for (int i=0; i < subItems.size(); i++)
      {
        if ( (offset = subItems[i]->checkHgl(text, offset2, len)) )
          return offset;
      }
    }

    return offset2;
  }

  return 0;
}
//END

//BEGIN KateHlFloat
KateHlFloat::KateHlFloat(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context, regionId,regionId2)
{
  alwaysStartEnable = false;
}

int KateHlFloat::checkHgl(const QString& text, int offset, int len)
{
  bool b = false;
  bool p = false;

  while ((len > 0) && text[offset].isDigit())
  {
    offset++;
    len--;
    b = true;
  }

  if ((len > 0) && (p = (text[offset] == '.')))
  {
    offset++;
    len--;

    while ((len > 0) && text[offset].isDigit())
    {
      offset++;
      len--;
      b = true;
    }
  }

  if (!b)
    return 0;

  if ((len > 0) && (text[offset].toAscii() == 'E'))
  {
    offset++;
    len--;
  }
  else
  {
    if (!p)
      return 0;
    else
    {
      if (len > 0)
      {
        for (int i=0; i < subItems.size(); ++i)
        {
          int offset2 = subItems[i]->checkHgl(text, offset, len);

          if (offset2)
            return offset2;
        }
      }

      return offset;
    }
  }

  if ((len > 0) && (text[offset] == '-' || text[offset] =='+'))
  {
    offset++;
    len--;
  }

  b = false;

  while ((len > 0) && text[offset].isDigit())
  {
    offset++;
    len--;
    b = true;
  }

  if (b)
  {
    if (len > 0)
    {
      for (int i=0; i < subItems.size(); ++i)
      {
        int offset2 = subItems[i]->checkHgl(text, offset, len);

        if (offset2)
          return offset2;
      }
    }

    return offset;
  }

  return 0;
}
//END

//BEGIN KateHlCOct
KateHlCOct::KateHlCOct(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2)
{
  alwaysStartEnable = false;
}

int KateHlCOct::checkHgl(const QString& text, int offset, int len)
{
  if (text[offset].toAscii() == '0')
  {
    offset++;
    len--;

    int offset2 = offset;

    while ((len > 0) && (text[offset2].toAscii() >= '0' && text[offset2].toAscii() <= '7'))
    {
      offset2++;
      len--;
    }

    if (offset2 > offset)
    {
      if ((len > 0) && (text[offset2].toAscii() == 'L' || text[offset].toAscii() == 'U' ))
        offset2++;

      return offset2;
    }
  }

  return 0;
}
//END

//BEGIN KateHlCHex
KateHlCHex::KateHlCHex(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2)
{
  alwaysStartEnable = false;
}

int KateHlCHex::checkHgl(const QString& text, int offset, int len)
{
  if ((len > 1) && (text[offset++].toAscii() == '0') && (text[offset++].toAscii() == 'X' ))
  {
    len -= 2;

    int offset2 = offset;

    while ((len > 0) && (text[offset2].isDigit() || (text[offset2].toAscii() >= 'A' && text[offset2].toAscii() <= 'F')))
    {
      offset2++;
      len--;
    }

    if (offset2 > offset)
    {
      if ((len > 0) && (text[offset2].toAscii() == 'L' || text[offset2].toAscii() == 'U' ))
        offset2++;

      return offset2;
    }
  }

  return 0;
}
//END

//BEGIN KateHlCFloat
KateHlCFloat::KateHlCFloat(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2)
  : KateHlFloat(attribute,context,regionId,regionId2)
{
  alwaysStartEnable = false;
}

int KateHlCFloat::checkIntHgl(const QString& text, int offset, int len)
{
  int offset2 = offset;

  while ((len > 0) && text[offset].isDigit()) {
    offset2++;
    len--;
  }

  if (offset2 > offset)
     return offset2;

  return 0;
}

int KateHlCFloat::checkHgl(const QString& text, int offset, int len)
{
  int offset2 = KateHlFloat::checkHgl(text, offset, len);

  if (offset2)
  {
    if (text[offset2].toAscii() == 'F' )
      offset2++;

    return offset2;
  }
  else
  {
    offset2 = checkIntHgl(text, offset, len);

    if (offset2 && (text[offset2].toAscii() == 'F' ))
      return ++offset2;
    else
      return 0;
  }
}
//END

//BEGIN KateHlAnyChar
KateHlAnyChar::KateHlAnyChar(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, const QString& charList)
  : KateHlItem(attribute, context,regionId,regionId2)
  , _charList(charList)
{
}

int KateHlAnyChar::checkHgl(const QString& text, int offset, int)
{
  if (_charList.contains(text[offset]))
    return ++offset;

  return 0;
}
//END

//BEGIN KateHlRegExpr
KateHlRegExpr::KateHlRegExpr( int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, const QString &regexp, bool insensitive, bool minimal)
  : KateHlItem(attribute, context, regionId,regionId2)
  , handlesLinestart (regexp.startsWith("^"))
  , _regexp(regexp)
  , _insensitive(insensitive)
  , _minimal(minimal)
{
  Expr = new QRegExp(handlesLinestart ? regexp : '^' + regexp, _insensitive ? Qt::CaseInsensitive : Qt::CaseSensitive );
  Expr->setMinimal(_minimal);
}

int KateHlRegExpr::checkHgl(const QString& text, int offset, int /*len*/)
{
  if (offset && handlesLinestart)
    return 0;

  int offset2 = Expr->indexIn( text, offset, QRegExp::CaretAtOffset );

  if (offset2 == -1) return 0;

  return (offset + Expr->matchedLength());
}

QStringList *KateHlRegExpr::capturedTexts()
{
  return new QStringList(Expr->capturedTexts());
}

KateHlItem *KateHlRegExpr::clone(const QStringList *args)
{
  QString regexp = _regexp;
  QStringList escArgs = *args;

  for (QStringList::Iterator it = escArgs.begin(); it != escArgs.end(); ++it)
  {
    (*it).replace(QRegExp("(\\W)"), "\\\\1");
  }

  dynamicSubstitute(regexp, &escArgs);

  if (regexp == _regexp)
    return this;

  // kDebug (13010) << "clone regexp: " << regexp << endl;

  KateHlRegExpr *ret = new KateHlRegExpr(attr, ctx, region, region2, regexp, _insensitive, _minimal);
  ret->dynamicChild = true;
  return ret;
}
//END

//BEGIN KateHlLineContinue
KateHlLineContinue::KateHlLineContinue(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2) {
}

int KateHlLineContinue::checkHgl(const QString& text, int offset, int len)
{
  if ((len == 1) && (text[offset] == '\\'))
    return ++offset;

  return 0;
}
//END

//BEGIN KateHlCStringChar
KateHlCStringChar::KateHlCStringChar(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2) {
}

// checks for C escaped chars \n and escaped hex/octal chars
static int checkEscapedChar(const QString& text, int offset, int& len)
{
  int i;
  if (text[offset] == '\\' && len > 1)
  {
    offset++;
    len--;

    switch(text[offset].toAscii())
    {
      case  'a': // checks for control chars
      case  'b': // we want to fall through
      case  'e':
      case  'f':

      case  'n':
      case  'r':
      case  't':
      case  'v':
      case '\'':
      case '\"':
      case '?' : // added ? ANSI C classifies this as an escaped char
      case '\\':
        offset++;
        len--;
        break;

      case 'x': // if it's like \xff
        offset++; // eat the x
        len--;
        // these for loops can probably be
        // replaced with something else but
        // for right now they work
        // check for hexdigits
        for (i = 0; (len > 0) && (i < 2) && (text[offset].toAscii() >= '0' && text[offset].toAscii() <= '9' || text[offset].toAscii() >= 'A' && text[offset].toAscii() <= 'F'); i++)
        {
          offset++;
          len--;
        }

        if (i == 0)
          return 0; // takes care of case '\x'

        break;

      case '0': case '1': case '2': case '3' :
      case '4': case '5': case '6': case '7' :
        for (i = 0; (len > 0) && (i < 3) && (text[offset] >='0'&& text[offset] <='7'); i++)
        {
          offset++;
          len--;
        }
        break;

      default:
        return 0;
    }

    return offset;
  }

  return 0;
}

int KateHlCStringChar::checkHgl(const QString& text, int offset, int len)
{
  return checkEscapedChar(text, offset, len);
}
//END

//BEGIN KateHlCChar
KateHlCChar::KateHlCChar(int attribute, KateHlContextModification context,signed char regionId,signed char regionId2)
  : KateHlItem(attribute,context,regionId,regionId2) {
}

int KateHlCChar::checkHgl(const QString& text, int offset, int len)
{
  if ((len > 1) && (text[offset] == '\'') && (text[offset+1] != '\''))
  {
    int oldl;
    oldl = len;

    len--;

    int offset2 = checkEscapedChar(text, offset + 1, len);

    if (!offset2)
    {
      if (oldl > 2)
      {
        offset2 = offset + 2;
        len = oldl - 2;
      }
      else
      {
        return 0;
      }
    }

    if ((len > 0) && (text[offset2] == '\''))
      return ++offset2;
  }

  return 0;
}
//END

//BEGIN KateHl2CharDetect
KateHl2CharDetect::KateHl2CharDetect(int attribute, KateHlContextModification context, signed char regionId,signed char regionId2, const QChar *s)
  : KateHlItem(attribute,context,regionId,regionId2) {
  sChar1 = s[0];
  sChar2 = s[1];
  }
//END KateHl2CharDetect

KateHlData::KateHlData(const QString &wildcards, const QString &mimetypes, const QString &identifier, int priority)
  : wildcards(wildcards), mimetypes(mimetypes), identifier(identifier), priority(priority)
{
}

KateHlData::KateHlData() {}

//BEGIN KateHlContext
KateHlContext::KateHlContext (const QString &_hlId, int attribute, KateHlContextModification _lineEndContext, KateHlContextModification _lineBeginContext, bool _fallthrough,
	KateHlContextModification _fallthroughContext, bool _dynamic, bool _noIndentationBasedFolding)
{
  hlId = _hlId;
  attr = attribute;
  lineEndContext = _lineEndContext;
  lineBeginContext = _lineBeginContext;
  fallthrough = _fallthrough;
  ftctx = _fallthroughContext;
  dynamic = _dynamic;
  dynamicChild = false;
  noIndentationBasedFolding=_noIndentationBasedFolding;
  if (_noIndentationBasedFolding) kDebug(13010)<<QString("**********************_noIndentationBasedFolding is TRUE*****************")<<endl;

}

KateHlContext *KateHlContext::clone(const QStringList *args)
{
  KateHlContext *ret = new KateHlContext(hlId, attr, lineEndContext, lineBeginContext, fallthrough, ftctx, false,noIndentationBasedFolding);

  for (int n=0; n < items.size(); ++n)
  {
    KateHlItem *item = items[n];
    KateHlItem *i = (item->dynamic ? item->clone(args) : item);
    ret->items.append(i);
  }

  ret->dynamicChild = true;

  return ret;
}

KateHlContext::~KateHlContext()
{
  if (dynamicChild)
  {
    for (int n=0; n < items.size(); ++n)
    {
      if (items[n]->dynamicChild)
        delete items[n];
    }
  }
}
//END

//BEGIN KateHighlighting
KateHighlighting::KateHighlighting(const KateSyntaxModeListItem *def) : refCount(0)
{
  errorsAndWarnings = "";
  building=false;
  noHl = false;
  m_foldingIndentationSensitive = false;
  folding=false;

  if (def == 0)
  {
    noHl = true;
    iName = "None"; // not translated internal name (for config and more)
    iNameTranslated = i18n("None"); // user visible name
    iSection = "";
    m_priority = 0;
    iHidden = false;
    m_additionalData.insert( "none", new HighlightPropertyBag );
    m_additionalData["none"]->deliminator = stdDeliminator;
    m_additionalData["none"]->wordWrapDeliminator = stdDeliminator;
    m_hlIndex[0] = "none";
  }
  else
  {
    iName = def->name;
    iNameTranslated = def->nameTranslated;
    iSection = def->section;
    iHidden = def->hidden;
    iWildcards = def->extension;
    iMimetypes = def->mimetype;
    identifier = def->identifier;
    iVersion=def->version;
    iAuthor=def->author;
    iLicense=def->license;
    m_priority=def->priority.toInt();
  }

   deliminator = stdDeliminator;
}

KateHighlighting::~KateHighlighting()
{
  // cleanup ;)
  cleanup ();

  qDeleteAll(m_additionalData);
}

void KateHighlighting::cleanup ()
{
  qDeleteAll (m_contexts);
  m_contexts.clear ();

  m_attributeArrays.clear ();

  internalIDList.clear();
}

KateHlContext *KateHighlighting::generateContextStack (QVector<short> &contextStack,
                                                       KateHlContextModification modification,
                                                       int &indexLastContextPreviousLine)
{
  while (true)
  {
    switch (modification.type)
    {
      /**
       * stay, do nothing, just return the last context
       * in the stack or 0
       */
      case KateHlContextModification::doNothing:
        return contextNum (contextStack.isEmpty() ? 0 : contextStack.last());

      /**
       * just add a new context to the stack
       * and return this one
       */
      case KateHlContextModification::doPush:
        contextStack.append (modification.newContext);
        return contextNum (modification.newContext);

      /**
       * pop some contexts + add a new one afterwards, immediate....
       */
      case KateHlContextModification::doPopsAndPush:
        // resize stack
        contextStack.resize ((modification.pops >= contextStack.size()) ? 0 : (contextStack.size() - modification.pops));

        // push imediate the new context....
        // don't handle the previous line stuff at all....
        // ### TODO ### think about this
        contextStack.append (modification.newContext);
        return contextNum (modification.newContext);

      /**
       * do only pops...
       */
      default:
      {
        // resize stack
        contextStack.resize ((modification.pops >= contextStack.size()) ? 0 : (contextStack.size() - modification.pops));

        // handling of context of previous line....
        if (indexLastContextPreviousLine >= (contextStack.size()-1))
        {
          // set new index, if stack is empty, this is -1, done for eternity...
          indexLastContextPreviousLine = contextStack.size() - 1;

          // stack already empty, nothing to do...
          if ( contextStack.isEmpty() )
            return contextNum (0);

          KateHlContext *c = contextNum(contextStack.last());

          // this must be a valid context, or our context stack is borked....
          Q_ASSERT (c);

          // handle line end context as new modificationContext
          modification = c->lineEndContext;
          continue;
        }

        return contextNum (contextStack.isEmpty() ? 0 : contextStack.last());
      }
    }
  }

  // should never be reached
  Q_ASSERT (false);

  return contextNum (0);
}

/**
 * Creates a new dynamic context or reuse an old one if it has already been created.
 */
int KateHighlighting::makeDynamicContext(KateHlContext *model, const QStringList *args)
{
  QPair<KateHlContext *, QString> key(model, args->front());
  short value;

  if (dynamicCtxs.contains(key))
    value = dynamicCtxs[key];
  else
  {
    kDebug(13010) << "new stuff: " << startctx << endl;

    KateHlContext *newctx = model->clone(args);

    m_contexts.push_back (newctx);

    value = startctx++;
    dynamicCtxs[key] = value;
    KateHlManager::self()->incDynamicCtxs();
  }

  // kDebug(13010) << "Dynamic context: using context #" << value << " (for model " << model << " with args " << *args << ")" << endl;

  return value;
}

/**
 * Drop all dynamic contexts. Shall be called with extreme care, and shall be immediately
 * followed by a full HL invalidation.
 */
void KateHighlighting::dropDynamicContexts()
{
  for (int i=base_startctx; i < m_contexts.size(); ++i)
    delete m_contexts[i];

  m_contexts.resize (base_startctx);

  dynamicCtxs.clear();
  startctx = base_startctx;
}

/**
 * Parse the text and fill in the context array and folding list array
 *
 * @param prevLine The previous line, the context array is picked up from that if present.
 * @param textLine The text line to parse
 * @param foldingList will be filled
 * @param ctxChanged will be set to reflect if the context changed
 */
void KateHighlighting::doHighlight ( KateTextLine *prevLine,
                                     KateTextLine *textLine,
                                     QVector<int> &foldingList,
                                     bool &ctxChanged )
{
  if (!textLine)
    return;

  // in all cases, remove old hl, or we will grow to infinite ;)
  textLine->clearAttributes ();

  // no hl set, nothing to do more than the above cleaning ;)
  if (noHl)
    return;

  // duplicate the ctx stack, only once !
  QVector<short> ctx (prevLine->ctxArray());

  int previousLine = -1;
  KateHlContext *context;

  if (ctx.isEmpty())
  {
    // If the stack is empty, we assume to be in Context 0 (Normal)
    context = contextNum(0);
  }
  else
  {
    //kDebug(13010) << "\t\tctxNum = " << ctxNum << " contextList[ctxNum] = " << contextList[ctxNum] << endl; // ellis

    //if (lineContinue)   kDebug(13010)<<QString("The old context should be %1").arg((int)ctxNum)<<endl;
    context = contextNum(ctx.last());

    //kDebug(13010)<<"test1-2-1-text2"<<endl;

    previousLine = ctx.size()-1; //position of the last context ID of th previous line within the stack

    // hl continue set or not ???
    if (prevLine->hlLineContinue())
    {
      prevLine--;
    }
    else
    {
      context = generateContextStack(ctx, context->lineEndContext, previousLine); //get stack ID to use
    }

    //kDebug(13010)<<"test1-2-1-text4"<<endl;

    //if (lineContinue)   kDebug(13010)<<QString("The new context is %1").arg((int)ctxNum)<<endl;
  }

  // text, for programming convenience :)
  QChar lastChar = ' ';
  const QString& text = textLine->string();
  const int len = textLine->length();

  // calc at which char the first char occurs, set it to length of line if never
  const int firstChar = textLine->firstChar();
  const int startNonSpace = (firstChar == -1) ? len : firstChar;

  // last found item
  KateHlItem *item = 0;

  // loop over the line, offset gives current offset
  int offset = 0;
  while (offset < len)
  {
    bool anItemMatched = false;
    bool standardStartEnableDetermined = false;
    bool customStartEnableDetermined = false;

    int index = 0;
    for (item = context->items.empty() ? 0 : context->items[0]; item; item = (++index < context->items.size()) ? context->items[index] : 0 )
    {
      // does we only match if we are firstNonSpace?
      if (item->firstNonSpace && (offset > startNonSpace))
        continue;

      // have we a column specified? if yes, only match at this column
      if ((item->column != -1) && (item->column != offset))
        continue;

      if (!item->alwaysStartEnable)
      {
        if (item->customStartEnable)
        {
            if (customStartEnableDetermined || m_additionalData[context->hlId]->deliminator.contains(lastChar))
            customStartEnableDetermined = true;
          else
            continue;
        }
        else
        {
          if (standardStartEnableDetermined || stdDeliminator.contains(lastChar))
            standardStartEnableDetermined = true;
          else
            continue;
        }
      }

      int offset2 = item->checkHgl(text, offset, len-offset);

      if (offset2 <= offset)
        continue;

      if (item->region2)
      {
        // kDebug(13010)<<QString("Region mark 2 detected: %1").arg(item->region2)<<endl;
        if ( !foldingList.isEmpty() && ((item->region2 < 0) && (int)foldingList[foldingList.size()-2] == -item->region2 ) )
        {
          foldingList.resize (foldingList.size()-2);
        }
        else
        {
          foldingList.resize (foldingList.size()+2);
          foldingList[foldingList.size()-2] = (uint)item->region2;
          if (item->region2<0) //check not really needed yet
            foldingList[foldingList.size()-1] = offset2;
          else
            foldingList[foldingList.size()-1] = offset;
        }

      }

      if (item->region)
      {
        // kDebug(13010)<<QString("Region mark detected: %1").arg(item->region)<<endl;

      /* if ( !foldingList->isEmpty() && ((item->region < 0) && (*foldingList)[foldingList->size()-1] == -item->region ) )
        {
          foldingList->resize (foldingList->size()-1, QGArray::SpeedOptim);
        }
        else*/
        {
          foldingList.resize (foldingList.size()+2);
          foldingList[foldingList.size()-2] = item->region;
          if (item->region<0) //check not really needed yet
            foldingList[foldingList.size()-1] = offset2;
          else
            foldingList[foldingList.size()-1] = offset;
        }

      }

      // regenerate context stack if needed
      context = generateContextStack (ctx, item->ctx, previousLine);

      // dynamic context: substitute the model with an 'instance'
      if (context->dynamic)
      {
        QStringList *lst = item->capturedTexts();
        if (lst != 0)
        {
          // Replace the top of the stack and the current context
          int newctx = makeDynamicContext(context, lst);
          if (ctx.size() > 0)
            ctx[ctx.size() - 1] = newctx;

          context = contextNum(newctx);
        }
        delete lst;
      }

      // dominik: look ahead w/o changing offset?
      if (!item->lookAhead)
      {
        if (offset2 > len)
          offset2 = len;

        // even set attributes ;)
        int attribute = item->onlyConsume ? context->attr : item->attr;
        if (attribute > 0)
          textLine->addAttribute (offset, offset2-offset, attribute);

        offset = offset2;
        lastChar = text[offset-1];
      }

      anItemMatched = true;
      break;
    }

    // something matched, continue loop
    if (anItemMatched)
      continue;

    // nothing found: set attribute of one char
    // anders: unless this context does not want that!
    if ( context->fallthrough )
    {
    // set context to context->ftctx.
      context=generateContextStack(ctx, context->ftctx, previousLine);  //regenerate context stack

    //kDebug(13010)<<"context num after fallthrough at col "<<z<<": "<<ctxNum<<endl;
    // the next is necessary, as otherwise keyword (or anything using the std delimitor check)
    // immediately after fallthrough fails. Is it bad?
    // jowenn, can you come up with a nicer way to do this?
    /*  if (offset)
        lastChar = text[offset - 1];
      else
        lastChar = '\\';*/
      continue;
    }
    else
    {
      // set attribute if any
      if (context->attr > 0)
        textLine->addAttribute (offset, 1, context->attr);

      lastChar = text[offset];
      offset++;
    }
  }

  // has the context stack changed ?
  if (ctx == textLine->ctxArray())
  {
    ctxChanged = false;
  }
  else
  {
    ctxChanged = true;

    // assign ctx stack !
    textLine->setContext(ctx);
  }

  // write hl continue flag
  textLine->setHlLineContinue (item && item->lineContinue());

  if (m_foldingIndentationSensitive) {
    bool noindent=false;
    for(int i=ctx.size()-1; i>=0; --i) {
      if (contextNum(ctx[i])->noIndentationBasedFolding) {
        noindent=true;
        break;
      }
    }
    textLine->setNoIndentBasedFolding(noindent);
  }
}

void KateHighlighting::loadWildcards()
{
  KConfigGroup config(KateHlManager::self()->getKConfig(), "Highlighting " + iName);

  QString extensionString = config.readEntry("Wildcards", iWildcards);

  if (extensionSource != extensionString) {
    regexpExtensions.clear();
    plainExtensions.clear();

    extensionSource = extensionString;

    static QRegExp sep("\\s*;\\s*");

    QStringList l = extensionSource.split( sep, QString::SkipEmptyParts );

    static QRegExp boringExpression("\\*\\.[\\d\\w]+");

    for( QStringList::Iterator it = l.begin(); it != l.end(); ++it )
      if (boringExpression.exactMatch(*it))
        plainExtensions.append((*it).mid(1));
      else
        regexpExtensions.append(QRegExp((*it), Qt::CaseSensitive, QRegExp::Wildcard));
  }
}

QList<QRegExp>& KateHighlighting::getRegexpExtensions()
{
  return regexpExtensions;
}

QStringList& KateHighlighting::getPlainExtensions()
{
  return plainExtensions;
}

QString KateHighlighting::getMimetypes()
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName);

  return config.readEntry("Mimetypes", iMimetypes);
}

int KateHighlighting::priority()
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName);

  return config.readEntry("Priority", m_priority);
}

KateHlData KateHighlighting::getData()
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName);

  return  KateHlData(
  config.readEntry("Wildcards", iWildcards),
  config.readEntry("Mimetypes", iMimetypes),
  config.readEntry("Identifier", identifier),
  config.readEntry("Priority", m_priority));
}

void KateHighlighting::setData(const KateHlData &hlData)
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName);

  config.writeEntry("Wildcards",hlData.wildcards);
  config.writeEntry("Mimetypes",hlData.mimetypes);
  config.writeEntry("Priority",hlData.priority);
}

void KateHighlighting::getKateExtendedAttributeList (uint schema, QList<KateExtendedAttribute::Ptr> &list)
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName + " - Schema " + KateGlobal::self()->schemaManager()->name(schema));

  list.clear();
  createKateExtendedAttribute(list);

  foreach (KateExtendedAttribute::Ptr p, list)
  {
    Q_ASSERT(p);

    QStringList s = config.readEntry(p->name(), QStringList());

//    kDebug(13010)<<p->name<<s.count()<<endl;
    if (s.count()>0)
    {

      while(s.count()<9) s<<"";
      p->clear();

      QString tmp=s[0]; if (!tmp.isEmpty()) p->setDefaultStyleIndex(tmp.toInt());

      QRgb col;

      tmp=s[1]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); p->setForeground(QColor(col)); }

      tmp=s[2]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); p->setSelectedForeground(QColor(col)); }

      tmp=s[3]; if (!tmp.isEmpty()) p->setFontBold(tmp!="0");

      tmp=s[4]; if (!tmp.isEmpty()) p->setFontItalic(tmp!="0");

      tmp=s[5]; if (!tmp.isEmpty()) p->setFontStrikeOut(tmp!="0");

      tmp=s[6]; if (!tmp.isEmpty()) p->setFontUnderline(tmp!="0");

      tmp=s[7]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); p->setBackground(QColor(col)); }

      tmp=s[8]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); p->setSelectedBackground(QColor(col)); }

    }
  }
}

void KateHighlighting::getKateExtendedAttributeListCopy( uint schema, QList< KateExtendedAttribute::Ptr >& list )
{
  QList<KateExtendedAttribute::Ptr> attributes;
  getKateExtendedAttributeList(schema, attributes);

  list.clear();

  foreach (KateExtendedAttribute::Ptr attribute, attributes)
    list.append(KateExtendedAttribute::Ptr(new KateExtendedAttribute(*attribute.data())));
}


/**
 * Saves the KateHlData attribute definitions to the config file.
 *
 * @param schema The id of the schema group to save
 * @param list QList<KateExtendedAttribute::Ptr> containing the data to be used
 */
void KateHighlighting::setKateExtendedAttributeList(uint schema, QList<KateExtendedAttribute::Ptr> &list)
{
  KConfigGroup config(KateHlManager::self()->getKConfig(),
                      "Highlighting " + iName + " - Schema "
                      + KateGlobal::self()->schemaManager()->name(schema));

  QStringList settings;

  foreach (const KateExtendedAttribute::Ptr& p, list)
  {
    Q_ASSERT(p);

    settings.clear();
    settings<<QString::number(p->defaultStyleIndex(),10);
    settings<<(p->hasProperty(QTextFormat::ForegroundBrush)?QString::number(p->foreground().color().rgb(),16):"");
    settings<<(p->hasProperty(KTextEditor::Attribute::SelectedForeground)?QString::number(p->selectedForeground().color().rgb(),16):"");
    settings<<(p->hasProperty(QTextFormat::FontWeight)?(p->fontBold()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontItalic)?(p->fontItalic()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontStrikeOut)?(p->fontStrikeOut()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontUnderline)?(p->fontUnderline()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::BackgroundBrush)?QString::number(p->background().color().rgb(),16):"");
    settings<<(p->hasProperty(KTextEditor::Attribute::SelectedBackground)?QString::number(p->selectedBackground().color().rgb(),16):"");
    settings<<"---";
    config.writeEntry(p->name(),settings);
  }
}

/**
 * Increase the usage count, and trigger initialization if needed.
 */
void KateHighlighting::use()
{
  if (refCount == 0)
    init();

  refCount++;
}

/**
 * Decrease the usage count, and trigger cleanup if needed.
 */
void KateHighlighting::release()
{
  refCount--;

  if (refCount == 0)
    done();
}

/**
 * Initialize a context for the first time.
 */

void KateHighlighting::init()
{
  if (noHl)
    return;

  // cu contexts
  for (int i=0; i < m_contexts.size(); ++i)
    delete m_contexts[i];
  m_contexts.clear ();

  makeContextList();
}


/**
 * If the there is no document using the highlighting style free the complete
 * context structure.
 */
void KateHighlighting::done()
{
  if (noHl)
    return;

  cleanup ();
}

/**
 * KateHighlighting - createKateExtendedAttribute
 * This function reads the itemData entries from the config file, which specifies the
 * default attribute styles for matched items/contexts.
 *
 * @param list A reference to the internal list containing the parsed default config
 */
void KateHighlighting::createKateExtendedAttribute(QList<KateExtendedAttribute::Ptr> &list)
{
  // If no highlighting is selected we need only one default.
  if (noHl)
  {
    list.append(KateExtendedAttribute::Ptr(new KateExtendedAttribute(i18n("Normal Text"), KateExtendedAttribute::dsNormal)));
    return;
  }

  // If the internal list isn't already available read the config file
  if (internalIDList.isEmpty())
    makeContextList();

  list=internalIDList;
}

/**
 * Adds the styles of the currently parsed highlight to the itemdata list
 */
void KateHighlighting::addToKateExtendedAttributeList()
{
  //Tell the syntax document class which file we want to parse and which data group
  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data = KateHlManager::self()->syntax->getGroupInfo("highlighting","itemData");

  //begin with the real parsing
  while (KateHlManager::self()->syntax->nextGroup(data))
  {
    // read all attributes
    QString color = KateHlManager::self()->syntax->groupData(data,QString("color"));
    QString selColor = KateHlManager::self()->syntax->groupData(data,QString("selColor"));
    QString bold = KateHlManager::self()->syntax->groupData(data,QString("bold"));
    QString italic = KateHlManager::self()->syntax->groupData(data,QString("italic"));
    QString underline = KateHlManager::self()->syntax->groupData(data,QString("underline"));
    QString strikeOut = KateHlManager::self()->syntax->groupData(data,QString("strikeOut"));
    QString bgColor = KateHlManager::self()->syntax->groupData(data,QString("backgroundColor"));
    QString selBgColor = KateHlManager::self()->syntax->groupData(data,QString("selBackgroundColor"));

    KateExtendedAttribute::Ptr newData(new KateExtendedAttribute(
            buildPrefix+KateHlManager::self()->syntax->groupData(data,QString("name")).simplified(),
            KateExtendedAttribute::indexForStyleName(KateHlManager::self()->syntax->groupData(data,QString("defStyleNum")))));

    /* here the custom style overrides are specified, if needed */
    if (!color.isEmpty()) newData->setForeground(QColor(color));
    if (!selColor.isEmpty()) newData->setSelectedForeground(QColor(selColor));
    if (!bold.isEmpty()) newData->setFontBold( IS_TRUE(bold) );
    if (!italic.isEmpty()) newData->setFontItalic( IS_TRUE(italic) );
    // new attributes for the new rendering view
    if (!underline.isEmpty()) newData->setFontUnderline( IS_TRUE(underline) );
    if (!strikeOut.isEmpty()) newData->setFontStrikeOut( IS_TRUE(strikeOut) );
    if (!bgColor.isEmpty()) newData->setBackground(QColor(bgColor));
    if (!selBgColor.isEmpty()) newData->setSelectedBackground(QColor(selBgColor));

    internalIDList.append(newData);
  }

  //clean up
  if (data)
    KateHlManager::self()->syntax->freeGroupInfo(data);
}

/**
 * KateHighlighting - lookupAttrName
 * This function is  a helper for makeContextList and createKateHlItem. It looks the given
 * attribute name in the itemData list up and returns it's index
 *
 * @param name the attribute name to lookup
 * @param iDl the list containing all available attributes
 *
 * @return The index of the attribute, or 0 if the attribute isn't found
 */
int  KateHighlighting::lookupAttrName(const QString& name, QList<KateExtendedAttribute::Ptr> &iDl)
{
  for (int i = 0; i < iDl.count(); i++)
    if (iDl.at(i)->name() == buildPrefix+name)
      return i;

  kDebug(13010)<<"Couldn't resolve itemDataName:"<<name<<endl;
  return 0;
}

/**
 * KateHighlighting - createKateHlItem
 * This function is  a helper for makeContextList. It parses the xml file for
 * information.
 *
 * @param data Data about the item read from the xml file
 * @param iDl List of all available itemData entries.
 *            Needed for attribute name->index translation
 * @param RegionList list of code folding region names
 * @param ContextNameList list of context names
 *
 * @return A pointer to the newly created item object
 */
KateHlItem *KateHighlighting::createKateHlItem(KateSyntaxContextData *data,
                                               QList<KateExtendedAttribute::Ptr> &iDl,
                                               QStringList *RegionList,
                                               QStringList *ContextNameList)
{
  // No highlighting -> exit
  if (noHl)
    return 0;

  // get the (tagname) itemd type
  QString dataname=KateHlManager::self()->syntax->groupItemData(data,QString(""));

  // code folding region handling:
  QString beginRegionStr=KateHlManager::self()->syntax->groupItemData(data,QString("beginRegion"));
  QString endRegionStr=KateHlManager::self()->syntax->groupItemData(data,QString("endRegion"));

  signed char regionId=0;
  signed char regionId2=0;

  if (!beginRegionStr.isEmpty())
  {
    regionId = RegionList->indexOf(beginRegionStr);

    if (regionId==-1) // if the region name doesn't already exist, add it to the list
    {
      (*RegionList)<<beginRegionStr;
      regionId = RegionList->indexOf(beginRegionStr);
    }

    regionId++;

    kDebug(13010) << "########### BEG REG: "  << beginRegionStr << " NUM: " << regionId << endl;
  }

  if (!endRegionStr.isEmpty())
  {
    regionId2 = RegionList->indexOf(endRegionStr);

    if (regionId2==-1) // if the region name doesn't already exist, add it to the list
    {
      (*RegionList)<<endRegionStr;
      regionId2 = RegionList->indexOf(endRegionStr);
    }

    regionId2 = -regionId2 - 1;

    kDebug(13010) << "########### END REG: "  << endRegionStr << " NUM: " << regionId2 << endl;
  }

  int attr = 0;
  QString tmpAttr=KateHlManager::self()->syntax->groupItemData(data,QString("attribute")).simplified();
  bool onlyConsume = tmpAttr.isEmpty();

  // only relevant for non consumer
  if (!onlyConsume)
  {
    if (QString("%1").arg(tmpAttr.toInt())==tmpAttr)
    {
      errorsAndWarnings+=i18n(
          "<B>%1</B>: Deprecated syntax. Attribute (%2) not addressed by symbolic name<BR>",
      buildIdentifier, tmpAttr);
      attr=tmpAttr.toInt();
    }
    else
      attr=lookupAttrName(tmpAttr,iDl);
  }

  // Info about context switch
  KateHlContextModification context = -1;
  QString unresolvedContext;
  QString tmpcontext=KateHlManager::self()->syntax->groupItemData(data,QString("context"));
  if (!tmpcontext.isEmpty())
    context=getContextModificationFromString(ContextNameList, tmpcontext,unresolvedContext);

  // Get the char parameter (eg DetectChar)
  char chr;
  if (! KateHlManager::self()->syntax->groupItemData(data,QString("char")).isEmpty())
    chr= qPrintable((KateHlManager::self()->syntax->groupItemData(data,QString("char"))))[0];
  else
    chr=0;

  // Get the String parameter (eg. StringDetect)
  QString stringdata=KateHlManager::self()->syntax->groupItemData(data,QString("String"));

  // Get a second char parameter (char1) (eg Detect2Chars)
  char chr1;
  if (! KateHlManager::self()->syntax->groupItemData(data,QString("char1")).isEmpty())
    chr1= (KateHlManager::self()->syntax->groupItemData(data,QString("char1")).toLatin1())[0];
  else
    chr1=0;

  // Will be removed eventually. Atm used for StringDetect, keyword and RegExp
  const QString & insensitive_str = KateHlManager::self()->syntax->groupItemData(data,QString("insensitive"));
  bool insensitive = IS_TRUE( insensitive_str );

  // for regexp only
  bool minimal = IS_TRUE( KateHlManager::self()->syntax->groupItemData(data,QString("minimal")) );

  // dominik: look ahead and do not change offset. so we can change contexts w/o changing offset1.
  bool lookAhead = IS_TRUE( KateHlManager::self()->syntax->groupItemData(data,QString("lookAhead")) );

  bool dynamic= IS_TRUE(KateHlManager::self()->syntax->groupItemData(data,QString("dynamic")) );

  bool firstNonSpace = IS_TRUE(KateHlManager::self()->syntax->groupItemData(data,QString("firstNonSpace")) );

  int column = -1;
  QString colStr = KateHlManager::self()->syntax->groupItemData(data,QString("column"));
  if (!colStr.isEmpty())
    column = colStr.toInt();

  //Create the item corresponding to it's type and set it's parameters
  KateHlItem *tmpItem;

  if (dataname=="keyword")
  {
    bool keywordInsensitive = insensitive_str.isEmpty() ? !casesensitive : insensitive;
    KateHlKeyword *keyword=new KateHlKeyword(attr,context,regionId,regionId2,keywordInsensitive,
                                             m_additionalData[ buildIdentifier ]->deliminator);

    //Get the entries for the keyword lookup list
    keyword->addList(KateHlManager::self()->syntax->finddata("highlighting",stringdata));
    tmpItem=keyword;
  }
  else if (dataname=="Float") tmpItem= (new KateHlFloat(attr,context,regionId,regionId2));
  else if (dataname=="Int") tmpItem=(new KateHlInt(attr,context,regionId,regionId2));
  else if (dataname=="DetectChar") tmpItem=(new KateHlCharDetect(attr,context,regionId,regionId2,chr));
  else if (dataname=="Detect2Chars") tmpItem=(new KateHl2CharDetect(attr,context,regionId,regionId2,chr,chr1));
  else if (dataname=="RangeDetect") tmpItem=(new KateHlRangeDetect(attr,context,regionId,regionId2, chr, chr1));
  else if (dataname=="LineContinue") tmpItem=(new KateHlLineContinue(attr,context,regionId,regionId2));
  else if (dataname=="StringDetect") tmpItem=(new KateHlStringDetect(attr,context,regionId,regionId2,stringdata,insensitive));
  else if (dataname=="AnyChar") tmpItem=(new KateHlAnyChar(attr,context,regionId,regionId2,stringdata));
  else if (dataname=="RegExpr") tmpItem=(new KateHlRegExpr(attr,context,regionId,regionId2,stringdata, insensitive, minimal));
  else if (dataname=="HlCChar") tmpItem= ( new KateHlCChar(attr,context,regionId,regionId2));
  else if (dataname=="HlCHex") tmpItem= (new KateHlCHex(attr,context,regionId,regionId2));
  else if (dataname=="HlCOct") tmpItem= (new KateHlCOct(attr,context,regionId,regionId2));
  else if (dataname=="HlCFloat") tmpItem= (new KateHlCFloat(attr,context,regionId,regionId2));
  else if (dataname=="HlCStringChar") tmpItem= (new KateHlCStringChar(attr,context,regionId,regionId2));
  else if (dataname=="DetectSpaces") tmpItem= (new KateHlDetectSpaces(attr,context,regionId,regionId2));
  else if (dataname=="DetectIdentifier") tmpItem= (new KateHlDetectIdentifier(attr,context,regionId,regionId2));
  else
  {
    // oops, unknown type. Perhaps a spelling error in the xml file
    return 0;
  }

  // set lookAhead & dynamic properties
  tmpItem->lookAhead = lookAhead;
  tmpItem->dynamic = dynamic;
  tmpItem->firstNonSpace = firstNonSpace;
  tmpItem->column = column;
  tmpItem->onlyConsume = onlyConsume;

  if (!unresolvedContext.isEmpty())
  {
    unresolvedContextReferences.insert(&(tmpItem->ctx),unresolvedContext);
  }

  return tmpItem;
}

QString KateHighlighting::hlKeyForAttrib( int i ) const
{
  // find entry. This is faster than QMap::find. m_hlIndex always has an entry
  // for key '0' (it is "none"), so the result is always valid.
  int k = 0;
  QMap<int,QString>::const_iterator it = m_hlIndex.constEnd();
  while ( it != m_hlIndex.constBegin() )
  {
    --it;
    k = it.key();
    if ( i >= k )
      break;
  }
  return it.value();
}

bool KateHighlighting::isInWord( QChar c, int attrib ) const
{
  return m_additionalData[ hlKeyForAttrib( attrib ) ]->deliminator.indexOf(c) < 0
      && !c.isSpace()
      && c != QChar::fromLatin1('"') && c != QChar::fromLatin1('\'');
}

bool KateHighlighting::canBreakAt( QChar c, int attrib ) const
{
  static const QString& sq = KGlobal::staticQString("\"'");
  return (m_additionalData[ hlKeyForAttrib( attrib ) ]->wordWrapDeliminator.indexOf(c) != -1) && (sq.indexOf(c) == -1);
}

QLinkedList<QRegExp> KateHighlighting::emptyLines(int attrib) const
{
  kDebug(13010)<<"hlKeyForAttrib: "<<hlKeyForAttrib(attrib)<<endl;
  return m_additionalData[hlKeyForAttrib(attrib)]->emptyLines;
}

signed char KateHighlighting::commentRegion(int attr) const {
  QString commentRegion=m_additionalData[ hlKeyForAttrib( attr ) ]->multiLineRegion;
  return (commentRegion.isEmpty()?0:(commentRegion.toShort()));
}

bool KateHighlighting::canComment( int startAttrib, int endAttrib ) const
{
  QString k = hlKeyForAttrib( startAttrib );
  return ( k == hlKeyForAttrib( endAttrib ) &&
      ( ( !m_additionalData[k]->multiLineCommentStart.isEmpty() && !m_additionalData[k]->multiLineCommentEnd.isEmpty() ) ||
       ! m_additionalData[k]->singleLineCommentMarker.isEmpty() ) );
}

QString KateHighlighting::getCommentStart( int attrib ) const
{
  return m_additionalData[ hlKeyForAttrib( attrib) ]->multiLineCommentStart;
}

QString KateHighlighting::getCommentEnd( int attrib ) const
{
  return m_additionalData[ hlKeyForAttrib( attrib ) ]->multiLineCommentEnd;
}

QString KateHighlighting::getCommentSingleLineStart( int attrib ) const
{
  return m_additionalData[ hlKeyForAttrib( attrib) ]->singleLineCommentMarker;
}

KateHighlighting::CSLPos KateHighlighting::getCommentSingleLinePosition( int attrib ) const
{
  return m_additionalData[ hlKeyForAttrib( attrib) ]->singleLineCommentPosition;
}


/**
 * Helper for makeContextList. It parses the xml file for
 * information, how single or multi line comments are marked
 */
void KateHighlighting::readCommentConfig()
{
  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data=KateHlManager::self()->syntax->getGroupInfo("general","comment");

  QString cmlStart="", cmlEnd="", cmlRegion="", cslStart="";
  CSLPos cslPosition=CSLPosColumn0;

  if (data)
  {
    while  (KateHlManager::self()->syntax->nextGroup(data))
    {
      if (KateHlManager::self()->syntax->groupData(data,"name")=="singleLine")
      {
        cslStart=KateHlManager::self()->syntax->groupData(data,"start");
        QString cslpos=KateHlManager::self()->syntax->groupData(data,"position");
        if (cslpos=="afterwhitespace")
          cslPosition=CSLPosAfterWhitespace;
        else
          cslPosition=CSLPosColumn0;
      }
      else if (KateHlManager::self()->syntax->groupData(data,"name")=="multiLine")
      {
        cmlStart=KateHlManager::self()->syntax->groupData(data,"start");
        cmlEnd=KateHlManager::self()->syntax->groupData(data,"end");
        cmlRegion=KateHlManager::self()->syntax->groupData(data,"region");
      }
    }

    KateHlManager::self()->syntax->freeGroupInfo(data);
  }

  m_additionalData[buildIdentifier]->singleLineCommentMarker = cslStart;
  m_additionalData[buildIdentifier]->singleLineCommentPosition = cslPosition;
  m_additionalData[buildIdentifier]->multiLineCommentStart = cmlStart;
  m_additionalData[buildIdentifier]->multiLineCommentEnd = cmlEnd;
  m_additionalData[buildIdentifier]->multiLineRegion = cmlRegion;
}




void KateHighlighting::readEmptyLineConfig()
{
  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data=KateHlManager::self()->syntax->getGroupInfo("general","emptyLine");

  QLinkedList<QRegExp> exprList;

  if (data)
  {
    while  (KateHlManager::self()->syntax->nextGroup(data))
    {
      kDebug(13010)<<"creating an empty line regular expression"<<endl;
      QString regexprline=KateHlManager::self()->syntax->groupData(data,"regexpr");
      bool regexprcase=(KateHlManager::self()->syntax->groupData(data,"casesensitive").toUpper().compare("TRUE")==0);
      exprList.append(QRegExp(regexprline,regexprcase?Qt::CaseSensitive:Qt::CaseInsensitive));
    }
      KateHlManager::self()->syntax->freeGroupInfo(data);
  }

  m_additionalData[buildIdentifier]->emptyLines = exprList;
}






/**
 * Helper for makeContextList. It parses the xml file for information,
 * if keywords should be treated case(in)sensitive and creates the keyword
 * delimiter list. Which is the default list, without any given weak deliminiators
 */
void KateHighlighting::readGlobalKeywordConfig()
{
  deliminator = stdDeliminator;
  // Tell the syntax document class which file we want to parse
  kDebug(13010)<<"readGlobalKeywordConfig:BEGIN"<<endl;

  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data = KateHlManager::self()->syntax->getConfig("general","keywords");

  if (data)
  {
    kDebug(13010)<<"Found global keyword config"<<endl;

    if ( IS_TRUE( KateHlManager::self()->syntax->groupItemData(data,QString("casesensitive")) ) )
      casesensitive=true;
    else
      casesensitive=false;

    //get the weak deliminators
    weakDeliminator=(KateHlManager::self()->syntax->groupItemData(data,QString("weakDeliminator")));

    kDebug(13010)<<"weak delimiters are: "<<weakDeliminator<<endl;

    // remove any weakDelimitars (if any) from the default list and store this list.
    for (int s=0; s < weakDeliminator.length(); s++)
    {
      int f = deliminator.indexOf (weakDeliminator[s]);

      if (f > -1)
        deliminator.remove (f, 1);
    }

    QString addDelim = (KateHlManager::self()->syntax->groupItemData(data,QString("additionalDeliminator")));

    if (!addDelim.isEmpty())
      deliminator=deliminator+addDelim;

    KateHlManager::self()->syntax->freeGroupInfo(data);
  }
  else
  {
    //Default values
    casesensitive=true;
    weakDeliminator=QString("");
  }

  kDebug(13010)<<"readGlobalKeywordConfig:END"<<endl;

  kDebug(13010)<<"delimiterCharacters are: "<<deliminator<<endl;

  m_additionalData[buildIdentifier]->deliminator = deliminator;
}

/**
 * Helper for makeContextList. It parses the xml file for any wordwrap
 * deliminators, characters * at which line can be broken. In case no keyword
 * tag is found in the xml file, the wordwrap deliminators list defaults to the
 * standard denominators. In case a keyword tag is defined, but no
 * wordWrapDeliminator attribute is specified, the deliminator list as computed
 * in readGlobalKeywordConfig is used.
 *
 * @return the computed delimiter string.
 */
void KateHighlighting::readWordWrapConfig()
{
  // Tell the syntax document class which file we want to parse
  kDebug(13010)<<"readWordWrapConfig:BEGIN"<<endl;

  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data = KateHlManager::self()->syntax->getConfig("general","keywords");

  QString wordWrapDeliminator = stdDeliminator;
  if (data)
  {
    kDebug(13010)<<"Found global keyword config"<<endl;

    wordWrapDeliminator = (KateHlManager::self()->syntax->groupItemData(data,QString("wordWrapDeliminator")));
    //when no wordWrapDeliminator is defined use the deliminator list
    if ( wordWrapDeliminator.length() == 0 ) wordWrapDeliminator = deliminator;

    kDebug(13010) << "word wrap deliminators are " << wordWrapDeliminator << endl;

    KateHlManager::self()->syntax->freeGroupInfo(data);
  }

  kDebug(13010)<<"readWordWrapConfig:END"<<endl;

  m_additionalData[buildIdentifier]->wordWrapDeliminator = wordWrapDeliminator;
}

void KateHighlighting::readIndentationConfig()
{
  m_indentation = "";

  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data = KateHlManager::self()->syntax->getConfig("general","indentation");

  if (data)
  {
    m_indentation = (KateHlManager::self()->syntax->groupItemData(data,QString("mode")));

    KateHlManager::self()->syntax->freeGroupInfo(data);
  }
}

void KateHighlighting::readFoldingConfig()
{
  // Tell the syntax document class which file we want to parse
  kDebug(13010)<<"readfoldignConfig:BEGIN"<<endl;

  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);
  KateSyntaxContextData *data = KateHlManager::self()->syntax->getConfig("general","folding");

  if (data)
  {
    kDebug(13010)<<"Found global keyword config"<<endl;

    if ( IS_TRUE( KateHlManager::self()->syntax->groupItemData(data,QString("indentationsensitive")) ) )
      m_foldingIndentationSensitive=true;
    else
      m_foldingIndentationSensitive=false;

    KateHlManager::self()->syntax->freeGroupInfo(data);
  }
  else
  {
    //Default values
    m_foldingIndentationSensitive = false;
  }

  kDebug(13010)<<"readfoldingConfig:END"<<endl;

  kDebug(13010)<<"############################ use indent for fold are: "<<m_foldingIndentationSensitive<<endl;
}

void  KateHighlighting::createContextNameList(QStringList *ContextNameList,int ctx0)
{
  kDebug(13010)<<"creatingContextNameList:BEGIN"<<endl;

  if (ctx0 == 0)
      ContextNameList->clear();

  KateHlManager::self()->syntax->setIdentifier(buildIdentifier);

  KateSyntaxContextData *data=KateHlManager::self()->syntax->getGroupInfo("highlighting","context");

  int id=ctx0;

  if (data)
  {
     while (KateHlManager::self()->syntax->nextGroup(data))
     {
          QString tmpAttr=KateHlManager::self()->syntax->groupData(data,QString("name")).simplified();
    if (tmpAttr.isEmpty())
    {
     tmpAttr=QString("!KATE_INTERNAL_DUMMY! %1").arg(id);
     errorsAndWarnings +=i18n("<B>%1</B>: Deprecated syntax. Context %2 has no symbolic name<BR>", buildIdentifier, id-ctx0);
    }
          else tmpAttr=buildPrefix+tmpAttr;
    (*ContextNameList)<<tmpAttr;
          id++;
     }
     KateHlManager::self()->syntax->freeGroupInfo(data);
  }
  kDebug(13010)<<"creatingContextNameList:END"<<endl;

}

KateHlContextModification KateHighlighting::getContextModificationFromString(QStringList *ContextNameList, QString tmpLineEndContext, /*NO CONST*/ QString &unres)
{
  // nothing unresolved
  unres = "";

  // context to push on stack
  int context = -1;

  // number of contexts to pop
  int pops = 0;

  // we allow arbitrary #stay and #pop at the start
  bool anyFound = false;
  while (tmpLineEndContext.startsWith("#stay") || tmpLineEndContext.startsWith("#pop"))
  {
    // ignore stay
    if (tmpLineEndContext.startsWith("#stay"))
    {
      tmpLineEndContext.remove (0, 5);
    }
    else // count the pops
    {
      ++pops;
      tmpLineEndContext.remove (0, 4);
    }

    anyFound = true;
  }

  /**
   * we want a ! if we have found any pop or push and still have stuff in the string...
   */
  if (anyFound && !tmpLineEndContext.isEmpty())
  {
    if (tmpLineEndContext.startsWith("!"))
      tmpLineEndContext.remove (0, 1);
  }

  /**
   * empty string, done
   */
  if (tmpLineEndContext.isEmpty())
    return KateHlContextModification (context, pops);

  /**
   * handle the remaining string, this might be a ##contextname
   * or a normal contextname....
   */
  if ( tmpLineEndContext.startsWith("##"))
  {
    QString tmp=tmpLineEndContext.right(tmpLineEndContext.length()-2);
    if (!embeddedHls.contains(tmp))  embeddedHls.insert(tmp,KateEmbeddedHlInfo());
    unres=tmp;
    kDebug(13010) << "unres = " << unres << endl;
    context=0;
  }

  else
  {
    context=ContextNameList->indexOf(buildPrefix+tmpLineEndContext);
    if (context==-1)
    {
      context=tmpLineEndContext.toInt();
      errorsAndWarnings+=i18n(
        "<B>%1</B>:Deprecated syntax. Context %2 not addressed by a symbolic name"
        , buildIdentifier, tmpLineEndContext);
    }
//#warning restructure this the name list storage.
//    context=context+buildContext0Offset;
  }

  return KateHlContextModification (context, pops);
}

/**
 * The most important initialization function for each highlighting. It's called
 * each time a document gets a highlighting style assigned. parses the xml file
 * and creates a corresponding internal structure
 */
void KateHighlighting::makeContextList()
{
  if (noHl)  // if this a highlighting for "normal texts" only, tere is no need for a context list creation
    return;

  embeddedHls.clear();
  unresolvedContextReferences.clear();
  RegionList.clear();
  ContextNameList.clear();

  // prepare list creation. To reuse as much code as possible handle this
  // highlighting the same way as embedded onces
  embeddedHls.insert(iName,KateEmbeddedHlInfo());

  bool something_changed;
  // the context "0" id is 0 for this hl, all embedded context "0"s have offsets
  startctx=base_startctx=0;
  // inform everybody that we are building the highlighting contexts and itemlists
  building=true;

  do
  {
    kDebug(13010)<<"**************** Outer loop in make ContextList"<<endl;
    kDebug(13010)<<"**************** Hl List count:"<<embeddedHls.count()<<endl;
    something_changed=false; //assume all "embedded" hls have already been loaded
    for (KateEmbeddedHlInfos::const_iterator it=embeddedHls.begin(); it!=embeddedHls.end();++it)
    {
      if (!it.value().loaded)  // we found one, we still have to load
      {
        kDebug(13010)<<"**************** Inner loop in make ContextList"<<endl;
        QString identifierToUse;
        kDebug(13010)<<"Trying to open highlighting definition file: "<< it.key()<<endl;
        if (iName==it.key()) // the own identifier is known
          identifierToUse=identifier;
        else                 // all others have to be looked up
          identifierToUse=KateHlManager::self()->identifierForName(it.key());

        kDebug(13010)<<"Location is:"<< identifierToUse<<endl;

        buildPrefix=it.key()+':';  // attribute names get prefixed by the names
                                   // of the highlighting definitions they belong to

        if (identifierToUse.isEmpty() )
          kDebug(13010)<<"OHOH, unknown highlighting description referenced"<<endl;

        kDebug(13010)<<"setting ("<<it.key()<<") to loaded"<<endl;

        //mark hl as loaded
        it=embeddedHls.insert(it.key(),KateEmbeddedHlInfo(true,startctx));
        //set class member for context 0 offset, so we don't need to pass it around
        buildContext0Offset=startctx;
        //parse one hl definition file
        startctx=addToContextList(identifierToUse,startctx);

        if (noHl) return;  // an error occurred

        base_startctx = startctx;
        something_changed=true; // something has been loaded
      }
    }
  } while (something_changed);  // as long as there has been another file parsed
                  // repeat everything, there could be newly added embedded hls.


  // at this point all needed highlighing (sub)definitions are loaded. It's time
  // to resolve cross file  references (if there are any)
  kDebug(13010)<<"Unresolved contexts, which need attention: "<<unresolvedContextReferences.count()<<endl;

  //optimize this a littlebit
  for (KateHlUnresolvedCtxRefs::iterator unresIt=unresolvedContextReferences.begin();
       unresIt != unresolvedContextReferences.end();
       ++unresIt)
  {
    QString incCtx = unresIt.value();
    kDebug(13010) << "Context " <<incCtx << " is unresolved" << endl;

    // only resolve '##Name' contexts here; handleKateHlIncludeRules() can figure
    // out 'Name##Name'-style inclusions, but we screw it up
    if (incCtx.endsWith(":")) {
      kDebug(13010)<<"Looking up context0 for ruleset "<<incCtx<<endl;
      incCtx = incCtx.left(incCtx.length()-1);
      //try to find the context0 id for a given unresolvedReference
      KateEmbeddedHlInfos::const_iterator hlIt=embeddedHls.find(incCtx);
      if (hlIt!=embeddedHls.end())
        *(unresIt.key())=hlIt.value().context0;
    }
  }

  // eventually handle KateHlIncludeRules items, if they exist.
  // This has to be done after the cross file references, because it is allowed
  // to include the context0 from a different definition, than the one the rule
  // belongs to
  handleKateHlIncludeRules();

  embeddedHls.clear(); //save some memory.
  unresolvedContextReferences.clear(); //save some memory
  RegionList.clear();  // I think you get the idea ;)
  ContextNameList.clear();


  // if there have been errors show them
  if (!errorsAndWarnings.isEmpty())
  KMessageBox::detailedSorry(0L,i18n(
        "There were warning(s) and/or error(s) while parsing the syntax "
        "highlighting configuration."),
        errorsAndWarnings, i18n("Kate Syntax Highlighting Parser"));

  // we have finished
  building=false;
}

void KateHighlighting::handleKateHlIncludeRules()
{
  // if there are noe include rules to take care of, just return
  kDebug(13010)<<"KateHlIncludeRules, which need attention: " <<includeRules.size()<<endl;
  if (includeRules.isEmpty()) return;

  buildPrefix="";
  QString dummy;

  // By now the context0 references are resolved, now more or less only inner
  // file references are resolved. If we decide that arbitrary inclusion is
  // needed, this doesn't need to be changed, only the addToContextList
  // method.

  //resolove context names
  for (KateHlIncludeRules::iterator it=includeRules.begin(); it!=includeRules.end(); )
  {
    if ((*it)->incCtx.newContext==-1) // context unresolved ?
    {

      if ((*it)->incCtxN.isEmpty())
      {
        // no context name given, and no valid context id set, so this item is
        // going to be removed
        KateHlIncludeRules::iterator it1=it;
        ++it1;
        delete (*it);
        includeRules.erase(it);
        it=it1;
      }
      else
      {
        // resolve name to id
        (*it)->incCtx=getContextModificationFromString(&ContextNameList,(*it)->incCtxN,dummy).newContext;
        kDebug(13010)<<"Resolved "<<(*it)->incCtxN<< " to "<<(*it)->incCtx.newContext<<" for include rule"<<endl;
        // It would be good to look here somehow, if the result is valid
      }
    }
    else ++it; //nothing to do, already resolved (by the cross defintion reference resolver)
  }

  // now that all KateHlIncludeRule items should be valid and completely resolved,
  // do the real inclusion of the rules.
  // recursiveness is needed, because context 0 could include context 1, which
  // itself includes context 2 and so on.
  //  In that case we have to handle context 2 first, then 1, 0
  //TODO: catch circular references: eg 0->1->2->3->1
  while (!includeRules.isEmpty())
    handleKateHlIncludeRulesRecursive(0, &includeRules);
}

void KateHighlighting::handleKateHlIncludeRulesRecursive(int index, KateHlIncludeRules *list)
{
  if (index < 0 || index >= list->count()) return;  //invalid iterator, shouldn't happen, but better have a rule prepared ;)

  int index1 = index;
  int ctx = list->at(index1)->ctx;

  // find the last entry for the given context in the KateHlIncludeRules list
  // this is need if one context includes more than one. This saves us from
  // updating all insert positions:
  // eg: context 0:
  // pos 3 - include context 2
  // pos 5 - include context 3
  // During the building of the includeRules list the items are inserted in
  // ascending order, now we need it descending to make our life easier.
  while (index < list->count() && list->at(index)->ctx == ctx)
  {
    index1 = index;
    ++index;
  }

  // iterate over each include rule for the context the function has been called for.
  while (index1 >= 0 && index1 < list->count() && list->at(index1)->ctx == ctx)
  {
    KateHlContextModification ctx1 = list->at(index1)->incCtx;

    //let's see, if the the included context includes other contexts
    for (int index2 = 0; index2 < list->count(); ++index2)
    {
      if (list->at(index2)->ctx == ctx1.newContext)
      {
        //yes it does, so first handle that include rules, since we want to
        // include those subincludes too
        handleKateHlIncludeRulesRecursive(index2, list);
        break;
      }
    }

    // if the context we want to include had sub includes, they are already inserted there.
    KateHlContext *dest=m_contexts[ctx];
    KateHlContext *src=m_contexts[ctx1.newContext];
//     kDebug(3010)<<"linking included rules from "<<ctx<<" to "<<ctx1<<endl;

    // If so desired, change the dest attribute to the one of the src.
    // Required to make commenting work, if text matched by the included context
    // is a different highlight than the host context.
    if ( list->at(index1)->includeAttrib )
      dest->attr = src->attr;

    // insert the included context's rules starting at position p
    int p = list->at(index1)->pos;

    // remember some stuff
    int oldLen = dest->items.size();
    uint itemsToInsert = src->items.size();

    // resize target
    dest->items.resize (oldLen + itemsToInsert);

    // move old elements
    for (int i=oldLen-1; i >= p; --i)
      dest->items[i+itemsToInsert] = dest->items[i];

    // insert new stuff
    for (uint i=0; i < itemsToInsert; ++i  )
      dest->items[p+i] = src->items[i];

    index = index1; //backup the iterator
    --index1;  //move to the next entry, which has to be take care of
    delete list->takeAt(index); //free + remove the already handled data structure
  }
}

/**
 * Add one highlight to the contextlist.
 *
 * @return the number of contexts after this is added.
 */
int KateHighlighting::addToContextList(const QString &ident, int ctx0)
{
  kDebug(13010)<<"=== Adding hl with ident '"<<ident<<"'"<<endl;

  buildIdentifier=ident;
  KateSyntaxContextData *data, *datasub;
  KateHlItem *c;

  QString dummy;

  // Let the syntax document class know, which file we'd like to parse
  if (!KateHlManager::self()->syntax->setIdentifier(ident))
  {
    noHl=true;
    KMessageBox::information(0L,i18n(
        "Since there has been an error parsing the highlighting description, "
        "this highlighting will be disabled"));
    return 0;
  }

  // only read for the own stuff
  if (identifier == ident)
  {
    readIndentationConfig ();
  }

  RegionList<<"!KateInternal_TopLevel!";

  m_hlIndex[internalIDList.count()] = ident;
  m_additionalData.insert( ident, new HighlightPropertyBag );

  // fill out the propertybag
  readCommentConfig();
  readEmptyLineConfig();
  readGlobalKeywordConfig();
  readWordWrapConfig();

  readFoldingConfig ();

  QString ctxName;

  // This list is needed for the translation of the attribute parameter,
  // if the itemData name is given instead of the index
  addToKateExtendedAttributeList();
  QList<KateExtendedAttribute::Ptr> iDl = internalIDList;

  createContextNameList(&ContextNameList,ctx0);


  kDebug(13010)<<"Parsing Context structure"<<endl;
  //start the real work
  data=KateHlManager::self()->syntax->getGroupInfo("highlighting","context");
  uint i=buildContext0Offset;
  if (data)
  {
    while (KateHlManager::self()->syntax->nextGroup(data))
    {
      kDebug(13010)<<"Found a context in file, building structure now"<<endl;
      //BEGIN - Translation of the attribute parameter
      QString tmpAttr=KateHlManager::self()->syntax->groupData(data,QString("attribute")).simplified();
      int attr;
      if (QString("%1").arg(tmpAttr.toInt())==tmpAttr)
        attr=tmpAttr.toInt();
      else
        attr=lookupAttrName(tmpAttr,iDl);
      //END - Translation of the attribute parameter

      ctxName=buildPrefix+KateHlManager::self()->syntax->groupData(data,QString("lineEndContext")).simplified();

      QString tmpLineEndContext=KateHlManager::self()->syntax->groupData(data,QString("lineEndContext")).simplified();
      KateHlContextModification context;

      context=getContextModificationFromString(&ContextNameList, tmpLineEndContext,dummy);

      QString tmpNIBF = KateHlManager::self()->syntax->groupData(data, QString("noIndentationBasedFolding") );
      bool noIndentationBasedFolding=IS_TRUE(tmpNIBF);

      //BEGIN get fallthrough props
      bool ft = false;
      KateHlContextModification ftc = 0; // fallthrough context
      if ( i > 0 )  // fallthrough is not smart in context 0
      {
        QString tmpFt = KateHlManager::self()->syntax->groupData(data, QString("fallthrough") );
        if ( IS_TRUE(tmpFt) )
          ft = true;
        if ( ft )
        {
          QString tmpFtc = KateHlManager::self()->syntax->groupData( data, QString("fallthroughContext") );

          ftc=getContextModificationFromString(&ContextNameList, tmpFtc,dummy);

          // stay is not allowed, we need to #pop or push some context...
          if (ftc.type == KateHlContextModification::doNothing) ftc = 0;

          kDebug(13010)<<"Setting fall through context (context "<<i<<"): "<<ftc.newContext<<endl;
        }
      }
      //END falltrhough props

      bool dynamic = false;
      QString tmpDynamic = KateHlManager::self()->syntax->groupData(data, QString("dynamic") );
      if ( tmpDynamic.toLower() == "true" ||  tmpDynamic.toInt() == 1 )
        dynamic = true;

      KateHlContext *ctxNew = new KateHlContext (
        ident,
        attr,
        context,
        (KateHlManager::self()->syntax->groupData(data,QString("lineBeginContext"))).isEmpty()?-1:
        (KateHlManager::self()->syntax->groupData(data,QString("lineBeginContext"))).toInt(),
        ft, ftc, dynamic,noIndentationBasedFolding);

      m_contexts.push_back (ctxNew);

      kDebug(13010) << "INDEX: " << i << " LENGTH " << m_contexts.size()-1 << endl;

      //Let's create all items for the context
      while (KateHlManager::self()->syntax->nextItem(data))
      {
//    kDebug(13010)<< "In make Contextlist: Item:"<<endl;

      // KateHlIncludeRules : add a pointer to each item in that context
        // TODO add a attrib includeAttrib
      QString tag = KateHlManager::self()->syntax->groupItemData(data,QString(""));
      if ( tag == "IncludeRules" ) //if the new item is an Include rule, we have to take special care
      {
        QString incCtx = KateHlManager::self()->syntax->groupItemData( data, QString("context"));
        QString incAttrib = KateHlManager::self()->syntax->groupItemData( data, QString("includeAttrib"));
        bool includeAttrib = IS_TRUE( incAttrib );

        // only context refernces of type Name, ##Name, and Subname##Name are allowed
        if (incCtx.startsWith("##") || (!incCtx.startsWith("#")))
        {
           int incCtxi = incCtx.indexOf ("##");
           //#stay, #pop is not interesting here
           if (incCtxi >= 0)
           {
             QString incSet = incCtx.mid(incCtxi + 2);
             QString incCtxN = incSet + ':' + incCtx.left(incCtxi);

             //a cross highlighting reference
             kDebug(13010)<<"Cross highlight reference <IncludeRules>, context "<<incCtxN<<endl;
             KateHlIncludeRule *ir=new KateHlIncludeRule(i,m_contexts[i]->items.count(),incCtxN,includeAttrib);

             //use the same way to determine cross hl file references as other items do
             if (!embeddedHls.contains(incSet))
               embeddedHls.insert(incSet,KateEmbeddedHlInfo());
             else
               kDebug(13010)<<"Skipping embeddedHls.insert for "<<incCtxN<<endl;

            unresolvedContextReferences.insert(&(ir->incCtx), incCtxN);

            includeRules.append(ir);
          }
          else
          {
            // a local reference -> just initialize the include rule structure
            incCtx=buildPrefix+incCtx.simplified ();
            includeRules.append(new KateHlIncludeRule(i,m_contexts[i]->items.count(),incCtx, includeAttrib));
          }
        }

        continue;
      }
      // TODO -- can we remove the block below??
#if 0
                QString tag = KateHlManager::self()->syntax->groupKateExtendedAttribute(data,QString(""));
                if ( tag == "IncludeRules" ) {
                  // attrib context: the index (jowenn, i think using names here
                  // would be a cool feat, goes for mentioning the context in
                  // any item. a map or dict?)
                  int ctxId = getIdFromString(&ContextNameList,
                                               KateHlManager::self()->syntax->groupKateExtendedAttribute( data, QString("context")),dummy); // the index is *required*
                  if ( ctxId > -1) { // we can even reuse rules of 0 if we want to:)
                    kDebug(13010)<<"makeContextList["<<i<<"]: including all items of context "<<ctxId<<endl;
                    if ( ctxId < (int) i ) { // must be defined
                      for ( c = m_contexts[ctxId]->items.first(); c; c = m_contexts[ctxId]->items.next() )
                        m_contexts[i]->items.append(c);
                    }
                    else
                      kDebug(13010)<<"Context "<<ctxId<<"not defined. You can not include the rules of an undefined context"<<endl;
                  }
                  continue; // while nextItem
                }
#endif
      c=createKateHlItem(data,iDl,&RegionList,&ContextNameList);
      if (c)
      {
        m_contexts[i]->items.append(c);

        // Not supported completely atm and only one level. Subitems.(all have
        // to be matched to at once)
        datasub=KateHlManager::self()->syntax->getSubItems(data);
        for (bool tmpbool=KateHlManager::self()->syntax->nextItem(datasub);
             tmpbool;
             tmpbool=KateHlManager::self()->syntax->nextItem(datasub))
        {
          c->subItems.resize (c->subItems.size()+1);
          c->subItems[c->subItems.size()-1] = createKateHlItem(datasub,iDl,&RegionList,&ContextNameList);
        }
        KateHlManager::self()->syntax->freeGroupInfo(datasub);
      }
      }
      i++;
    }
  }

  KateHlManager::self()->syntax->freeGroupInfo(data);

  if (RegionList.count()!=1)
    folding=true;

  folding = folding || m_foldingIndentationSensitive;

  //BEGIN Resolve multiline region if possible
  if (!m_additionalData[ ident ]->multiLineRegion.isEmpty()) {
    long commentregionid=RegionList.indexOf( m_additionalData[ ident ]->multiLineRegion );
    if (-1==commentregionid) {
      errorsAndWarnings+=i18n(
          "<B>%1</B>: Specified multiline comment region (%2) could not be resolved<BR>"
                             , buildIdentifier,  m_additionalData[ ident ]->multiLineRegion );
      m_additionalData[ ident ]->multiLineRegion.clear();
      kDebug(13010)<<"ERROR comment region attribute could not be resolved"<<endl;

    } else {
      m_additionalData[ ident ]->multiLineRegion=QString::number(commentregionid+1);
      kDebug(13010)<<"comment region resolved to:"<<m_additionalData[ ident ]->multiLineRegion<<endl;
    }
  }
  //END Resolve multiline region if possible
  return i;
}

void KateHighlighting::clearAttributeArrays ()
{
  QMutableHashIterator< int, QList<KTextEditor::Attribute::Ptr> > it = m_attributeArrays;
  while (it.hasNext())
  {
    it.next();

    // k, schema correct, let create the data
    KateAttributeList defaultStyleList;

    KateHlManager::self()->getDefaults(it.key(), defaultStyleList);

    QList<KateExtendedAttribute::Ptr> itemDataList;
    getKateExtendedAttributeList(it.key(), itemDataList);

    uint nAttribs = itemDataList.count();
    QList<KTextEditor::Attribute::Ptr>& array = it.value();
    array.clear();

    for (uint z = 0; z < nAttribs; z++)
    {
      KateExtendedAttribute::Ptr itemData = itemDataList.at(z);
      KTextEditor::Attribute::Ptr n = defaultStyleList.at(itemData->defaultStyleIndex());

      if (itemData && itemData->hasAnyProperty())
        *n += *itemData;

      array.append(n);
    }
  }
}

QList<KTextEditor::Attribute::Ptr> KateHighlighting::attributes (uint schema)
{
  // found it, already floating around
  if (m_attributeArrays.contains(schema))
    return m_attributeArrays[schema];

  // ohh, not found, check if valid schema number
  if (!KateGlobal::self()->schemaManager()->validSchema(schema))
  {
    // uhh, not valid :/, stick with normal default schema, it's always there !
    return attributes (0);
  }

  // k, schema correct, let create the data
  QList<KTextEditor::Attribute::Ptr> array;
  KateAttributeList defaultStyleList;

  KateHlManager::self()->getDefaults(schema, defaultStyleList);

  QList<KateExtendedAttribute::Ptr> itemDataList;
  getKateExtendedAttributeList(schema, itemDataList);

  uint nAttribs = itemDataList.count();

  for (uint z = 0; z < nAttribs; z++)
  {
    KateExtendedAttribute::Ptr itemData = itemDataList.at(z);
    KTextEditor::Attribute::Ptr n = defaultStyleList.at(itemData->defaultStyleIndex());

    if (itemData && itemData->hasAnyProperty())
      *n += *itemData;

    array.append(n);
  }

  m_attributeArrays.insert(schema, array);

  return array;
}

//END

//BEGIN KateHlManager
KateHlManager::KateHlManager()
  : QObject()
  , m_config ("katesyntaxhighlightingrc", KConfig::NoGlobals)
  , commonSuffixes (QString(".orig;.new;~;.bak;.BAK").split(';'))
  , syntax (new KateSyntaxDocument())
  , dynamicCtxsCount(0)
  , forceNoDCReset(false)
{
  KateSyntaxModeList modeList = syntax->modeList();
  for (int i=0; i < modeList.count(); i++)
  {
    KateHighlighting *hl = new KateHighlighting(modeList[i]);

    int insert = 0;
    for (; insert <= hlList.count(); insert++)
    {
      if (insert == hlList.count())
        break;

      if ( QString(hlList.at(insert)->section() + hlList.at(insert)->nameTranslated()).toLower()
            > QString(hl->section() + hl->nameTranslated()).toLower() )
        break;
    }

    hlList.insert (insert, hl);
    hlDict.insert (hl->name(), hl);
  }

  // Normal HL
  KateHighlighting *hl = new KateHighlighting(0);
  hlList.prepend (hl);
  hlDict.insert (hl->name(), hl);

  lastCtxsReset.start();
}

KateHlManager::~KateHlManager()
{
  delete syntax;
  qDeleteAll(hlList);
}

KateHlManager *KateHlManager::self()
{
  return KateGlobal::self ()->hlManager ();
}

KateHighlighting *KateHlManager::getHl(int n)
{
  if (n < 0 || n >= hlList.count())
    n = 0;

  return hlList.at(n);
}

int KateHlManager::nameFind(const QString &name)
{
  int z (hlList.count() - 1);
  for (; z > 0; z--)
    if (hlList.at(z)->name() == name)
      return z;

  return z;
}

int KateHlManager::detectHighlighting (KateDocument *doc)
{
  int hl = wildcardFind( doc->url().fileName() );
  if ( hl < 0 )
    hl = mimeFind ( doc );

  return hl;
}

int KateHlManager::wildcardFind(const QString &fileName)
{
  int result = -1;
  if ((result = realWildcardFind(fileName)) != -1)
    return result;

  int length = fileName.length();
  QString backupSuffix = KateDocumentConfig::global()->backupSuffix();
  if (fileName.endsWith(backupSuffix)) {
    if ((result = realWildcardFind(fileName.left(length - backupSuffix.length()))) != -1)
      return result;
  }

  for (QStringList::Iterator it = commonSuffixes.begin(); it != commonSuffixes.end(); ++it) {
    if (*it != backupSuffix && fileName.endsWith(*it)) {
      if ((result = realWildcardFind(fileName.left(length - (*it).length()))) != -1)
        return result;
    }
  }

  return -1;
}

int KateHlManager::realWildcardFind(const QString &fileName)
{
  QList<KateHighlighting*> highlights;

  foreach (KateHighlighting *highlight, hlList) {
    highlight->loadWildcards();

    foreach (QString extension, highlight->getPlainExtensions())
      if (fileName.endsWith(extension))
        highlights.append(highlight);

    foreach (QRegExp re, highlight->getRegexpExtensions()) {
      if (re.exactMatch(fileName))
        highlights.append(highlight);
    }
  }

  if ( !highlights.isEmpty() )
  {
    int pri = -1;
    int hl = -1;

    foreach (KateHighlighting *highlight, highlights)
    {
      if (highlight->priority() > pri)
      {
        pri = highlight->priority();
        hl = hlList.indexOf(highlight);
      }
    }
    return hl;
  }

  return -1;
}

int KateHlManager::mimeFind( KateDocument *doc )
{
  static QRegExp sep("\\s*;\\s*");

  KMimeType::Ptr mt = doc->mimeTypeForContent();

  QList<KateHighlighting*> highlights;

  foreach (KateHighlighting *highlight, hlList)
  {
    foreach (QString mimeType, highlight->getMimetypes().split( sep, QString::SkipEmptyParts ))
    {
      if ( mimeType == mt->name() ) // faster than a regexp i guess?
        highlights.append (highlight);
    }
  }

  if ( !highlights.isEmpty() )
  {
    int pri = -1;
    int hl = -1;

    foreach (KateHighlighting *highlight, highlights)
    {
      if (highlight->priority() > pri)
      {
        pri = highlight->priority();
        hl = hlList.indexOf(highlight);
      }
    }

    return hl;
  }

  return -1;
}

uint KateHlManager::defaultStyles()
{
  return 14;
}

QString KateHlManager::defaultStyleName(int n, bool translateNames)
{
  static QStringList names;
  static QStringList translatedNames;

  if (names.isEmpty())
  {
    names << "Normal";
    names << "Keyword";
    names << "Data Type";
    names << "Decimal/Value";
    names << "Base-N Integer";
    names << "Floating Point";
    names << "Character";
    names << "String";
    names << "Comment";
    names << "Others";
    names << "Alert";
    names << "Function";
    // this next one is for denoting the beginning/end of a user defined folding region
    names << "Region Marker";
    // this one is for marking invalid input
    names << "Error";

    translatedNames << i18n("Normal");
    translatedNames << i18n("Keyword");
    translatedNames << i18n("Data Type");
    translatedNames << i18n("Decimal/Value");
    translatedNames << i18n("Base-N Integer");
    translatedNames << i18n("Floating Point");
    translatedNames << i18n("Character");
    translatedNames << i18n("String");
    translatedNames << i18n("Comment");
    translatedNames << i18n("Others");
    translatedNames << i18n("Alert");
    translatedNames << i18n("Function");
    // this next one is for denoting the beginning/end of a user defined folding region
    translatedNames << i18n("Region Marker");
    // this one is for marking invalid input
    translatedNames << i18n("Error");
  }

  return translateNames ? translatedNames[n] : names[n];
}

void KateHlManager::getDefaults(uint schema, KateAttributeList &list)
{
  KTextEditor::Attribute::Ptr normal(new KTextEditor::Attribute());
  normal->setForeground(Qt::black);
  normal->setSelectedForeground(Qt::white);
  list.append(normal);

  KTextEditor::Attribute::Ptr keyword(new KTextEditor::Attribute());
  keyword->setForeground(Qt::black);
  keyword->setSelectedForeground(Qt::white);
  keyword->setFontBold(true);
  list.append(keyword);

  KTextEditor::Attribute::Ptr dataType(new KTextEditor::Attribute());
  dataType->setForeground(Qt::darkRed);
  dataType->setSelectedForeground(Qt::white);
  list.append(dataType);

  KTextEditor::Attribute::Ptr decimal(new KTextEditor::Attribute());
  decimal->setForeground(Qt::blue);
  decimal->setSelectedForeground(Qt::cyan);
  list.append(decimal);

  KTextEditor::Attribute::Ptr basen(new KTextEditor::Attribute());
  basen->setForeground(Qt::darkCyan);
  basen->setSelectedForeground(Qt::cyan);
  list.append(basen);

  KTextEditor::Attribute::Ptr floatAttribute(new KTextEditor::Attribute());
  floatAttribute->setForeground(Qt::darkMagenta);
  floatAttribute->setSelectedForeground(Qt::cyan);
  list.append(floatAttribute);

  KTextEditor::Attribute::Ptr charAttribute(new KTextEditor::Attribute());
  charAttribute->setForeground(Qt::magenta);
  charAttribute->setSelectedForeground(Qt::magenta);
  list.append(charAttribute);

  KTextEditor::Attribute::Ptr string(new KTextEditor::Attribute());
  string->setForeground(QColor::QColor("#D00"));
  string->setSelectedForeground(Qt::red);
  list.append(string);

  KTextEditor::Attribute::Ptr comment(new KTextEditor::Attribute());
  comment->setForeground(Qt::darkGray);
  comment->setSelectedForeground(Qt::gray);
  comment->setFontItalic(true);
  list.append(comment);

  KTextEditor::Attribute::Ptr others(new KTextEditor::Attribute());
  others->setForeground(Qt::darkGreen);
  others->setSelectedForeground(Qt::green);
  list.append(others);

  KTextEditor::Attribute::Ptr alert(new KTextEditor::Attribute());
  alert->setForeground(Qt::black);
  alert->setSelectedForeground( QColor::QColor("#FCC") );
  alert->setFontBold(true);
  alert->setBackground( QColor::QColor("#FCC") );
  list.append(alert);

  KTextEditor::Attribute::Ptr functionAttribute(new KTextEditor::Attribute());
  functionAttribute->setForeground(Qt::darkBlue);
  functionAttribute->setSelectedForeground(Qt::white);
  list.append(functionAttribute);

  KTextEditor::Attribute::Ptr regionmarker(new KTextEditor::Attribute());
  regionmarker->setForeground(Qt::white);
  regionmarker->setBackground(Qt::gray);
  regionmarker->setSelectedForeground(Qt::gray);
  list.append(regionmarker);

  KTextEditor::Attribute::Ptr error(new KTextEditor::Attribute());
  error->setForeground(Qt::red);
  error->setFontUnderline(true);
  error->setSelectedForeground(Qt::red);
  list.append(error);

  KConfigGroup config(KateHlManager::self()->self()->getKConfig(),
                      "Default Item Styles - Schema " + KateGlobal::self()->schemaManager()->name(schema));

  for (uint z = 0; z < defaultStyles(); z++)
  {
    KTextEditor::Attribute::Ptr i = list.at(z);
    QStringList s = config.readEntry(defaultStyleName(z), QStringList());
    if (!s.isEmpty())
    {
      while( s.count()<8)
        s << "";

      QString tmp;
      QRgb col;

      tmp=s[0]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); i->setForeground(QColor(col)); }

      tmp=s[1]; if (!tmp.isEmpty()) {
         col=tmp.toUInt(0,16); i->setSelectedForeground(QColor(col)); }

      tmp=s[2]; if (!tmp.isEmpty()) i->setFontBold(tmp!="0");

      tmp=s[3]; if (!tmp.isEmpty()) i->setFontItalic(tmp!="0");

      tmp=s[4]; if (!tmp.isEmpty()) i->setFontStrikeOut(tmp!="0");

      tmp=s[5]; if (!tmp.isEmpty()) i->setFontUnderline(tmp!="0");

      tmp=s[6]; if (!tmp.isEmpty()) {
        if ( tmp != "-" )
        {
          col=tmp.toUInt(0,16);
          i->setBackground(QColor(col));
        }
        else
          i->clearBackground();
      }
      tmp=s[7]; if (!tmp.isEmpty()) {
        if ( tmp != "-" )
        {
          col=tmp.toUInt(0,16);
          i->setSelectedBackground(QColor(col));
        }
        else
          i->clearProperty(KTextEditor::Attribute::SelectedBackground);
      }
    }
  }
}

void KateHlManager::setDefaults(uint schema, KateAttributeList &list)
{
  KConfigGroup config(KateHlManager::self()->self()->getKConfig(),
                      "Default Item Styles - Schema " + KateGlobal::self()->schemaManager()->name(schema));

  for (uint z = 0; z < defaultStyles(); z++)
  {
    QStringList settings;
    KTextEditor::Attribute::Ptr p = list.at(z);

    settings<<(p->hasProperty(QTextFormat::ForegroundBrush)?QString::number(p->foreground().color().rgb(),16):"");
    settings<<(p->hasProperty(KTextEditor::Attribute::SelectedForeground)?QString::number(p->selectedForeground().color().rgb(),16):"");
    settings<<(p->hasProperty(QTextFormat::FontWeight)?(p->fontBold()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontItalic)?(p->fontItalic()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontStrikeOut)?(p->fontStrikeOut()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::FontUnderline)?(p->fontUnderline()?"1":"0"):"");
    settings<<(p->hasProperty(QTextFormat::BackgroundBrush)?QString::number(p->background().color().rgb(),16):"");
    settings<<(p->hasProperty(KTextEditor::Attribute::SelectedBackground)?QString::number(p->selectedBackground().color().rgb(),16):"");
    settings<<"---";

    config.writeEntry(defaultStyleName(z),settings);
  }

  emit changed();
}

int KateHlManager::highlights()
{
  return (int) hlList.count();
}

QString KateHlManager::hlName(int n)
{
  return hlList.at(n)->name();
}

QString KateHlManager::hlNameTranslated(int n)
{
  return hlList.at(n)->nameTranslated();
}

QString KateHlManager::hlSection(int n)
{
  return hlList.at(n)->section();
}

bool KateHlManager::hlHidden(int n)
{
  return hlList.at(n)->hidden();
}

QString KateHlManager::identifierForName(const QString& name)
{
  KateHighlighting *hl = 0;

  if ((hl = hlDict[name]))
    return hl->getIdentifier ();

  return QString();
}

bool KateHlManager::resetDynamicCtxs()
{
  if (forceNoDCReset)
    return false;

  if (lastCtxsReset.elapsed() < KATE_DYNAMIC_CONTEXTS_RESET_DELAY)
    return false;

  foreach (KateHighlighting *hl, hlList)
    hl->dropDynamicContexts();

  dynamicCtxsCount = 0;
  lastCtxsReset.start();

  return true;
}
//END

//BEGIN KateHighlightAction
KateViewHighlightAction::~KateViewHighlightAction()
{
  qDeleteAll (subMenus);
}

void KateViewHighlightAction::init()
{
  m_doc = 0;

  connect(menu(),SIGNAL(aboutToShow()),this,SLOT(slotAboutToShow()));
}

void KateViewHighlightAction::updateMenu (KateDocument *doc)
{
  m_doc = doc;
}

void KateViewHighlightAction::slotAboutToShow()
{
  for (int z=0; z < KateHlManager::self()->highlights(); z++)
  {
    QString hlName = KateHlManager::self()->hlNameTranslated (z);
    QString hlSection = KateHlManager::self()->hlSection (z);

    if (!KateHlManager::self()->hlHidden(z))
    {
      if ( !hlSection.isEmpty() && !names.contains(hlName) )
      {
        if (!subMenusName.contains(hlSection))
        {
          subMenusName << hlSection;
          QMenu *qmenu = new QMenu ('&'+hlSection);
          subMenus.append(qmenu);
          menu()->addMenu( qmenu );
        }

        int m = subMenusName.indexOf (hlSection);
        names << hlName;
        QAction *a=subMenus.at(m)->addAction( '&' + hlName, this, SLOT(setHl()));
	a->setData(z);
	a->setCheckable(true);
	subActions.append(a);
      }
      else if (!names.contains(hlName))
      {
        names << hlName;
        QAction *a=menu()->addAction ( '&' + hlName, this, SLOT(setHl()));
	a->setData(z);
	a->setCheckable(true);
	subActions.append(a);
      }
    }
  }

  if (!m_doc) return;
  for (int i=0;i<subActions.count();i++) {
	subActions[i]->setChecked(false);
  }

  int mode=m_doc->hlMode();
  int start=mode;
  if ( (mode<0) || (mode>=subActions.count() ) )
    start=subActions.count()-1;
  for(;(start>0) && (subActions[start]->data().toInt()!=mode);start--);
  if (start>=0)
    subActions[start]->setChecked(true);

}

void KateViewHighlightAction::setHl ()
{
  if (!sender()) return;
  QAction *action=qobject_cast<QAction*>(sender());
  if (!action) return;
  int mode=action->data().toInt();
  KateDocument *doc=m_doc;

  if (doc)
    doc->setHighlighting(KateHlManager::self()->hlName (mode));
}

//END KateViewHighlightAction

// kate: space-indent on; indent-width 2; replace-tabs on;
