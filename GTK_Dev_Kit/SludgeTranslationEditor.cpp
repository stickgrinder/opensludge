/* -*- Mode: C++; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * SludgeTranslationEditor.cpp - Part of the SLUDGE Translation Editor (GTK+ version)
 *
 * Copyright (C) 2010 Tobias Hansen <tobias.han@gmx.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <gtk/gtk.h>
#include <gtk/gtkgl.h>
#include <gdk/gdk.h>
#include <gdk/gdkkeysyms.h>
#include <glib-object.h>
#include <glib.h>
#include <glib/gstdio.h>

#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <GL/glu.h>
#include <GL/glext.h>

#include <errno.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>

#include "translator.h"
#include "moreio.h"
#include "Common.h"

#include "SludgeTranslationEditor.h"
#include "TranslationEditorMain.h"

SludgeTranslationEditor::SludgeTranslationEditor()
 : SludgeApplication(joinTwoStrings(DATADIR, "TranslationEditor.glade"), "flags", "translationeditor")
{
	if (!initSuccess) return;

	firstTransLine = NULL;
	langName = NULL;

	comboBox = NULL;
	listStore = NULL;
	filterModel = NULL;
	sortModel = NULL;
	selection = NULL;
	originalColumn = NULL;
	translationColumn = NULL;

	theIdAdjustment = GTK_ADJUSTMENT (gtk_builder_get_object(theXml, "id_adjustment"));
	theLanguageEntry = GTK_ENTRY (gtk_builder_get_object(theXml, "language_name"));
	theSearchEntry = GTK_ENTRY (gtk_builder_get_object(theXml, "search_entry"));
	theOriginalTextBuffer = GTK_TEXT_BUFFER (gtk_builder_get_object(theXml, "original_textbuffer"));
	theTranslationTextBuffer = GTK_TEXT_BUFFER (gtk_builder_get_object(theXml, "translation_textbuffer"));

	init(TRUE);
}

// Concrete methods for SludgeApplication:

gboolean SludgeTranslationEditor::init(gboolean calledFromConstructor) 
{
	currentFilename[0] = 0;
	sprintf(currentShortname, "%s", getUntitledFilename());

	badLangName = FALSE;

    return FALSE;
}

const char * SludgeTranslationEditor::getWindowTitle()
{
	return "SLUDGE Translation Editor";
}

const char * SludgeTranslationEditor::getFilterName()
{
	return "SLUDGE Translation Files (*.tra)";
}

const char * SludgeTranslationEditor::getFilterPattern()
{
	return "*.[tT][rR][aA]";
}

const char * SludgeTranslationEditor::getUntitledFilename()
{
	return "Untitled Translation.tra";
}

gboolean SludgeTranslationEditor::saveFile(char *filename)
{
	return saveTranslationFile (filename, firstTransLine, gtk_entry_get_text(theLanguageEntry), (unsigned int)gtk_adjustment_get_value(theIdAdjustment));
}

gboolean SludgeTranslationEditor::loadFile(char *filename)
{
	unsigned int langID;
	if (loadTranslationFile(filename, &firstTransLine, &langName, &langID)) {
		gtk_adjustment_set_value(theIdAdjustment, (double)langID);
		if (langName) {
			if (g_utf8_validate(langName, -1, NULL)) {
				badLangName = FALSE;
				gtk_entry_set_text(theLanguageEntry, langName);
			} else {
				badLangName = TRUE;
			}
			deleteString(langName);
			langName = NULL;
		}
		return TRUE;
	} else {
		return FALSE;
	}
}


void SludgeTranslationEditor::postOpen()
{
	listChanged();
}

void SludgeTranslationEditor::postNew()
{
	newFile(&firstTransLine);
	gtk_adjustment_set_value(theIdAdjustment, 0.);
	gtk_entry_set_text(theLanguageEntry, "");
	listChanged();
}


void SludgeTranslationEditor::listChanged()
{
	char *listitem, *stringPtr;
	GtkTreeIter iter;
	int stringId = 0;

	gtk_list_store_clear(listStore);
	gboolean badChars = FALSE;
	int type = gtk_combo_box_get_active(comboBox);
	struct transLine * line = firstTransLine;
	while (line) {
		gtk_list_store_append(listStore, &iter);

		for (int k = 0; k < 2; k++) {
			stringPtr = k?line->transTo:line->transFrom;
			if (!stringPtr) continue;

			if (g_utf8_validate(stringPtr, -1, NULL)) {
				gtk_list_store_set(listStore, &iter, 2*k, stringPtr, -1);
			} else {
				badChars = TRUE;
			}
		}
		gtk_list_store_set(listStore, &iter, COLUMN_TRANSLATE, line?line->type != TYPE_NONE:FALSE, -1);
		gtk_list_store_set(listStore, &iter, COLUMN_ID, stringId, -1);
		gtk_list_store_set(listStore, &iter, COLUMN_VISIBLE, TRUE, -1);

		stringId++;
		line = line->next;
	}

	if (badChars || badLangName) {
		on_new();
		errorBox("Invalid characters!", "The translation file or SLUDGE scripts in the project contain characters that are encoded in something else than UTF-8. Starting with SLUDGE 2.2 all SLUDGE scripts have to be UTF-8 encoded. Please convert your files.");
	}
}

// Callbacks:

void SludgeTranslationEditor::on_combobox_realize(GtkComboBox *theComboBox)
{
	GtkListStore *comboStore;
	GtkCellRenderer *cellRenderer;
	GtkTreeIter iter, iter1;

	comboBox = theComboBox;

	comboStore = gtk_list_store_new(1, G_TYPE_STRING);
	gtk_list_store_append(comboStore, &iter1);
	gtk_list_store_set(comboStore, &iter1, 0, "Show all strings", -1);
	gtk_list_store_append(comboStore, &iter);
	gtk_list_store_set(comboStore, &iter, 0, "Show strings that are missing translations", -1);
	gtk_list_store_append(comboStore, &iter);
	gtk_list_store_set(comboStore, &iter, 0, "Show translated strings", -1);
	gtk_list_store_append(comboStore, &iter);
	gtk_list_store_set(comboStore, &iter, 0, "Show strings that don't need translation", -1);
	gtk_combo_box_set_model(theComboBox, GTK_TREE_MODEL(comboStore));

	cellRenderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start( GTK_CELL_LAYOUT( theComboBox ), cellRenderer, TRUE );
	gtk_cell_layout_set_attributes( GTK_CELL_LAYOUT( theComboBox ), cellRenderer, "text", 0, NULL );

	gtk_combo_box_set_active_iter(theComboBox, &iter1);
}

void SludgeTranslationEditor::on_combobox_changed(GtkComboBox *theComboBox)
{
	if (listStore) {
		int type = gtk_combo_box_get_active(theComboBox);

		GtkTreeIter iter;
		if (!gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL(listStore), &iter, "0")) {
			return;
		}
		struct transLine * line = firstTransLine;
		while (line) {
			if (	(type == 0) ||
					(type == 1 && line->type == TYPE_NEW) ||
					(type == 2 && line->type == TYPE_TRANS) ||
					(type == 3 && line->type == TYPE_NONE)	)
			{
				gtk_list_store_set(listStore, &iter, COLUMN_VISIBLE, TRUE, -1);
			} else {
				gtk_list_store_set(listStore, &iter, COLUMN_VISIBLE, FALSE, -1);
			}
			gtk_tree_model_iter_next(GTK_TREE_MODEL(listStore), &iter);
			line = line->next;
		}
	}
}

void SludgeTranslationEditor::on_treeview_realize(GtkTreeView *theTreeView)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	char caption[100];

	listStore = gtk_list_store_new(N_COLUMNS, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_INT, G_TYPE_BOOLEAN);
	filterModel = gtk_tree_model_filter_new(GTK_TREE_MODEL(listStore), NULL);
	gtk_tree_model_filter_set_visible_column(GTK_TREE_MODEL_FILTER(filterModel), COLUMN_VISIBLE);
	sortModel = gtk_tree_model_sort_new_with_model(filterModel);
	gtk_tree_view_set_model(theTreeView, sortModel);

	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE (sortModel),
		                                    COLUMN_ID, GTK_SORT_ASCENDING);

	selection = gtk_tree_view_get_selection(theTreeView);
	g_signal_connect(G_OBJECT (selection), "changed",
				      G_CALLBACK (on_tree_selection_changed_cb),
				      NULL);

	sprintf(caption, "Original text");
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	originalColumn = gtk_tree_view_column_new_with_attributes(caption,
		                                               renderer,
		                                               "text", COLUMN_ORIGINAL,
		                                               NULL);
	gtk_tree_view_column_set_clickable(originalColumn, TRUE);
	g_signal_connect(G_OBJECT (originalColumn), "clicked",
				      G_CALLBACK (on_sort_original_clicked_cb),
				      NULL);
	gtk_tree_view_column_set_expand(originalColumn, TRUE);
	gtk_tree_view_append_column(theTreeView, originalColumn);

	sprintf(caption, "Translate?");
	renderer = gtk_cell_renderer_toggle_new();
	g_signal_connect(G_OBJECT (renderer), "toggled",
				      G_CALLBACK (on_translate_toggled_cb),
				      NULL);
	column = gtk_tree_view_column_new_with_attributes(caption,
		                                               renderer,
	                                                   "active", COLUMN_TRANSLATE,
		                                               NULL);
	gtk_tree_view_column_set_expand(column, FALSE);
	gtk_tree_view_append_column(theTreeView, column);

	sprintf(caption, "Translation");
	renderer = gtk_cell_renderer_text_new();
	g_object_set(renderer, "ellipsize", PANGO_ELLIPSIZE_END, NULL);
	g_object_set(renderer, "editable", TRUE, NULL);
	g_signal_connect(G_OBJECT (renderer), "edited",
				      G_CALLBACK (on_translation_edited_cb),
				      NULL);
	translationColumn = gtk_tree_view_column_new_with_attributes(caption,
		                                               renderer,
		                                               "text", COLUMN_TRANSLATION,
		                                               NULL);
	gtk_tree_view_column_set_clickable(translationColumn, TRUE);
	g_signal_connect(G_OBJECT (translationColumn), "clicked",
				      G_CALLBACK (on_sort_translation_clicked_cb),
				      NULL);
	gtk_tree_view_column_set_expand(translationColumn, TRUE);
	gtk_tree_view_append_column(theTreeView, translationColumn);

	gtk_tree_view_set_search_equal_func(theTreeView, searchEqualFunc_cb, NULL, NULL);
	gtk_tree_view_set_search_entry(theTreeView, theSearchEntry);
}

void SludgeTranslationEditor::on_tree_selection_changed(GtkTreeSelection *theSelection)
{
	GtkTreeIter iter;
	GtkTreeModel *model;
	gchar *tx;
	if (gtk_tree_selection_get_selected(theSelection, &model, &iter)) {
		gtk_tree_model_get(model, &iter, COLUMN_ORIGINAL, &tx, -1);
		gtk_text_buffer_set_text(theOriginalTextBuffer, tx?tx:"", -1);
		g_free(tx);
		tx = NULL;
		gtk_tree_model_get(model, &iter, COLUMN_TRANSLATION, &tx, -1);
		gtk_text_buffer_set_text(theTranslationTextBuffer, tx?tx:"", -1);
		g_free(tx);
	} else {
		gtk_text_buffer_set_text(theOriginalTextBuffer, "", 0);
		gtk_text_buffer_set_text(theTranslationTextBuffer, "", 0);
	}
}

void SludgeTranslationEditor::on_column_changed(int column, GtkCellRenderer *theCell_renderer, gchar *thePath, gchar *theNewText)
{
	GtkTreePath *sortedPath, *filterPath, *listStorePath;
	GtkTreeIter iter;
	gchar *indexStr;
	int index;

	sortedPath = gtk_tree_path_new_from_string(thePath);
	filterPath = gtk_tree_model_sort_convert_path_to_child_path(GTK_TREE_MODEL_SORT(sortModel), sortedPath);
	listStorePath = gtk_tree_model_filter_convert_path_to_child_path(GTK_TREE_MODEL_FILTER(filterModel), filterPath);
	gtk_tree_model_get_iter(GTK_TREE_MODEL(listStore), &iter, listStorePath);

	indexStr = gtk_tree_path_to_string(listStorePath);
	index = atoi(indexStr);
	g_free(indexStr);

	struct transLine * line = firstTransLine;
	for (int j = 0; j < index; j++) {
		line = line->next;
	}

	switch (column) {
		case COLUMN_TRANSLATE:
		{
			if (gtk_cell_renderer_toggle_get_active(GTK_CELL_RENDERER_TOGGLE(theCell_renderer))) {
				if (line->transTo) {
					if (!askAQuestion("Delete translation?", 
					"You disabled translation of a string that is already translated. Do you want to delete the existing translation?")) {
						return;
					}
					deleteString(line->transTo);
					line->transTo = NULL;
					gtk_list_store_set(listStore, &iter, COLUMN_TRANSLATION, "", -1);
				}
				line->type = TYPE_NONE;
			} else {
				if (line->transTo)
					line->type = TYPE_TRANS;
				else
					line->type = TYPE_NEW;
			}
			setFileChanged();
			gtk_list_store_set(listStore, &iter, COLUMN_TRANSLATE, line?line->type != TYPE_NONE:FALSE, -1);
			break;
		}
		case COLUMN_TRANSLATION:
		{
			if (line->type == TYPE_NONE) {
				if (!strlen(theNewText) || !askAQuestion("Enable translation?", 
					"You entered a translation for a string that is not supposed to be translated. Do you want to enable translation for this string?")) {
					return;
				} else {
					gtk_list_store_set(listStore, &iter, COLUMN_TRANSLATE, TRUE, -1);
				}
			}

			if (line->transTo) {
				if (! strcmp(line->transTo, theNewText)) {
					return;
				} else {
					setFileChanged();
				}
				deleteString(line->transTo);
				line->transTo = NULL;
			} else if (! strlen(theNewText)) {
				return;
			} else {
				setFileChanged();
			}
			line->transTo = copyString(theNewText);
			if (!strlen(line->transTo)) {
				if (line->type != TYPE_NONE)
					line->type = TYPE_NEW;
				deleteString(line->transTo);
				line->transTo = NULL;
			} else {
				line->type = TYPE_TRANS;
			}
			gtk_list_store_set(listStore, &iter, COLUMN_TRANSLATION, theNewText, -1);
			on_tree_selection_changed(selection);
			break;
		}
		default:
			break;
	}

	int type = gtk_combo_box_get_active(comboBox);
	if (	(type == 0) ||
			(type == 1 && line->type == TYPE_NEW) ||
			(type == 2 && line->type == TYPE_TRANS) ||
			(type == 3 && line->type == TYPE_NONE)	)
	{
		gtk_list_store_set(listStore, &iter, COLUMN_VISIBLE, TRUE, -1);
	} else {
		gtk_list_store_set(listStore, &iter, COLUMN_VISIBLE, FALSE, -1);
	}
}

void SludgeTranslationEditor::on_sort_clicked(GtkTreeViewColumn *theTreeViewColumn, int sortColumn)
{
	GtkTreeViewColumn *theOtherColumn;
	if (sortColumn == COLUMN_ORIGINAL) {
		theOtherColumn = translationColumn;
	} else {
		theOtherColumn = originalColumn;
	}
	if (gtk_tree_view_column_get_sort_indicator(theOtherColumn)) {
		gtk_tree_view_column_set_sort_indicator(theOtherColumn, FALSE);
	}

	if (!gtk_tree_view_column_get_sort_indicator(theTreeViewColumn)) {
		gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE (sortModel),
		                                    sortColumn, GTK_SORT_ASCENDING);
	} else {
		gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE (sortModel),
		                                    COLUMN_ID, GTK_SORT_ASCENDING);
	}
	gtk_tree_view_column_set_sort_indicator(theTreeViewColumn,
		!gtk_tree_view_column_get_sort_indicator(theTreeViewColumn));

	// Resize, because sort indicator may change width of column:
	GtkWidget *treeView = gtk_tree_view_column_get_tree_view(theTreeViewColumn);
	gtk_container_resize_children(GTK_CONTAINER(treeView));
}

void SludgeTranslationEditor::on_load_strings_clicked()
{
	GtkWidget *dialog;
	GtkFileFilter *filter;

	dialog = gtk_file_chooser_dialog_new("Select a SLUDGE Project",
				      NULL,
				      GTK_FILE_CHOOSER_ACTION_OPEN,
				      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
				      GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT,
				      NULL);

	filter = gtk_file_filter_new();
	gtk_file_filter_set_name(filter, "SLUDGE Project Files (*.slp)");
	gtk_file_filter_add_pattern(filter, "*.[sS][lL][pP]");
	gtk_file_chooser_add_filter(GTK_FILE_CHOOSER (dialog), filter);

	gtk_file_chooser_set_filter(GTK_FILE_CHOOSER (dialog), filter);

	gtk_file_chooser_set_select_multiple(GTK_FILE_CHOOSER (dialog), FALSE);

	if (currentFolder[0] != 0)
	{
		gtk_file_chooser_set_current_folder(GTK_FILE_CHOOSER (dialog), currentFolder);
	}

	if (gtk_dialog_run(GTK_DIALOG (dialog)) == GTK_RESPONSE_ACCEPT)
	{
		char *filename;
		filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER (dialog));
		flipBackslashes(&filename);

		if (updateFromProject (filename, &firstTransLine)) {
			listChanged();
		}
		setFolderFromFilename(filename);

		g_free(filename);
	}
	gtk_widget_destroy(dialog);
}

gboolean SludgeTranslationEditor::searchEqualFunc(GtkTreeModel *model, const gchar *key, GtkTreeIter *iter)
{
	gboolean retval = TRUE;
	char *escaped_key, *pattern, *original, *translation;
	GRegex *regex;
	GError *error = NULL;

	gtk_tree_model_get(model, iter, COLUMN_ORIGINAL, &original, COLUMN_TRANSLATION, &translation, -1);

	escaped_key = g_regex_escape_string(key, strlen(key));
	pattern = new char[strlen(escaped_key) + 5];
	sprintf(pattern, ".*%s.*", escaped_key);
	g_free(escaped_key);

   	regex = g_regex_new (pattern, G_REGEX_CASELESS, G_REGEX_MATCH_ANCHORED, &error);

	if (regex == NULL) fprintf(stderr, "%s\n", error->message);

	if (original) {
		if (strlen(original)) {
			if (g_regex_match (regex, original, G_REGEX_MATCH_ANCHORED, NULL))
			{
				retval = FALSE;
			}
		}
	}
	if (retval && translation) {
		if (strlen(translation)) {
			if (g_regex_match (regex, translation, G_REGEX_MATCH_ANCHORED, NULL))
			{
				retval = FALSE;
			}
		}
	}
	g_free(original);
	g_free(translation);
	g_regex_unref(regex);
	delete pattern;

	return retval;
}

