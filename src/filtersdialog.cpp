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

#include "log.h"

#include <cassert>
#include <utility>
#include <QFileDialog>
#include <QDir>
#include "configuration.h"
#include "persistentinfo.h"
#include "filterset.h"

#include "filtersdialog.h"

void FilterListItemDelegate::paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const
{
    auto myOption = option;
    myOption.decorationPosition = QStyleOptionViewItem::Right;
    QStyledItemDelegate::paint( painter, myOption, index );
}

static const QString DEFAULT_PATTERN = "New Filter";
static const bool    DEFAULT_IGNORE_CASE = false;
static const QString DEFAULT_FORE_COLOUR = "black";
static const QString DEFAULT_BACK_COLOUR = "white";

// Construct the box, including a copy of the global FilterSet
// to handle ok/cancel/apply
FiltersDialog::FiltersDialog( QWidget* parent ) : QDialog( parent )
{
    setupUi( this );

    // Reload the filter list from disk (in case it has been changed
    // by another glogg instance) and copy it to here.
    auto persistentFilterSet = Persistent<FilterSet>( "filterSet" );
    GetPersistentInfo().retrieve( *persistentFilterSet );
    filterSet = *persistentFilterSet;
    auto persistentLoadedFilterSet = Persistent<LoadedFilterSets>( "loadedFilterSets" );
    GetPersistentInfo().retrieve( *persistentLoadedFilterSet );
    loadedFilterSets = *persistentLoadedFilterSet;

    // scale icons for filterListWidget
    {
        // This dummy must be destroyed before populating the list.
        QListWidgetItem dummy{ filterListWidget };
        int text_height = QFontMetrics{ dummy.font() }.height();
        QSize icon_size{ text_height, text_height };
        loadedFilterIcon = QIcon(":/images/filter_loaded.svg").pixmap( icon_size );
        modifiedFilterIcon = QIcon(":/images/filter_modified.svg").pixmap( icon_size );
    }

    populateColors();
    populateLoadedFilterList();
    populateFilterList();

    // Start with all buttons disabled except 'add'
    removeFilterButton->setEnabled(false);
    upFilterButton->setEnabled(false);
    downFilterButton->setEnabled(false);
    saveToFileButton->setEnabled(false);
    saveChangesButton->setEnabled(false);
    undoChangesButton->setEnabled(false);

    // Default to black on white
    int index = foreColorBox->findText( DEFAULT_FORE_COLOUR );
    foreColorBox->setCurrentIndex( index );
    index = backColorBox->findText( DEFAULT_BACK_COLOUR );
    backColorBox->setCurrentIndex( index );

    connect( filterListWidget, SIGNAL( itemSelectionChanged() ),
            this, SLOT( updatePropertyFields() ) );
    connect( loadedFilterListWidget, SIGNAL( itemSelectionChanged() ),
            this, SLOT( updateLoadedFilterList() ) );
    connect( patternEdit, SIGNAL( textEdited( const QString& ) ),
            this, SLOT( updateFilterProperties() ) );
    connect( ignoreCaseCheckBox, SIGNAL( clicked( bool ) ),
            this, SLOT( updateFilterProperties() ) );
    connect( foreColorBox, SIGNAL( activated( int ) ),
            this, SLOT( updateFilterProperties() ) );
    connect( backColorBox, SIGNAL( activated( int ) ),
            this, SLOT( updateFilterProperties() ) );

    if ( !filterSet.empty() ) {
        filterListWidget->setCurrentItem( filterListWidget->item( 0 ) );
    }

    filterListWidget->setItemDelegate( &filterListItemDelegates[0] );
    loadedFilterListWidget->setItemDelegate( &filterListItemDelegates[1] );
    activeFiltersListWidget->setItemDelegate( &filterListItemDelegates[2] );
}

FiltersDialog::FilterRef& FiltersDialog::findLoadedFilterRef( int origin, int index )
{
    using std::begin;
    using std::end;

    auto& refs = loadedFilterRefs[origin];
    auto iter = begin( refs );

    // Look for the Filter that has the index
    for ( ; iter->filter_index != index; ++iter ) {
        assert ( iter != end( refs ) );
    }

    return *iter;
}

