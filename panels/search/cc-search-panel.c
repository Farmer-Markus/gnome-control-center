/* -*- mode: C; c-file-style: "gnu"; indent-tabs-mode: nil; -*-
 *
 * Copyright (C) 2012 Red Hat, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Author: Cosimo Cecchi <cosimoc@gnome.org>
 */

#include "cc-search-panel.h"
#include "cc-search-locations-dialog.h"

#include <egg-list-box/egg-list-box.h>
#include <gio/gdesktopappinfo.h>
#include <glib/gi18n.h>

CC_PANEL_REGISTER (CcSearchPanel, cc_search_panel)

#define WID(s) GTK_WIDGET (gtk_builder_get_object (self->priv->builder, s))

struct _CcSearchPanelPrivate
{
  GtkBuilder *builder;
  GtkWidget  *list_box;
  GtkWidget  *up_button;
  GtkWidget  *down_button;

  GSettings  *search_settings;
  GHashTable *sort_order;

  GtkWidget  *locations_dialog;
};

#define SHELL_PROVIDER_GROUP "Shell Search Provider"

static gint
list_sort_func (gconstpointer a,
                gconstpointer b,
                gpointer user_data)
{
  CcSearchPanel *self = user_data;
  GtkWidget *widget_a, *widget_b;
  GAppInfo *app_a, *app_b;
  const gchar *id_a, *id_b;
  gint idx_a, idx_b, num_sorted;
  gpointer lookup;

  widget_a = GTK_WIDGET (a);
  widget_b = GTK_WIDGET (b);

  app_a = g_object_get_data (G_OBJECT (widget_a), "app-info");
  app_b = g_object_get_data (G_OBJECT (widget_b), "app-info");

  id_a = g_app_info_get_id (app_a);
  id_b = g_app_info_get_id (app_b);

  /* find the index of the application in the GSettings preferences */
  idx_a = -1;
  idx_b = -1;

  lookup = g_hash_table_lookup (self->priv->sort_order, id_a);
  if (lookup)
    idx_a = GPOINTER_TO_INT (lookup) - 1;

  lookup = g_hash_table_lookup (self->priv->sort_order, id_b);
  if (lookup)
    idx_b = GPOINTER_TO_INT (lookup) - 1;

  /* if neither app is found, use alphabetical order */
  if ((idx_a == -1) && (idx_b == -1))
    return g_utf8_collate (g_app_info_get_name (app_a), g_app_info_get_name (app_b));

  num_sorted = g_hash_table_size (self->priv->sort_order) - 1;
  if (num_sorted > 1)
    {
      /* if app_a is the last, it goes after everything */
      if (idx_a == num_sorted)
        return 1;
      /* if app_b is the last, it goes after everything */
      else if (idx_b == num_sorted)
        return -1;
    }

  /* if app_a isn't found, it's sorted after app_b */
  if (idx_a == -1)
    return 1;

  /* if app_b isn't found, it's sorted after app_a */
  if (idx_b == -1)
    return -1;

  /* finally, if both apps are found, return their order in the list */
  return (idx_a - idx_b);
}

static void
search_panel_invalidate_button_state (CcSearchPanel *self)
{
  GList *children;
  gboolean is_first, is_last;
  GtkWidget *child;

  child = egg_list_box_get_selected_child (EGG_LIST_BOX (self->priv->list_box));
  children = gtk_container_get_children (GTK_CONTAINER (self->priv->list_box));

  if (!child || !children)
    return;

  is_first = (child == g_list_first (children)->data);
  is_last = (child == g_list_last (children)->data);

  gtk_widget_set_sensitive (self->priv->up_button, !is_first);
  gtk_widget_set_sensitive (self->priv->down_button, !is_last);

  g_list_free (children);
}

static void
search_panel_invalidate_sort_order (CcSearchPanel *self)
{
  gchar **sort_order;
  gint idx;

  g_hash_table_remove_all (self->priv->sort_order);
  sort_order = g_settings_get_strv (self->priv->search_settings, "sort-order");

  for (idx = 0; sort_order[idx] != NULL; idx++)
    g_hash_table_insert (self->priv->sort_order, g_strdup (sort_order[idx]), GINT_TO_POINTER (idx + 1));

  egg_list_box_resort (EGG_LIST_BOX (self->priv->list_box));
  g_strfreev (sort_order);

  search_panel_invalidate_button_state (self);
}

