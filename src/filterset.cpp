/*
 * Copyright (C) 2009, 2010, 2011 Nicolas Bonnefon and other contributors
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

// This file implements classes Filter and FilterSet

#include <QDirIterator>
#include <QSettings>
#include <QStandardPaths>
#include <QDataStream>
#include <QMessageBox>

#include <cassert>

#include "log.h"
#include "filterset.h"
#include "persistentinfo.h"

const int FilterSet::FILTERSET_VERSION = 1;
const int LoadedFilterSets::LOADED_FILTERSET_VERSION = 1;

static const QDir& autoFilterDir() {
    static const QDir dir{ QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) + "/filters/", "*.conf", 0, QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden };
    return dir;
}

QRegularExpression::PatternOptions getPatternOptions( bool ignoreCase )
{
    QRegularExpression::PatternOptions options =
            QRegularExpression::UseUnicodePropertiesOption
            | QRegularExpression::OptimizeOnFirstUsageOption;

    if ( ignoreCase ) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    return options;
}

Filter::Filter() :
    origin_( -1 ), loaded_offset_( -1 )
{
}

Filter::Filter(const QString& pattern, bool ignoreCase,
            const QString& foreColorName, const QString& backColorName,
            int origin, int loaded_offset ) :
    regexp_( pattern,  getPatternOptions( ignoreCase ) ),
    foreColorName_( foreColorName ),
    backColorName_( backColorName ), enabled_( true ),
    origin_( origin ), loaded_offset_( loaded_offset )
{
    LOG(logDEBUG) << "New Filter, fore: " << foreColorName_.toStdString()
        << " back: " << backColorName_.toStdString();
}

QString Filter::pattern() const
{
    return regexp_.pattern();
}

void Filter::setPattern( const QString& pattern )
{
    regexp_.setPattern( pattern );
}

bool Filter::ignoreCase() const
{
    return regexp_.patternOptions().testFlag(QRegularExpression::CaseInsensitiveOption);
}

void Filter::setIgnoreCase( bool ignoreCase )
{
    regexp_.setPatternOptions( getPatternOptions( ignoreCase ) );
}

const QString& Filter::foreColorName() const
{
    return foreColorName_;
}

void Filter::setForeColor( const QString& foreColorName )
{
    foreColorName_ = foreColorName;
}

const QString& Filter::backColorName() const
{
    return backColorName_;
}

void Filter::setBackColor( const QString& backColorName )
{
    backColorName_ = backColorName;
}

int Filter::origin() const
{
    return origin_;
}

void Filter::setOrigin( int origin )
{
    origin_ = origin;
}

int Filter::loadedOffset() const
{
    return loaded_offset_;
}

void Filter::setLoadedOffset( int offset )
{
    loaded_offset_ = offset;
}

bool Filter::hasMatch( const QString& string ) const
{
    return regexp_.match( string ).hasMatch();
}

//
// Operators for serialization
//

QDataStream& operator<<( QDataStream& out, const Filter& object )
{
    LOG(logDEBUG) << "<<operator from Filter";
    out << object.regexp_;
    out << object.foreColorName_;
    out << object.backColorName_;

    return out;
}

QDataStream& operator>>( QDataStream& in, Filter& object )
{
    LOG(logDEBUG) << ">>operator from Filter";
    in >> object.regexp_;
    in >> object.foreColorName_;
    in >> object.backColorName_;
    object.origin_ = -1;
    object.loaded_offset_ = -1;

    return in;
}


// Default constructor
FilterSet::FilterSet()
{
    qRegisterMetaTypeStreamOperators<Filter>( "Filter" );
    qRegisterMetaTypeStreamOperators<FilterSet>( "FilterSet" );
    qRegisterMetaTypeStreamOperators<FilterSet::FilterList>( "FilterSet::FilterList" );
}

bool FilterSet::matchLine( const QString& line,
        QColor* foreColor, QColor* backColor ) const
{
    for ( QList<Filter>::const_iterator i = filterList.constBegin();
          i != filterList.constEnd(); i++ ) {
        if ( i->hasMatch( line ) ) {
            foreColor->setNamedColor( i->foreColorName() );
            backColor->setNamedColor( i->backColorName() );
            return true;
        }
    }

    return false;
}

//
// Operators for serialization
//

QDataStream& operator<<( QDataStream& out, const FilterSet& object )
{
    LOG(logDEBUG) << "<<operator from FilterSet";
    out << object.filterList;

    return out;
}

QDataStream& operator>>( QDataStream& in, FilterSet& object )
{
    LOG(logDEBUG) << ">>operator from FilterSet";
    in >> object.filterList;

    return in;
}

//
// Persistable virtual functions implementation
//

void Filter::saveToStorage( QSettings& settings, bool origin ) const
{
    LOG(logDEBUG) << "Filter::saveToStorage";

    settings.setValue( "regexp", regexp_.pattern() );
    settings.setValue( "ignore_case", regexp_.patternOptions().testFlag( QRegularExpression::CaseInsensitiveOption ) );
    settings.setValue( "fore_colour", foreColorName_ );
    settings.setValue( "back_colour", backColorName_ );
    if ( origin ) {
        if ( origin_ != -1 ) {
            auto loadedFilterSet = Persistent<LoadedFilterSets>( "loadedFilterSets" );
            settings.setValue( "origin", loadedFilterSet->namedFilterSets[origin_].filename );
        }
        else {
            settings.setValue( "origin", "" );
        }
        settings.setValue( "loaded_offset", loaded_offset_ );
    }
}

void Filter::retrieveFromStorage( QSettings& settings, int origin )
{
    LOG(logDEBUG) << "Filter::retrieveFromStorage";

    regexp_ = QRegularExpression( settings.value( "regexp" ).toString(),
                       getPatternOptions( settings.value( "ignore_case", false ).toBool() ) );
    foreColorName_ = settings.value( "fore_colour" ).toString();
    backColorName_ = settings.value( "back_colour" ).toString();
    loaded_offset_ = settings.value( "loaded_offset" , -1 ).toInt();

    auto origin_file = settings.value( "origin" , "" ).toString();
    FilterSet *set = nullptr;
    if ( ! origin_file.isEmpty() ) {
        auto loadedFilterSet = Persistent<LoadedFilterSets>( "loadedFilterSets" );
        int i = 0;
        bool missing = false;
        for( auto& namedSet : loadedFilterSet->namedFilterSets ) {
            if ( namedSet.filename == origin_file ) {
                origin_ = i;
                set = &namedSet.set;
                missing = namedSet.missing;
                break;
            }
            ++i;
        }
        if ( origin < 0 && ! set ) {
            loadedFilterSet->namedFilterSets.emplace_back( origin_file, true );
            auto& namedSet = loadedFilterSet->namedFilterSets.back();
            origin_ = i;
            set = &namedSet.set;

            if ( QFile::exists( origin_file ) ) {
                LOG(logWARNING) << "Filter file " << origin_file.toStdString() << " not loaded.";
                QSettings missing_settings{ origin_file, QSettings::IniFormat };
                set->retrieveFromStorage( missing_settings );
                namedSet.missing = false;
            }
            else {
                LOG(logERROR) << "Cannot find " << origin_file.toStdString() << "; adding dummy";
                missing = true;
            }
        }
        if ( missing ) {
            assert( set );
            if ( loaded_offset_ == -1 ) {
                //handled below
            }
            else if ( set->size() <= loaded_offset_ ) {
                set->filterList.reserve( loaded_offset_ );
                for ( int i = set->size(); i < loaded_offset_; ++i ) {
                    set->filterList.push_back( { "", false, "black", "black", origin_, i } );
                }
                set->filterList.push_back( *this );
            }
            else {
                set->filterList[loaded_offset_] = *this;
            }
        }
    }
    else {
        origin_ = -1;
    }

    if ( origin >= 0 ) {
        if ( origin_ >= 0 ) {
            LOG(logWARNING) << "Loaded filter " << origin << ":" << regexp_.pattern().toStdString() << " with origin set to " << origin_;
        }
        if ( loaded_offset_ >= 0 ) {
            LOG(logWARNING) << "Loaded filter " << origin << ":" << regexp_.pattern().toStdString() << " with loaded_offset set to " << loaded_offset_;
            origin_ = -1;
            loaded_offset_ = -1;
            return;
        }
        origin_ = origin;
    }
    else if ( origin_ < 0 ) {
        if ( loaded_offset_ >= 0 ) {
            LOG(logWARNING) << "Loaded filter " << settings.fileName().toStdString() << ":" << regexp_.pattern().toStdString() << " with loaded_offset set to " << loaded_offset_;
            loaded_offset_ = -1;
        }
    }
    else {
        if ( loaded_offset_ < 0 || set->size() <= loaded_offset_ ) {
            LOG(logWARNING) << "Loaded filter " << origin_ << ":" << regexp_.pattern().toStdString() << " has invalid offset " << loaded_offset_;
            origin_ = -1;
            loaded_offset_ = -1;
        }
    }
}

void FilterSet::saveToStorage( QSettings& settings, bool origin ) const
{
    LOG(logDEBUG) << "FilterSet::saveToStorage";

    settings.beginGroup( "FilterSet" );
    // Remove everything in case the array is shorter than the previous one
    settings.remove("");
    settings.setValue( "version", FILTERSET_VERSION );
    settings.beginWriteArray( "filters" );
    for (int i = 0; i < filterList.size(); ++i) {
        settings.setArrayIndex(i);
        filterList[i].saveToStorage( settings, origin );
    }
    settings.endArray();
    settings.endGroup();
}

void FilterSet::retrieveFromStorage( QSettings& settings, int origin )
{
    LOG(logDEBUG) << "FilterSet::retrieveFromStorage";

    filterList.clear();

    if ( settings.contains( "FilterSet/version" ) ) {
        settings.beginGroup( "FilterSet" );
        if ( settings.value( "version" ) == FILTERSET_VERSION ) {
            int size = settings.beginReadArray( "filters" );
            filterList.reserve( size );
            for (int i = 0; i < size; ++i) {
                settings.setArrayIndex(i);
                filterList.push_back( {} );
                filterList.back().retrieveFromStorage( settings, origin );
            }
            settings.endArray();
        }
        else {
            LOG(logERROR) << "Unknown version of FilterSet, ignoring it...";
        }
        settings.endGroup();
    }
    else {
        LOG(logWARNING) << "Trying to import legacy (<=0.8.2) filters...";
        FilterSet tmp_filter_set =
            settings.value( "filterSet" ).value<FilterSet>();
        *this = tmp_filter_set;
        LOG(logWARNING) << "...imported filterset: "
            << filterList.count() << " elements";
        // Remove the old key once migration is done
        settings.remove( "filterSet" );
        // And replace it with the new one
        saveToStorage( settings );
        settings.sync();
    }
}

void LoadedFilterSets::saveToStorage( QSettings& settings ) const
{
    LOG(logDEBUG) << "LoadedFilterSets::saveToStorage";

    settings.beginGroup( "LoadedFilterSets" );
    // Remove everything in case the array is shorter than the previous one
    settings.remove("");
    settings.setValue( "version", LOADED_FILTERSET_VERSION );
    settings.beginWriteArray( "sets" );
    int written = 0;
    for ( std::size_t i = 0; i < namedFilterSets.size(); ++i ) {
        auto& filename = namedFilterSets[i].filename;
        QFileInfo fileinfo{ filename };
        if ( fileinfo.absoluteDir() == autoFilterDir().absolutePath() ) {
            bool skip = false;
            for ( auto& nameFilter : autoFilterDir().nameFilters() ) {
                if( QRegExp{ nameFilter, Qt::CaseSensitive, QRegExp::Wildcard }.exactMatch( fileinfo.fileName() ) ) {
                    skip = true;
                    break;
                }
            }
            if ( skip ) {
                continue;
            }
        }
        settings.setArrayIndex( written );
        settings.setValue( "filename", filename );
        namedFilterSets[i].set.saveToStorage( settings, false );
        ++written;
    }
    settings.endArray();
    settings.endGroup();
}

void LoadedFilterSets::retrieveFromStorage( QSettings& settings )
{
    LOG(logDEBUG) << "LoadedFilterSets::retrieveFromStorage";

    namedFilterSets.clear();
    // Write out settings if we reload a filter file.
    bool sync = false;

    settings.beginGroup( "LoadedFilterSets" );
    if ( settings.value( "version" ) == LOADED_FILTERSET_VERSION ) {
        int size = settings.beginReadArray( "sets" );
        namedFilterSets.reserve( size );
        for ( int i = 0; i < size; ++i ) {
            settings.setArrayIndex( i );
            namedFilterSets.emplace_back( settings.value( "filename" ).toString() );
            auto& namedSet = namedFilterSets.back();
            namedSet.set.retrieveFromStorage( settings, i );

            if ( QFile::exists( namedSet.filename ) ) {
                QSettings settings{ namedSet.filename, QSettings::IniFormat };
                FilterSet actual;
                actual.retrieveFromStorage( settings, i );

                if ( actual.filterList != namedSet.set.filterList ) {
                    auto reply = QMessageBox::question( nullptr, "Filter file has changed", "The filter file " + namedSet.filename + " has changed.\nReload?", QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes );
                    if ( reply == QMessageBox::Yes ) {
                        namedSet.set = std::move( actual );
                        sync = true;
                    }
                }
            }
            else {
                namedSet.missing = true;
            }
        }
        settings.endArray();
    }
    else if ( settings.value( "version" ).isValid() ) {
        LOG(logERROR) << "Unknown version of NamedFilterSet, ignoring it...";
    }
    settings.endGroup();

    if ( sync ) {
        saveToStorage( settings );
    }

    QDirIterator dirIter{ autoFilterDir() };
    while( dirIter.hasNext() ) {
        auto file = dirIter.next();
        namedFilterSets.emplace_back( file );
        auto& namedSet = namedFilterSets.back();

        QSettings settings{ file, QSettings::IniFormat };
        namedSet.set.retrieveFromStorage( settings, namedFilterSets.size() - 1 );
    }
}