void FiltersDialog::moveFilter( int from, int to )
{
    int from_origin = filterSet[from].origin(),
        to_origin   = filterSet[to  ].origin();

    filterSet.filterList.move( from, to );

    // findLoadedFilterRef does a linear search over the array.
    // We'd only require one pass if from_origin == to_origin, but we always do two for simplicity.
    // Update the entries
    // We need to find the entries before modifying them, otherwise we could set from_ref.filter_index = to and then we'd find the same filter again when doing findLoadedFilterRef( to_origin, to ) since we have two refs with filter_index == to.
    FilterRef* from_ref = nullptr,
             * to_ref   = nullptr;
    if ( from_origin >= 0 ) {
        from_ref = &findLoadedFilterRef( from_origin, from );
    }
    if ( to_origin >= 0 ) {
        to_ref = &findLoadedFilterRef( to_origin, to );
    }
    if ( from_ref ) {
        from_ref->filter_index = to;
    }
    if ( to_ref ) {
        to_ref->filter_index = from;
    }

    QListWidgetItem* item = filterListWidget->takeItem( from );
    filterListWidget->insertItem( to, item );
    filterListWidget->setCurrentRow( to );
}

void FiltersDialog::removeFilter( FilterRef& filterRef )
{
    assert( filterRef.isActive() );

    if ( filterRef.modified ) {
        //TODO: confirm unsaved change?
    }
    filterRef.modified = false;

    // remove from the filterListWidget
    // first we figure out which row to select next, if this is the last one
    int newRow = -1;
    if ( filterListWidget->currentRow() == filterRef.filter_index && filterListWidget->selectionModel()->selectedIndexes().count() == 1 ) {
        int count = filterListWidget->count() - 1;
        if ( filterRef.filter_index < count ) {
            // Select the new item at the same index
            newRow = filterRef.filter_index;
        }
        else {
            newRow = count - 1;
        }
    }
    delete filterListWidget->item( filterRef.filter_index );
    // select a new row if we just deselected it
    if ( newRow > 0 ) {
        filterListWidget->setCurrentRow( newRow, QItemSelectionModel::SelectCurrent );
    }

    // we remove the item at the given index, so all indices that follow must be decremented
    for ( auto& filterList : loadedFilterRefs ) {
        for ( auto& ref : filterList ) {
            if ( ref.filter_index > filterRef.filter_index ) {
                --ref.filter_index;
            }
        }
    }

    // remove from loaded filters lists
    if ( filterRef.loaded_index >= 0 ) {
        int origin = filterSet[filterRef.filter_index].origin();

        bool changes = false;
        for ( auto& ref : loadedFilterRefs[origin] ) {
            changes |= ref.modified;
        }
        if ( !changes ) {
            loadedFilterListWidget->item( origin )->setIcon( {} );
        }
        // remove if the filter list is current selected
        if ( loadedFilterListWidget->currentRow() == origin ) {
            availableFiltersListWidget->item( filterRef.loaded_index )->setHidden( false );
            QListWidgetItem* activeItem = activeFiltersListWidget->item( filterRef.loaded_index );
            activeItem->setIcon( {} );
            activeItem->setHidden( true );

             if ( !changes ) {
                 saveChangesButton->setEnabled( false );
                 undoChangesButton->setEnabled( false );
             }
        }
    }

    filterSet.filterList.removeAt( filterRef.filter_index );

    // this marks the filter as inactive
    filterRef.filter_index = -1;
}

//
// Slots
//

void FiltersDialog::on_addFilterButton_clicked()
{
    LOG(logDEBUG) << "on_addFilterButton_clicked()";

    Filter newFilter = Filter( DEFAULT_PATTERN, DEFAULT_IGNORE_CASE,
            DEFAULT_FORE_COLOUR, DEFAULT_BACK_COLOUR );
    filterSet.filterList << newFilter;

    // Add and select the newly created filter
    filterListWidget->addItem( DEFAULT_PATTERN );
    filterListWidget->setCurrentRow( filterListWidget->count() - 1, QItemSelectionModel::ClearAndSelect );
}

void FiltersDialog::on_removeFilterButton_clicked()
{
    foreach ( const QListWidgetItem* item, filterListWidget->selectedItems() ) {
        int index = filterListWidget->row( item );
        LOG(logDEBUG) << "on_removeFilterButton_clicked() index " << index;

        const Filter& filter = filterSet[index];
        if ( filter.origin() < 0 ) {
            FilterRef dummy{ -1, index };
            removeFilter( dummy );
        }
        else {
            removeFilter( findLoadedFilterRef( filter.origin(), index ) );
        }
    }
    updatePropertyFields();
}