static gint
propagate_compare_func (gconstpointer a,
                        gconstpointer b,
                        gpointer user_data)
{
  CcSearchPanel *self = user_data;
  const gchar *key_a = a, *key_b = b;
  gint idx_a, idx_b;

  idx_a = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->sort_order, key_a));
  idx_b = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->sort_order, key_b));

  return (idx_a - idx_b);
}

static void
search_panel_propagate_sort_order (CcSearchPanel *self)
{
  GList *keys, *l;
  GPtrArray *sort_order;

  sort_order = g_ptr_array_new ();
  keys = g_hash_table_get_keys (self->priv->sort_order);
  keys = g_list_sort_with_data (keys, propagate_compare_func, self);

  for (l = keys; l != NULL; l = l->next)
    g_ptr_array_add (sort_order, l->data);

  g_ptr_array_add (sort_order, NULL);
  g_settings_set_strv (self->priv->search_settings, "sort-order",
                       (const gchar **) sort_order->pdata);

  g_ptr_array_unref (sort_order);
  g_list_free (keys);
}

static void
search_panel_set_no_providers (CcSearchPanel *self)
{
  GtkWidget *w;

  /* center the list box in the scrolled window */
  gtk_widget_set_valign (self->priv->list_box, GTK_ALIGN_CENTER);

  w = gtk_label_new (_("No applications found"));
  gtk_widget_show (w);

  gtk_container_add (GTK_CONTAINER (self->priv->list_box), w);
}

static void
search_panel_move_selected (CcSearchPanel *self,
                            gboolean down)
{
  GtkWidget *box, *other_box;
  GAppInfo *app_info, *other_app_info;
  const gchar *app_id, *other_app_id;
  gint idx, other_idx;
  GList *children, *l;

  box = egg_list_box_get_selected_child (EGG_LIST_BOX (self->priv->list_box));
  app_info = g_object_get_data (G_OBJECT (box), "app-info");
  app_id = g_app_info_get_id (app_info);

  other_app_id = NULL;
  children = gtk_container_get_children (GTK_CONTAINER (self->priv->list_box));
  l = g_list_find (children, box);

  if (l != NULL)
    {
      other_box = down ? g_list_next (l)->data : g_list_previous (l)->data;
      other_app_info = g_object_get_data (G_OBJECT (other_box), "app-info");
      other_app_id = g_app_info_get_id (other_app_info);
    }

  other_idx = GPOINTER_TO_INT (g_hash_table_lookup (self->priv->sort_order, app_id));
  idx = down ? (other_idx + 1) : (other_idx - 1);

  if (other_app_id != NULL)
    g_hash_table_replace (self->priv->sort_order, g_strdup (other_app_id), GINT_TO_POINTER (other_idx));

  g_hash_table_replace (self->priv->sort_order, g_strdup (app_id), GINT_TO_POINTER (idx));

  search_panel_propagate_sort_order (self);

  g_list_free (children);
}

static void
down_button_clicked (GtkWidget *widget,
                     CcSearchPanel *self)
{
  search_panel_move_selected (self, TRUE);
}

static void
up_button_clicked (GtkWidget *widget,
                   CcSearchPanel *self)
{
  search_panel_move_selected (self, FALSE);
}

static void
settings_button_clicked (GtkWidget *widget,
                         gpointer user_data)
{
  CcSearchPanel *self = user_data;

  if (self->priv->locations_dialog == NULL)
    {
      self->priv->locations_dialog = cc_search_locations_dialog_new (self);
      g_object_add_weak_pointer (G_OBJECT (self->priv->locations_dialog),
                                 (gpointer *) &self->priv->locations_dialog);
    }

  gtk_window_present (GTK_WINDOW (self->priv->locations_dialog));
}

