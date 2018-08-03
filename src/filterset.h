/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef FILTERSET_H
#define FILTERSET_H

#include <QRegularExpression>
#include <QColor>
#include <QMetaType>
#include <QVector>
#include <vector>

#include "persistable.h"

// Represents a filter, i.e. a regexp and the colors matching text
// should be rendered in.
class Filter
{
  public:
    // Construct an uninitialized Filter (when reading from a config file)
    Filter();
    Filter(const QString& pattern, bool ignoreCase,
            const QString& foreColor, const QString& backColor,
            int origin = -1, int loaded_offset = -1 );

    bool hasMatch( const QString& string ) const;

    // Accessor functions
    QString pattern() const;
    void setPattern( const QString& pattern );
    bool ignoreCase() const;
    void setIgnoreCase( bool ignoreCase );
    const QString& foreColorName() const;
    void setForeColor( const QString& foreColorName );
    const QString& backColorName() const;
    void setBackColor( const QString& backColorName );
    void setOrigin( int origin );
    int origin() const;
    void setLoadedOffset( int offset );
    int loadedOffset() const;

    // Operators for serialization
    // (must be kept to migrate filters from <=0.8.2)
    friend QDataStream& operator<<( QDataStream& out, const Filter& object );
    friend QDataStream& operator>>( QDataStream& in, Filter& object );

    // Reads/writes the current config in the QSettings object passed
    void saveToStorage( QSettings& settings, bool origin = true ) const;
    void retrieveFromStorage( QSettings& settings, int origin = -1 );

    bool operator==(const Filter& other) const { return
           regexp_        == other.regexp_
        && foreColorName_ == other.foreColorName_
        && backColorName_ == other.backColorName_
        //&& enabled_       == other.enabled_
        //&& origin_        == other.origin_
        ;
    }
    bool operator!=(const Filter& other) const { return ! ( *this == other ); }

  private:
    QRegularExpression regexp_;
    QString foreColorName_;
    QString backColorName_;
    bool enabled_;
    int origin_; // index into Persistent( "loadedFilterSets" ), -1 if not loaded from a filterSet-file
    int loaded_offset_; // index into Persistent( "loadedFilterSets" )[origin_], -1 if not loaded from a filterSet-file
};

// Represents an ordered set of filters to be applied to each line displayed.
class FilterSet : public Persistable
{
  private:
    using FilterList = QList<Filter>;

  public:
    using iterator = FilterList::iterator;
    using const_iterator = FilterList::const_iterator;
    using reference = FilterList::reference;
    using const_reference = FilterList::const_reference;
    using size_type = FilterList::size_type;

  public:
    // Construct an empty filter set
    FilterSet();

    // Returns weither the passed line match a filter of the set,
    // if so, it returns the fore/back colors the line should use.
    // Ownership of the colors is transfered to the caller.
    bool matchLine( const QString& line,
            QColor* foreColor, QColor* backColor ) const;

    // Reads/writes the current config in the QSettings object passed
    virtual void saveToStorage( QSettings& settings ) const { saveToStorage( settings, true ); }
    void saveToStorage( QSettings& settings, bool origin ) const;
    virtual void retrieveFromStorage( QSettings& settings ) { retrieveFromStorage( settings, -1 ); }
    void retrieveFromStorage( QSettings& settings, int origin );

    // Operators for serialization
    // (must be kept to migrate filters from <=0.8.2)
    friend QDataStream& operator<<(
            QDataStream& out, const FilterSet& object );
    friend QDataStream& operator>>(
            QDataStream& in, FilterSet& object );

  private:
    reference& operator[]( int index ) { return filterList[index]; }
    const_reference& operator[]( int index ) const { return filterList[index]; }

    reference& front() { return filterList.front(); }
    const_reference& front() const { return filterList.front(); }
    reference& back() { return filterList.back(); }
    const_reference& back() const { return filterList.back(); }

    iterator begin() { using std::begin; return begin( filterList ); }
    iterator end() { using std::end; return end( filterList ); }
    const_iterator cbegin() const { using std::begin; return begin( filterList ); }
    const_iterator cend() const { using std::end; return end( filterList ); }

    size_type size() const { return filterList.size(); }
    bool empty() const { return filterList.empty(); }

  private:
    static const int FILTERSET_VERSION;

    FilterList filterList;

    // To simplify this class interface, FilterDialog can access our
    // internal structure directly.
    friend class FiltersDialog;
    friend class Filter;
};

struct NamedFilterSet final
{
  public:
    NamedFilterSet( QString file, bool is_missing = false ) : filename( file ), missing( is_missing ) {}

  public:
    QString filename;
    FilterSet set;
    bool missing;
};

class LoadedFilterSets : public Persistable
{
  private:
    // QVector has some issues with NamedFilterSet that std::vector does not.
    using NamedFilterList = std::vector<NamedFilterSet>;

  public:
    using iterator = NamedFilterList::iterator;
    using const_iterator = NamedFilterList::const_iterator;
    using reference = NamedFilterList::reference;
    using const_reference = NamedFilterList::const_reference;
    using size_type = NamedFilterList::size_type;

  public:
    // Reads/writes the current config in the QSettings object passed
    virtual void saveToStorage( QSettings& settings ) const;
    virtual void retrieveFromStorage( QSettings& settings );

  private:
    reference& operator[]( int index ) { return namedFilterSets[index]; }
    const_reference& operator[]( int index ) const { return namedFilterSets[index]; }

    reference& front() { return namedFilterSets.front(); }
    const_reference& front() const { return namedFilterSets.front(); }
    reference& back() { return namedFilterSets.back(); }
    const_reference& back() const { return namedFilterSets.back(); }

    iterator begin() { using std::begin; return begin( namedFilterSets ); }
    iterator end() { using std::end; return end( namedFilterSets ); }
    const_iterator cbegin() const { using std::begin; return begin( namedFilterSets ); }
    const_iterator cend() const { using std::end; return end( namedFilterSets ); }

    size_type size() const { return namedFilterSets.size(); }

  private:
    static const int LOADED_FILTERSET_VERSION;

    NamedFilterList namedFilterSets;

    // To simplify this class interface, FilterDialog can access our
    // internal structure directly.
    friend class FiltersDialog;
    friend class Filter;
};

Q_DECLARE_METATYPE(FilterSet)

#endif