void FiltersDialog::on_upFilterButton_clicked()
{
    int index = filterListWidget->currentRow();
    LOG(logDEBUG) << "on_upFilterButton_clicked() index " << index;

    if ( index > 0 ) {
        moveFilter( index, index - 1 );
    }
}

void FiltersDialog::on_downFilterButton_clicked()
{
    int index = filterListWidget->currentRow();
    LOG(logDEBUG) << "on_downFilterButton_clicked() index " << index;

    if ( ( index >= 0 ) && ( index < ( filterListWidget->count() - 1 ) ) ) {
        moveFilter( index, index + 1 );
    }
}

void FiltersDialog::on_buttonBox_clicked( QAbstractButton* button )
{
    LOG(logDEBUG) << "on_buttonBox_clicked()";

    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if (   ( role == QDialogButtonBox::AcceptRole )
        || ( role == QDialogButtonBox::ApplyRole ) ) {
        // persist to disk
        auto persistentLoadedFilterSet = Persistent<LoadedFilterSets>( "loadedFilterSets" );
        *persistentLoadedFilterSet = std::move( loadedFilterSets );
        GetPersistentInfo().save( *persistentLoadedFilterSet );
        auto persistentFilterSet = Persistent<FilterSet>( "filterSet" );
        *persistentFilterSet = std::move( filterSet );
        GetPersistentInfo().save( *persistentFilterSet );
        emit optionsChanged();
    }

    if ( role == QDialogButtonBox::AcceptRole )
        accept();
    else if ( role == QDialogButtonBox::RejectRole )
        reject();
}

static const int FILTERFILE_VERSION = 1;

void FiltersDialog::on_saveToFileButton_clicked()
{
    LOG(logDEBUG) << "on_saveToFileButton_clicked()";

    auto selectedItems = filterListWidget->selectedItems();

    QString filename = QFileDialog::getSaveFileName(this,
            tr("Save Filters"), QDir::home().path(), tr("Filter files (*.conf)"));

    QSettings settings{ filename, QSettings::IniFormat };

    settings.remove("");
    settings.setValue( "version", FILTERFILE_VERSION );

    settings.beginGroup( "FilterSet" );
    settings.setValue( "version", FilterSet::FILTERSET_VERSION );

    settings.beginWriteArray( "filters" );
    for (int i = 0; i < selectedItems.size(); ++i) {
        auto selectedItem = selectedItems.at( i );
        int selectedRow = filterListWidget->row( selectedItem );

        settings.setArrayIndex( i );
        filterSet[selectedRow].saveToStorage( settings, false );
    }
    settings.endArray();

    settings.endGroup();
}

void FiltersDialog::on_saveChangesButton_clicked()
{
    LOG(logDEBUG) << "on_saveChangesButton_clicked()";

    const int row = loadedFilterListWidget->currentRow();
    if ( row < 0 ) {
        return;
    }

    auto& filterRefs = loadedFilterRefs[row];
    auto& namedFilterSet = loadedFilterSets[row];

    for ( auto& filterRef : filterRefs ) {
        if ( filterRef.isActive() ) {
            auto new_item = filterListWidget->item( filterRef.filter_index );
            auto old_active_item = activeFiltersListWidget->item( filterRef.loaded_index ),
                 old_available_item = availableFiltersListWidget->item( filterRef.loaded_index );
            Filter& old_filter = namedFilterSet.set[filterRef.loaded_index];
            Filter& new_filter = filterSet[filterRef.filter_index];

            new_item->setIcon( loadedFilterIcon );
            old_active_item->setIcon( {} );
            old_available_item->setIcon( {} );
            old_active_item->setText( new_filter.pattern() );
            old_available_item->setText( new_filter.pattern() );
            old_filter.setPattern( new_filter.pattern() );
            old_active_item->setForeground( new_item->foreground() );
            old_available_item->setForeground( new_item->foreground() );
            old_filter.setForeColor( new_filter.foreColorName() );
            old_active_item->setBackground( new_item->background() );
            old_available_item->setBackground( new_item->background() );
            old_filter.setBackColor( new_filter.backColorName() );

            filterRef.modified = false;
        }
    }

    QSettings settings{ loadedFilterListWidget->currentItem()->text(), QSettings::IniFormat };

    settings.remove("");
    settings.setValue( "version", FILTERFILE_VERSION );

    filterSet.saveToStorage( settings, false );

    for ( int i = 0; i < activeFiltersListWidget->count(); ++i ) {
        activeFiltersListWidget->item( i )->setIcon( {} );
    }

    loadedFilterListWidget->currentItem()->setIcon( {} );
    saveChangesButton->setEnabled( false );
    undoChangesButton->setEnabled( false );
}

