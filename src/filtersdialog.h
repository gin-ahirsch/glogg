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

#ifndef FILTERSDIALOG_H
#define FILTERSDIALOG_H

#include <array>
#include <memory>
#include <vector>

#include <QDialog>
#include <QStyledItemDelegate>

#include "filterset.h"
#include "ui_filtersdialog.h"

class FilterListItemDelegate : public QStyledItemDelegate
{
  public:
    virtual ~FilterListItemDelegate() = default;

  protected:
    virtual void paint( QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index ) const override;
};

class FiltersDialog : public QDialog, public Ui::FiltersDialog
{
  Q_OBJECT

  public:
    FiltersDialog( QWidget* parent = 0 );

  signals:
    // Is emitted when new settings must be used
    void optionsChanged();

  private slots:
    void on_addFilterButton_clicked();
    void on_removeFilterButton_clicked();
    void on_buttonBox_clicked( QAbstractButton* button );
    void on_upFilterButton_clicked();
    void on_downFilterButton_clicked();
    void on_saveToFileButton_clicked();
    void on_saveChangesButton_clicked();
    void on_addFilterFile_clicked();
    void on_removeFilterFile_clicked();
    void on_addLoadedFilterButton_clicked();
    void on_removeLoadedFilterButton_clicked();
    // Update the property (pattern, color...) fields from the
    // selected Filter.
    void updatePropertyFields();
    // Update the selected Filter from the values in the property fields.
    void updateFilterProperties();
    // Update the Loaded-tab with values from a loaded filter set.
    void updateLoadedFilterList();

  private:
    // Temporary filtersets modified by the dialog
    // they are copied from the ones in Config()
    FilterSet filterSet;
    LoadedFilterSets loadedFilterSets;

    // stores indices into various data structures for the same filter
    struct FilterRef final
    {
      public:
        FilterRef(int loaded, int filter): loaded_index(loaded), filter_index(filter) {}

        bool isActive() const { return filter_index >= 0; }

        int loaded_index; // index into a local filter array (Persistent( "loadedFilterSets" )->filterSetMap[filename], this->activeFilters[filter.origin()], this->activeFiltersListWidget, this->availableFiltersListWidget)
        int filter_index; // index into this->filterSet
        bool modified = false;
    };

    // This stores the activated filters from filter files.
    // It is indexed by a file's origin and the index of the filter.
    using FilterRefMap = std::vector<std::vector<FilterRef>>;
    FilterRefMap loadedFilterRefs;

    // Swap two filters in filterSet, update the FilterRefs in loadedFilterRefs and the filterListWidget.
    void moveFilter( int from, int to );
    // Find a FilterRef in LoadedFilterRefs.
    FilterRef& findLoadedFilterRef( int origin, int index );
    // Remove a filter from filterSet, LoadedFilterRefs and the corresponding widgets.
    void removeFilter( FilterRef& filterRef );

    QIcon loadedFilterIcon;
    QIcon modifiedFilterIcon;

    // These items all have the same lifetime, so instead of de/allocating them one-by-one we do that in one swoop in this vector.
    std::vector<QListWidgetItem> loadedFilterItems;

    void populateColors();
    void populateFilterList();
    void populateLoadedFilterList();

    std::array<FilterListItemDelegate, 3> filterListItemDelegates;
};

#endif