static GVariant *
switch_settings_mapping_set (const GValue *value,
                             const GVariantType *expected_type,
                             gpointer user_data)
{
  GtkWidget *box = user_data;
  CcSearchPanel *self = g_object_get_data (G_OBJECT (box), "self");
  GAppInfo *app_info = g_object_get_data (G_OBJECT (box), "app-info");
  gchar **disabled;
  GPtrArray *new_disabled;
  gint idx;
  gboolean remove, found;
  GVariant *variant;

  remove = g_value_get_boolean (value);
  found = FALSE;
  new_disabled = g_ptr_array_new_with_free_func (g_free);
  disabled = g_settings_get_strv (self->priv->search_settings, "disabled");

  for (idx = 0; disabled[idx] != NULL; idx++)
    {
      if (g_strcmp0 (disabled[idx], g_app_info_get_id (app_info)) == 0)
        {
          found = TRUE;

          if (remove)
            continue;
        }

      g_ptr_array_add (new_disabled, g_strdup (disabled[idx]));
    }

  if (!found && !remove)
    g_ptr_array_add (new_disabled, g_strdup (g_app_info_get_id (app_info)));

  g_ptr_array_add (new_disabled, NULL);

  variant = g_variant_new_strv ((const gchar **) new_disabled->pdata, -1);
  g_ptr_array_unref (new_disabled);
  g_strfreev (disabled);

  return variant;
}

static gboolean
switch_settings_mapping_get (GValue *value,
                             GVariant *variant,
                             gpointer user_data)
{
  GtkWidget *box = user_data;
  GAppInfo *app_info = g_object_get_data (G_OBJECT (box), "app-info");
  const gchar **disabled;
  gint idx;
  gboolean found;

  found = FALSE;
  disabled = g_variant_get_strv (variant, NULL);

  for (idx = 0; disabled[idx] != NULL; idx++)
    {
      if (g_strcmp0 (disabled[idx], g_app_info_get_id (app_info)) == 0)
        {
          found = TRUE;
          break;
        }
    }

  g_value_set_boolean (value, !found);

  return TRUE;
}

static void
search_panel_add_one_app_info (CcSearchPanel *self,
                               GAppInfo *app_info)
{
  GtkWidget *box, *w;
  GIcon *icon;

  /* reset valignment of the list box */
  gtk_widget_set_valign (self->priv->list_box, GTK_ALIGN_FILL);

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);
  gtk_widget_set_hexpand (box, TRUE);
  gtk_container_set_border_width (GTK_CONTAINER (box), 6);
  g_object_set_data_full (G_OBJECT (box), "app-info",
                          g_object_ref (app_info), g_object_unref);
  g_object_set_data (G_OBJECT (box), "self", self);
  gtk_container_add (GTK_CONTAINER (self->priv->list_box), box);

  icon = g_app_info_get_icon (app_info);
  if (icon == NULL)
    icon = g_themed_icon_new ("application-x-executable");
  else
    g_object_ref (icon);

  w = gtk_image_new_from_gicon (icon, GTK_ICON_SIZE_DIALOG);
  gtk_container_add (GTK_CONTAINER (box), w);
  g_object_unref (icon);

  w = gtk_label_new (g_app_info_get_name (app_info));
  gtk_container_add (GTK_CONTAINER (box), w);

  w = gtk_switch_new ();
  gtk_widget_set_valign (w, GTK_ALIGN_CENTER);
  gtk_box_pack_end (GTK_BOX (box), w, FALSE, FALSE, 0);

  g_settings_bind_with_mapping (self->priv->search_settings, "disabled",
                                w, "active",
                                G_SETTINGS_BIND_DEFAULT,
                                switch_settings_mapping_get,
                                switch_settings_mapping_set,
                                box, NULL);

  gtk_widget_show_all (box);
}