void FiltersDialog::on_undoChangesButton_clicked()
{
    LOG(logDEBUG) << "on_undoChangesButton_clicked()";

    const int row = loadedFilterListWidget->currentRow();
    if ( row < 0 ) {
        return;
    }

    auto& filterRefs = loadedFilterRefs[row];
    auto& namedFilterSet = loadedFilterSets[row];

    for ( auto& filterRef : filterRefs ) {
        if ( filterRef.modified ) {
            auto new_item = filterListWidget->item( filterRef.filter_index );
            auto old_item = availableFiltersListWidget->item( filterRef.loaded_index );
            Filter& old_filter = namedFilterSet.set[filterRef.loaded_index];
            Filter& new_filter = filterSet[filterRef.filter_index];

            new_item->setIcon( loadedFilterIcon );
            old_item->setIcon( {} );
            new_item->setText( old_filter.pattern() );
            new_filter.setPattern( old_filter.pattern() );
            new_item->setForeground( old_item->foreground() );
            new_filter.setForeColor( old_filter.foreColorName() );
            new_item->setBackground( old_item->background() );
            new_filter.setBackColor( old_filter.backColorName() );

            filterRef.modified = false;
        }
    }

    updatePropertyFields();

    for ( int i = 0; i < activeFiltersListWidget->count(); ++i ) {
        activeFiltersListWidget->item( i )->setIcon( {} );
    }

    loadedFilterListWidget->currentItem()->setIcon( {} );
    saveChangesButton->setEnabled( false );
    undoChangesButton->setEnabled( false );
}

void FiltersDialog::on_addFilterFile_clicked()
{
    LOG(logDEBUG) << "on_addFilterFile_clicked()";

    QString filename = QFileDialog::getOpenFileName(this,
            tr("Load Filters"), QDir::home().path(), tr("Filter files (*.conf)"));

    QSettings settings{ filename, QSettings::IniFormat };
    if ( settings.contains( "version" ) ) {
        if ( settings.value( "version" ) == FILTERFILE_VERSION ) {
            auto& namedSets = loadedFilterSets.namedFilterSets;
            int new_origin = namedSets.size();
            assert( loadedFilterListWidget->count() == new_origin );
            assert( static_cast<int>( loadedFilterRefs.size() ) == new_origin );
            namedSets.emplace_back( filename );
            auto& namedSet = namedSets.back();

            FilterSet& set = namedSet.set;
            set.retrieveFromStorage( settings, new_origin );

            loadedFilterListWidget->addItem( new QListWidgetItem( filename ) );
            loadedFilterRefs.push_back( {} );
            auto& filterRefs = loadedFilterRefs.back();
            filterRefs.reserve( set.size() );

            for( int i = 0; i < set.size(); ++i ) {
                filterRefs.push_back( { i, -1 } );
            }

            loadedFilterListWidget->setCurrentRow( new_origin );
            updateLoadedFilterList();
        }
        else {
            //FIXME: popup
            LOG(logERROR) << "Unknown version of FilterFile, ignoring it...";
        }
    }
    else {
        //FIXME: popup
        LOG(logERROR) << "Invalid FilterFile format, ignoring it...";
    }
}

void FiltersDialog::on_removeFilterFile_clicked()
{
    using std::begin;
    using std::next;

    LOG(logDEBUG) << "on_removeFilterFile_clicked()";

    const int row = loadedFilterListWidget->currentRow();
    if ( row < 0 ) {
        return;
    }
    auto& filterRefs = loadedFilterRefs[row];

    for ( auto& filterRef : filterRefs ) {
        if ( filterRef.isActive() ) {
            removeFilter( filterRef );
        }
    }

    loadedFilterListWidget->setCurrentRow( -1 );
    delete loadedFilterListWidget->item( row );
    if ( loadedFilterListWidget->count() > row ) {
        loadedFilterListWidget->setCurrentRow( row );
    }
    else {
        loadedFilterListWidget->setCurrentRow( loadedFilterListWidget->count() - 1 );
    }
    if ( loadedFilterListWidget->currentRow() >= 0 ) {
        updateLoadedFilterList();
    }

    loadedFilterSets.namedFilterSets.erase( next( begin( loadedFilterSets.namedFilterSets ), row ) );
    loadedFilterRefs.erase( next( begin( loadedFilterRefs ), row ) );
    updatePropertyFields();
}

