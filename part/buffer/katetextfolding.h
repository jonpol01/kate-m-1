/*  This file is part of the Kate project.
 *
 *  Copyright (C) 2013 Christoph Cullmann <cullmann@kde.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public License
 *  along with this library; see the file COPYING.LIB.  If not, write to
 *  the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301, USA.
 */

#ifndef KATE_TEXTFOLDING_H
#define KATE_TEXTFOLDING_H

#include "katepartprivate_export.h"

#include "ktexteditor/range.h"

#include <QObject>

namespace Kate {

class TextBuffer;
class TextCursor;

/**
 * Class representing the folding information for a TextBuffer.
 * The interface allows to arbitrary fold given regions of a buffer as long
 * as they are well nested.
 * Multiple instances of this class can exist for the same buffer.
 */
class KATEPART_TESTS_EXPORT TextFolding : QObject {
  Q_OBJECT
  
  public: 
    /**
     * Create folding object for given buffer.
     * @param buffer text buffer we want to provide folding info for
     */
    TextFolding (TextBuffer &buffer);

    /**
     * Folding state of a range
     */
    enum FoldingRangeState {
      /**
       * Range is folded away
       */
      Folded,
      
      /**
       * Range is visible
       */
      Visible
    };
    
    /**
     * Create a new folding range.
     * @param range folding range
     * @param state state after creation, e.g. folded or unfolded
     * @return success, might fail if the range can not be nested in the existing ones!
     */
    bool newFoldingRange (const KTextEditor::Range &range, FoldingRangeState state);
    
    /**
     * Dump folding state as string, for unit testing and debugging
     * @return current state as text
     */
    QString debugDump () const;
    
    /**
     * Print state to stdout for testing
     */
    void debugPrint (const QString &title) const;
  
  private:
    /**
     * Data holder for text folding range and its nested children
     */
    class FoldingRange {
      public:
        /**
         * Construct new one
         * @param buffer text buffer to use
         * @param range folding range
         * @param state state after creation, e.g. folded or unfolded
         */
        FoldingRange (TextBuffer &buffer, const KTextEditor::Range &range, FoldingRangeState state);
        
        /**
         * Cleanup
         */
        ~FoldingRange ();
        
        /**
         * Vector of range pointers
         */
        typedef QVector<FoldingRange*> Vector;
        
        /**
         * start moving cursor
         * NO range to be more efficient
         */
        Kate::TextCursor *start;
        
        /**
         * end moving cursor
         * NO range to be more efficient
         */
        Kate::TextCursor *end;
        
        /**
         * parent range, if any
         */
        FoldingRange *parent;
        
        /**
         * nested ranges, if any
         * this will always be sorted and non-overlapping
         * nested ranges are inside these ranges
         */
        FoldingRange::Vector nestedRanges;
        
        /**
         * Folding state
         */
        FoldingRangeState state;
    };
    
    /**
     * Dump folding state of given vector as string, for unit testing and debugging.
     * Will recurse.
     * @param ranges ranges vector to dump
     * @return current state as text
     */
    static QString debugDump (const TextFolding::FoldingRange::Vector &ranges);
    
    /**
     * Helper to insert folding range into existing ones.
     * Might fail, if not correctly nested.
     * Then the outside must take care of the passed pointer, e.g. delete it.
     * Will sanitize the ranges vectors, purge invalid/empty ranges.
     * @param existingRanges ranges into which we want to insert the new one
     * @param newRange new folding range
     * @return success, if false, newRange should be deleted afterwards, else it is registered internally
     */
    bool insertNewFoldingRange (TextFolding::FoldingRange::Vector &existingRanges, TextFolding::FoldingRange *newRange);
  
    /**
     * Compare two ranges by their start cursor.
     * @param a first range
     * @param b second range
     */
    static bool compareRangeByStart (FoldingRange *a, FoldingRange *b);

    /**
     * Compare two ranges by their end cursor.
     * @param a first range
     * @param b second range
     */
    static bool compareRangeByEnd (FoldingRange *a, FoldingRange *b);
    
  private:
    /**
     * parent text buffer
     * is a reference, and no pointer, as this must always exist and can't change
     * can't be const, as we create text cursors!
     */
    TextBuffer &m_buffer;
    
    /**
     * toplevel folding ranges
     * this will always be sorted and non-overlapping
     * nested ranges are inside these ranges
     */
    FoldingRange::Vector m_foldingRanges;
};

}

#endif