static void
search_panel_add_one_provider (CcSearchPanel *self,
                               GFile *provider)
{
  gchar *path, *desktop_id;
  GKeyFile *keyfile;
  GAppInfo *app_info;
  GError *error = NULL;

  path = g_file_get_path (provider);
  keyfile = g_key_file_new ();
  g_key_file_load_from_file (keyfile, path, G_KEY_FILE_NONE, &error);

  if (error != NULL)
    {
      g_warning ("Error loading %s: %s - search provider will be ignored",
                 path, error->message);
      goto out;
    }

  if (!g_key_file_has_group (keyfile, SHELL_PROVIDER_GROUP))
    goto out;

  desktop_id = g_key_file_get_string (keyfile, SHELL_PROVIDER_GROUP,
                                      "DesktopId", &error);

  if (error != NULL)
    {
      g_warning ("Unable to read desktop ID from %s: %s - search provider will be ignored",
                 path, error->message);
      goto out;
    }

  app_info = G_APP_INFO (g_desktop_app_info_new (desktop_id));
  g_free (desktop_id);

  if (app_info == NULL)
    goto out;

  search_panel_add_one_app_info (self, app_info);
  g_object_unref (app_info);

 out:
  g_free (path);
  g_clear_error (&error);
  g_key_file_unref (keyfile);
}

static void
next_search_provider_ready (GObject *source,
                            GAsyncResult *res,
                            gpointer user_data)
{
  CcSearchPanel *self = user_data;
  GFile *providers_location, *provider;
  GList *files;
  GError *error = NULL;
  gchar *path;

  files = g_file_enumerator_next_files_finish (G_FILE_ENUMERATOR (source),
                                               res, &error);
  providers_location = g_file_enumerator_get_container (G_FILE_ENUMERATOR (source));

  if (error != NULL)
    {
      path = g_file_get_path (providers_location);

      g_warning ("Error reading from %s: %s - search providers might be missing from the panel",
                 path, error->message);

      g_error_free (error);
      g_free (path);
    }

  if (files != NULL)
    {
      provider = g_file_get_child (providers_location, g_file_info_get_name (files->data));
      search_panel_add_one_provider (self, provider);
      g_object_unref (provider);

      g_file_enumerator_next_files_async (G_FILE_ENUMERATOR (source), 1,
                                          G_PRIORITY_DEFAULT, NULL,
                                          next_search_provider_ready, self);
    }
  else
    {
      /* propagate a write to GSettings, to make sure we always have
       * all the providers in the list.
       */
      search_panel_propagate_sort_order (self);
    }

  g_list_free_full (files, g_object_unref);
}

static void
enumerate_search_providers_ready (GObject *source,
                                  GAsyncResult *res,
                                  gpointer user_data)
{
  CcSearchPanel *self = user_data;
  GFileEnumerator *enumerator;
  GError *error = NULL;
  gchar *path;

  enumerator = g_file_enumerate_children_finish (G_FILE (source), res, &error);
  if (error != NULL)
    {
      path = g_file_get_path (G_FILE (source));

      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Error opening %s: %s - search provider configuration won't be possible",
                   path, error->message);

      search_panel_set_no_providers (self);
      g_error_free (error);
      return;
    }

  g_file_enumerator_next_files_async (enumerator, 1,
                                      G_PRIORITY_DEFAULT, NULL,
                                      next_search_provider_ready, self);
  g_object_unref (enumerator);
}

static void
populate_search_providers (CcSearchPanel *self)
{
  GFile *providers_location;

  providers_location = g_file_new_for_path (DATADIR "/gnome-shell/search-providers");
  g_file_enumerate_children_async (providers_location,
                                   "standard::type,standard::name,standard::content-type",
                                   G_FILE_QUERY_INFO_NONE,
                                   G_PRIORITY_DEFAULT,
                                   NULL,
                                   enumerate_search_providers_ready, self);
  g_object_unref (providers_location);
}

static void
cc_search_panel_finalize (GObject *object)
{
  CcSearchPanelPrivate *priv = CC_SEARCH_PANEL (object)->priv;

  g_clear_object (&priv->builder);
  g_clear_object (&priv->search_settings);
  g_hash_table_destroy (priv->sort_order);

  if (priv->locations_dialog)
    gtk_widget_destroy (priv->locations_dialog);

  G_OBJECT_CLASS (cc_search_panel_parent_class)->finalize (object);
}