void FiltersDialog::updateLoadedFilterList()
{
    LOG(logDEBUG) << "updateLoadedFilterList()";

    const int origin = loadedFilterListWidget->currentRow();

    if ( origin < 0 ) {
        return;
    }

    const auto& namedFilterSet = loadedFilterSets[origin];
    const auto& set = namedFilterSet.set;

    loadedFilterItems.clear();
    assert( availableFiltersListWidget->count() == 0 );
    assert( activeFiltersListWidget->count() == 0 );

    const auto& filterRefs = loadedFilterRefs[origin];
    assert( set.size() == static_cast<int>( filterRefs.size() ) );

    loadedFilterItems.reserve( filterRefs.size() * 2 ); // *2 since we have two lists
    auto* loadedFilterItemsData = loadedFilterItems.data();
    bool changes = false;
    for( std::size_t i = 0; i < filterRefs.size(); ++i ) {
        const Filter& filter = set[i];
        const FilterRef& filterRef = filterRefs[i];

        loadedFilterItems.emplace_back( filter.pattern() );
        QListWidgetItem* new_item = &loadedFilterItems.back();
        new_item->setForeground( QBrush( QColor( filter.foreColorName() ) ) );
        new_item->setBackground( QBrush( QColor( filter.backColorName() ) ) );
        availableFiltersListWidget->addItem( new_item );
        new_item->setHidden( filterRef.isActive() );

        loadedFilterItems.emplace_back( *new_item );
        new_item = &loadedFilterItems.back();
        if ( filterRef.modified ) {
            new_item->setIcon( modifiedFilterIcon );
            changes = true;
        }
        activeFiltersListWidget->addItem( new_item );
        new_item->setHidden( !filterRef.isActive() );
    }
    // we really shouldn't have reallocated, since we reserve()d
    assert( loadedFilterItemsData == loadedFilterItems.data() );

    saveChangesButton->setEnabled( changes );
    undoChangesButton->setEnabled( changes );
}

void FiltersDialog::on_addLoadedFilterButton_clicked()
{
    const int origin = loadedFilterListWidget->currentRow();
    assert ( origin >= 0 );
    auto& refs = loadedFilterRefs[origin];
    const auto& set = loadedFilterSets[origin].set;

    foreach ( QListWidgetItem* item, availableFiltersListWidget->selectedItems() ) {
        int row = availableFiltersListWidget->row( item );
        LOG(logDEBUG) << "on_addLoadedFilterButton_clicked() index " << row;

        auto filterItem = item->clone();
        filterItem->setIcon( loadedFilterIcon );
        filterListWidget->addItem( filterItem );

        item->setHidden( true );
        activeFiltersListWidget->item( row )->setHidden( false );

        filterSet.filterList.append( set[row] );
        filterSet.back().setLoadedOffset( row );

        refs[row].filter_index = filterSet.size() - 1;
    }

    availableFiltersListWidget->clearSelection();

    if ( filterListWidget->selectionModel()->selectedIndexes().count() == 1 ) {
        int selectedRow = filterListWidget->currentRow();
        upFilterButton->setEnabled( selectedRow > 0 );
        downFilterButton->setEnabled( selectedRow < ( filterListWidget->count() - 1 ) );
    }
}

void FiltersDialog::on_removeLoadedFilterButton_clicked()
{
    const int origin = loadedFilterListWidget->currentRow();
    assert ( origin >= 0 );
    auto& refs = loadedFilterRefs[origin];

    foreach ( const QListWidgetItem* item, activeFiltersListWidget->selectedItems() ) {
        int index = activeFiltersListWidget->row( item );
        LOG(logDEBUG) << "on_addLoadedFilterButton_clicked() index " << index;

        removeFilter( refs[index] );
    }
    updatePropertyFields();
}