static void
cc_search_panel_constructed (GObject *object)
{
  CcSearchPanel *self = CC_SEARCH_PANEL (object);
  GtkWidget *box, *widget, *search_box;

  G_OBJECT_CLASS (cc_search_panel_parent_class)->constructed (object);

  /* add the disable all switch */
  search_box = WID ("search_vbox");
  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 6);

  widget = gtk_label_new (_("Enabled"));
  gtk_container_add (GTK_CONTAINER (box), widget);

  widget = gtk_switch_new ();
  gtk_container_add (GTK_CONTAINER (box), widget);

  g_settings_bind (self->priv->search_settings, "disable-external",
                   widget, "active",
                   G_SETTINGS_BIND_DEFAULT |
                   G_SETTINGS_BIND_INVERT_BOOLEAN);

  g_object_bind_property (widget, "active",
                          search_box, "sensitive",
                          G_BINDING_DEFAULT |
                          G_BINDING_SYNC_CREATE);

  gtk_widget_show_all (box);
  cc_shell_embed_widget_in_header (cc_panel_get_shell (CC_PANEL (self)), box);
}

static void
cc_search_panel_init (CcSearchPanel *self)
{
  GError    *error;
  GtkWidget *widget;
  GtkWidget *scrolled_window;
  guint res;

  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self, CC_TYPE_SEARCH_PANEL, CcSearchPanelPrivate);

  self->priv->builder = gtk_builder_new ();

  error = NULL;
  res = gtk_builder_add_from_file (self->priv->builder,
                                   GNOMECC_UI_DIR "/search.ui",
                                   &error);

  if (res == 0)
    {
      g_warning ("Could not load interface file: %s",
                 (error != NULL) ? error->message : "unknown error");
      g_clear_error (&error);
      return;
    }

  scrolled_window = WID ("scrolled_window");
  widget = GTK_WIDGET (egg_list_box_new ());
  egg_list_box_set_sort_func (EGG_LIST_BOX (widget),
                              list_sort_func, self, NULL);
  egg_list_box_add_to_scrolled (EGG_LIST_BOX (widget), GTK_SCROLLED_WINDOW (scrolled_window));
  self->priv->list_box = widget;
  gtk_widget_show (widget);

  g_signal_connect_swapped (widget, "child-selected",
                            G_CALLBACK (search_panel_invalidate_button_state), self);

  self->priv->up_button = WID ("up_button");
  g_signal_connect (self->priv->up_button, "clicked",
                    G_CALLBACK (up_button_clicked), self);
  gtk_widget_set_sensitive (self->priv->up_button, FALSE);

  self->priv->down_button = WID ("down_button");
  g_signal_connect (self->priv->down_button, "clicked",
                    G_CALLBACK (down_button_clicked), self);
  gtk_widget_set_sensitive (self->priv->down_button, FALSE);

  widget = WID ("settings_button");
  g_signal_connect (widget, "clicked",
                    G_CALLBACK (settings_button_clicked), self);
  gtk_widget_set_sensitive (widget, cc_search_locations_dialog_is_available ());

  self->priv->search_settings = g_settings_new ("org.gnome.desktop.search-providers");
  self->priv->sort_order = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free, NULL);
  g_signal_connect_swapped (self->priv->search_settings, "changed::sort-order",
                            G_CALLBACK (search_panel_invalidate_sort_order), self);
  search_panel_invalidate_sort_order (self);

  populate_search_providers (self);

  widget = WID ("search_vbox");
  gtk_widget_reparent (widget, (GtkWidget *) self);
}

static void
cc_search_panel_class_init (CcSearchPanelClass *klass)
{
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->constructed = cc_search_panel_constructed;
  oclass->finalize = cc_search_panel_finalize;

  g_type_class_add_private (klass, sizeof (CcSearchPanelPrivate));
}

void
cc_search_panel_register (GIOModule *module)
{
  cc_search_panel_register_type (G_TYPE_MODULE (module));
  g_io_extension_point_implement (CC_SHELL_PANEL_EXTENSION_POINT,
                                  CC_TYPE_SEARCH_PANEL,
                                  "search", 0);
}