void FiltersDialog::updatePropertyFields()
{
    const auto& selectedIndexes = filterListWidget->selectionModel()->selectedIndexes();

    if ( selectedIndexes.count() == 1 ) {
        int selectedRow = selectedIndexes.first().row();

        LOG(logDEBUG) << "updatePropertyFields(), row = " << selectedRow;

        const Filter& currentFilter = filterSet[selectedRow];

        patternEdit->setText( currentFilter.pattern() );
        patternEdit->setEnabled( true );

        ignoreCaseCheckBox->setChecked( currentFilter.ignoreCase() );
        ignoreCaseCheckBox->setEnabled( true );

        int index = foreColorBox->findText( currentFilter.foreColorName() );
        if ( index != -1 ) {
            LOG(logDEBUG) << "fore index = " << index;
            foreColorBox->setCurrentIndex( index );
            foreColorBox->setEnabled( true );
        }
        index = backColorBox->findText( currentFilter.backColorName() );
        if ( index != -1 ) {
            LOG(logDEBUG) << "back index = " << index;
            backColorBox->setCurrentIndex( index );
            backColorBox->setEnabled( true );
        }

        // Enable the buttons if needed
        removeFilterButton->setEnabled( true );
        upFilterButton->setEnabled( selectedRow > 0 );
        downFilterButton->setEnabled(
                selectedRow < ( filterListWidget->count() - 1 ) );
        saveToFileButton->setEnabled( true );
    }
    else {
        LOG(logDEBUG) << "updatePropertyFields(), row = " << ( selectedIndexes.count() > 1 ? '*' : 'X' );

        // Nothing or multiple are selected, greys the buttons
        patternEdit->clear();
        patternEdit->setEnabled( false );

        int index = foreColorBox->findText( DEFAULT_FORE_COLOUR );
        foreColorBox->setCurrentIndex( index );
        foreColorBox->setEnabled( false );
        index = backColorBox->findText( DEFAULT_BACK_COLOUR );

        backColorBox->setCurrentIndex( index );
        backColorBox->setEnabled( false );

        ignoreCaseCheckBox->setChecked( DEFAULT_IGNORE_CASE );
        ignoreCaseCheckBox->setEnabled( false );
        upFilterButton->setEnabled( false );
        downFilterButton->setEnabled( false );
        saveToFileButton->setEnabled( selectedIndexes.count() != 0 );

        if ( selectedIndexes.count() == 0 ) {
            removeFilterButton->setEnabled( false );
        }
    }
}

void FiltersDialog::updateFilterProperties()
{
    LOG(logDEBUG) << "updateFilterProperties()";

    // If a row is selected
    if ( filterListWidget->selectionModel()->selectedIndexes().count() == 1 ) {
        int selectedRow = filterListWidget->currentRow();
        Filter& currentFilter = filterSet[selectedRow];

        // Update the internal data
        currentFilter.setPattern( patternEdit->text() );
        currentFilter.setIgnoreCase( ignoreCaseCheckBox->isChecked() );
        currentFilter.setForeColor( foreColorBox->currentText() );
        currentFilter.setBackColor( backColorBox->currentText() );

        int origin = currentFilter.origin();
        if ( origin >= 0 ) {
            auto& ref = findLoadedFilterRef( origin, selectedRow );
            int loadedIndex = ref.loaded_index;
            const Filter& loadedFilter = loadedFilterSets[origin].set[loadedIndex];

            QListWidgetItem* loadedActiveFilterItem = nullptr;
            if ( loadedFilterListWidget->currentRow() == origin ) {
                loadedActiveFilterItem = activeFiltersListWidget->item( loadedIndex );
            }

            const QIcon* icon = &loadedFilterIcon;
            if ( currentFilter != loadedFilter ) {
                ref.modified = true;

                icon = &modifiedFilterIcon;
                if ( loadedActiveFilterItem ) {
                    loadedActiveFilterItem->setIcon( *icon );

                    saveChangesButton->setEnabled( true );
                    undoChangesButton->setEnabled( true );
                }
                loadedFilterListWidget->item( origin )->setIcon( modifiedFilterIcon );
            }
            else {
                bool changes = false;
                for ( auto& ref : loadedFilterRefs[origin] ) {
                    changes |= ref.modified;
                }
                if ( !changes ) {
                    loadedFilterListWidget->item( origin )->setIcon( {} );
                }

                if ( loadedActiveFilterItem ) {
                    ref.modified = false;

                    loadedActiveFilterItem->setIcon( {} );

                    if ( !changes ) {
                        saveChangesButton->setEnabled( false );
                        undoChangesButton->setEnabled( false );
                    }
                }
            }
            filterListWidget->currentItem()->setIcon( *icon );
        }

        // Update the entry in the filterList widget
        filterListWidget->currentItem()->setText( patternEdit->text() );
        filterListWidget->currentItem()->setForeground(
                QBrush( QColor( currentFilter.foreColorName() ) ) );
        filterListWidget->currentItem()->setBackground(
                QBrush( QColor( currentFilter.backColorName() ) ) );
    }
}

//
// Private functions
//

// Fills the color selection combo boxes
void FiltersDialog::populateColors()
{
    const QStringList colorNames = QStringList()
        // Basic 16 HTML colors (minus greys):
        << "black"
        << "white"
        << "maroon"
        << "red"
        << "purple"
        << "fuchsia"
        << "green"
        << "lime"
        << "olive"
        << "yellow"
        << "navy"
        << "blue"
        << "teal"
        << "aqua"
        // Greys
        << "gainsboro"
        << "lightgrey"
        << "silver"
        << "darkgrey"
        << "grey"
        << "dimgrey"
        // Reds
        << "tomato"
        << "orangered"
        << "orange"
        << "crimson"
        << "darkred"
        // Greens
        << "greenyellow"
        << "lightgreen"
        << "darkgreen"
        << "lightseagreen"
        // Blues
        << "lightcyan"
        << "darkturquoise"
        << "steelblue"
        << "lightblue"
        << "royalblue"
        << "darkblue"
        << "midnightblue"
        // Browns
        << "bisque"
        << "tan"
        << "sandybrown"
        << "chocolate";

    for ( QStringList::const_iterator i = colorNames.constBegin();
            i != colorNames.constEnd(); ++i ) {
        QPixmap solidPixmap( 20, 10 );
        solidPixmap.fill( QColor( *i ) );
        QIcon solidIcon { solidPixmap };

        foreColorBox->addItem( solidIcon, *i );
        backColorBox->addItem( solidIcon, *i );
    }
}

void FiltersDialog::populateFilterList()
{
    filterListWidget->clear();

    for ( const Filter& filter : filterSet ) {
        QListWidgetItem* new_item = new QListWidgetItem( filter.pattern() );
        // new_item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled );
        new_item->setForeground( QBrush( QColor( filter.foreColorName() ) ) );
        new_item->setBackground( QBrush( QColor( filter.backColorName() ) ) );
        filterListWidget->addItem( new_item );

        if ( filter.origin() >= 0 ) {
            if ( static_cast<std::size_t>( filter.origin() ) >= loadedFilterSets.size() ) {
                LOG(logERROR) << "populateFilterList(): filter origin " << filter.origin() << " does not refer to a valid FilterSet";
                continue;
            }

            auto& namedFilterSet = loadedFilterSets[filter.origin()];
            auto& list = namedFilterSet.set.filterList;
            if ( filter.loadedOffset() >= list.size() ) {
                LOG(logERROR) << "populateFilterList(): filter offset " << filter.loadedOffset() << " does not refer to a valid Filter in " << namedFilterSet.filename.toStdString();
                continue;
            }

            auto& refs = loadedFilterRefs[filter.origin()];
            assert( refs.size() == static_cast<std::size_t>( list.size() ) );
            auto& ref = refs[filter.loadedOffset()];

            ref.filter_index = filterListWidget->count() - 1;

            const QIcon* icon = &loadedFilterIcon;
            if( list[filter.loadedOffset()] != filter ) {
                ref.modified = true;
                icon = &modifiedFilterIcon;
                // We could use a QBitArray to track origins with changed filters, but setIcon() is probably not too expensive.
                loadedFilterListWidget->item( filter.origin() )->setIcon( modifiedFilterIcon );
            }
            new_item->setIcon( *icon );
        }
    }
}

void FiltersDialog::populateLoadedFilterList()
{
    loadedFilterListWidget->clear();
    for ( const NamedFilterSet& set : loadedFilterSets ) {
        new QListWidgetItem( set.filename, loadedFilterListWidget );
        loadedFilterRefs.emplace_back();
        auto& refs = loadedFilterRefs.back();
        refs.reserve(set.set.size());
        for ( int i = 0; i < set.set.size(); ++i ) {
            refs.emplace_back( i, -1 );
        }
    }
}
