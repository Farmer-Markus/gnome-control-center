/*
 * cc-system-panel.c
 *
 * Copyright 2023 Gotam Gorabh <gautamy672@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <config.h>

#include <glib/gi18n-lib.h>



#include <glib/gi18n.h>
#include "about/cc-system-details-window.h"
#include "cc-hostname-entry.h"



#include "cc-list-row.h"
#include "cc-system-panel.h"
#include "cc-system-resources.h"
#include "cc-systemd-service.h"

#include "about/cc-about-page.h"
#include "datetime/cc-datetime-page.h"
#include "region/cc-region-page.h"
#include "remote-desktop/cc-remote-desktop-page.h"
#include "secure-shell/cc-secure-shell-page.h"
#include "users/cc-users-page.h"

struct _CcSystemPanel
{
  CcPanel    parent_instance;

  AdwActionRow *about_row;
  AdwActionRow *datetime_row;
  AdwActionRow *region_row;
  AdwActionRow *remote_desktop_row;
  AdwActionRow *users_row;

  CcSecureShellPage *secure_shell_dialog;
  AdwNavigationPage *software_updates_group;


  AdwActionRow    *disk_row;
  AdwActionRow    *hardware_model_row;
  AdwActionRow    *memory_row;
  GtkPicture      *os_logo;
  AdwActionRow    *os_name_row;
  AdwActionRow    *processor_row;

  AdwDialog       *system_details_window;
  guint            create_system_details_id;
};

CC_PANEL_REGISTER (CcSystemPanel, cc_system_panel)






//G_DEFINE_TYPE (CcSystemPanel, cc_system_panel, ADW_TYPE_NAVIGATION_PAGE)


static void
system_about_page_setup_overview (CcSystemPanel *self)
{
  guint64 ram_size;
  g_autofree char *memory_text = NULL;
  g_autofree char *cpu_text = NULL;
  g_autofree char *os_name_text = NULL;
  g_autofree char *hardware_model_text = NULL;
  g_autofree gchar *disk_capacity_string = NULL;

  hardware_model_text = get_hardware_model_string ();
  adw_action_row_set_subtitle (self->hardware_model_row, hardware_model_text);
  gtk_widget_set_visible (GTK_WIDGET (self->hardware_model_row), hardware_model_text != NULL);

  ram_size = get_ram_size_dmi ();
  if (ram_size == 0)
    ram_size = get_ram_size_libgtop ();
  memory_text = g_format_size_full (ram_size, G_FORMAT_SIZE_IEC_UNITS);
  adw_action_row_set_subtitle (self->memory_row, memory_text);

  cpu_text = get_cpu_info ();
  adw_action_row_set_subtitle (self->processor_row, cpu_text);

  disk_capacity_string = get_primary_disk_info ();
  if (disk_capacity_string == NULL)
    disk_capacity_string = g_strdup (_("Unknown"));
  adw_action_row_set_subtitle (self->disk_row, disk_capacity_string);

  os_name_text = get_os_name ();
  adw_action_row_set_subtitle (self->os_name_row, os_name_text);
}

static gboolean
cc_system_about_page_create_system_details (CcSystemPanel *self)
{
  if (!self->system_details_window)
    {
      self->system_details_window = ADW_DIALOG (cc_system_details_window_new ());
      g_object_ref_sink (self->system_details_window);
    }

  g_clear_handle_id (&self->create_system_details_id, g_source_remove);

  return G_SOURCE_REMOVE;
}

static void
cc_system_about_page_open_system_details (CcSystemPanel *self)
{
  cc_system_about_page_create_system_details (self);

  adw_dialog_present (self->system_details_window, GTK_WIDGET (self));
}

#if !defined(DISTRIBUTOR_LOGO) || defined(DARK_MODE_DISTRIBUTOR_LOGO)
static gboolean
use_dark_theme (CcSystemPanel *self)
{
  AdwStyleManager *style_manager = adw_style_manager_get_default ();

  return adw_style_manager_get_dark (style_manager);
}
#endif

tatic void
setup_os_logo (CcSystemPanel *self)
{
#ifdef DISTRIBUTOR_LOGO
#ifdef DARK_MODE_DISTRIBUTOR_LOGO
  if (use_dark_theme (self))
    {
      gtk_picture_set_filename (self->os_logo, DARK_MODE_DISTRIBUTOR_LOGO);
      return;
    }
#endif
  gtk_picture_set_filename (self->os_logo, DISTRIBUTOR_LOGO);
  return;
#else
  GtkIconTheme *icon_theme;
  g_autofree char *logo_name = g_get_os_info ("LOGO");
  g_autoptr(GtkIconPaintable) icon_paintable = NULL;
  g_autoptr(GPtrArray) array = NULL;
  g_autoptr(GIcon) icon = NULL;
  gboolean dark;

  dark = use_dark_theme (self);
  if (logo_name == NULL)
    logo_name = g_strdup ("gnome-logo");

  array = g_ptr_array_new_with_free_func (g_free);
  if (dark)
    g_ptr_array_add (array, (gpointer) g_strdup_printf ("%s-text-dark", logo_name));
  g_ptr_array_add (array, (gpointer) g_strdup_printf ("%s-text", logo_name));
  if (dark)
    g_ptr_array_add (array, (gpointer) g_strdup_printf ("%s-dark", logo_name));
  g_ptr_array_add (array, (gpointer) g_strdup_printf ("%s", logo_name));

  icon = g_themed_icon_new_from_names ((char **) array->pdata, array->len);
  icon_theme = gtk_icon_theme_get_for_display (gdk_display_get_default ());
  icon_paintable = gtk_icon_theme_lookup_by_gicon (icon_theme, icon,
                                                   192,
                                                   gtk_widget_get_scale_factor (GTK_WIDGET (self)),
                                                   gtk_widget_get_direction (GTK_WIDGET (self)),
                                                   0);
  gtk_picture_set_paintable (self->os_logo, GDK_PAINTABLE (icon_paintable));
#endif
}

static void
cc_about_page_dispose (GObject *object)
{
  CcAboutPage *self = CC_SYSTEM_PANEL (object);

  if (self->system_details_window)
    adw_dialog_close (self->system_details_window);
  g_clear_object (&self->system_details_window);

  g_clear_handle_id (&self->create_system_details_id, g_source_remove);

  G_OBJECT_CLASS (cc_about_page_parent_class)->dispose (object);
}






static gboolean
gnome_software_allows_updates (void)
{
  const gchar *schema_id  = "org.gnome.software";
  GSettingsSchemaSource *source;
  g_autoptr(GSettingsSchema) schema = NULL;
  g_autoptr(GSettings) settings = NULL;

  source = g_settings_schema_source_get_default ();

  if (source == NULL)
    return FALSE;

  schema = g_settings_schema_source_lookup (source, schema_id, FALSE);

  if (schema == NULL)
    return FALSE;

  settings = g_settings_new (schema_id);
  return g_settings_get_boolean (settings, "allow-updates");
}

static gboolean
gnome_software_exists (void)
{
  g_autofree gchar *path = g_find_program_in_path ("gnome-software");
  return path != NULL;
}

static gboolean
gpk_update_viewer_exists (void)
{
  g_autofree gchar *path = g_find_program_in_path ("gpk-update-viewer");
  return path != NULL;
}

static gboolean
show_software_updates_group (CcSystemPanel *self)
{
  return (gnome_software_exists () && gnome_software_allows_updates ()) ||
         gpk_update_viewer_exists ();
}

static void
cc_system_page_open_software_update (CcSystemPanel *self)
{
  g_autoptr(GError) error = NULL;
  gboolean ret;
  char *argv[3];

  if (gnome_software_exists ())
    {
      argv[0] = "gnome-software";
      argv[1] = "--mode=updates";
      argv[2] = NULL;
    }
  else
    {
      argv[0] = "gpk-update-viewer";
      argv[1] = NULL;
    }

  ret = g_spawn_async (NULL, argv, NULL, G_SPAWN_SEARCH_PATH, NULL, NULL, NULL, &error);
  if (!ret)
    g_warning ("Failed to spawn %s: %s", argv[0], error->message);
}

static void
on_secure_shell_row_clicked (CcSystemPanel *self)
{
  if (self->secure_shell_dialog == NULL)
    {
      self->secure_shell_dialog = g_object_new (CC_TYPE_SECURE_SHELL_PAGE, NULL);
      g_object_add_weak_pointer (G_OBJECT (self->secure_shell_dialog),
                                 (gpointer *) &self->secure_shell_dialog);
    }

  adw_dialog_present (ADW_DIALOG (self->secure_shell_dialog), GTK_WIDGET (self));
}

static void
cc_system_panel_class_init (CcSystemPanelClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/control-center/system/cc-system-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, about_row);
  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, datetime_row);
  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, region_row);
  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, remote_desktop_row);
  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, users_row);
  gtk_widget_class_bind_template_child (widget_class, CcSystemPanel, software_updates_group);

  gtk_widget_class_bind_template_callback (widget_class, cc_system_page_open_software_update);
  gtk_widget_class_bind_template_callback (widget_class, on_secure_shell_row_clicked);

  g_type_ensure (CC_TYPE_ABOUT_PAGE);
  g_type_ensure (CC_TYPE_DATE_TIME_PAGE);
  g_type_ensure (CC_TYPE_LIST_ROW);
  g_type_ensure (CC_TYPE_REGION_PAGE);
  g_type_ensure (CC_TYPE_REMOTE_DESKTOP_PAGE);
  g_type_ensure (CC_TYPE_SECURE_SHELL_PAGE);
  g_type_ensure (CC_TYPE_USERS_PAGE);
}

static void
cc_system_panel_init (CcSystemPanel *self)
{
  CcServiceState service_state;

  g_resources_register (cc_system_get_resource ());
  gtk_widget_init_template (GTK_WIDGET (self));

  service_state = cc_get_service_state (REMOTE_DESKTOP_SERVICE, G_BUS_TYPE_SYSTEM);
  /* Hide the remote-desktop page if the g-r-d service is either "masked", "static", or "not-found". */
  gtk_widget_set_visible (GTK_WIDGET (self->remote_desktop_row), service_state == CC_SERVICE_STATE_ENABLED ||
                                                                 service_state == CC_SERVICE_STATE_DISABLED);
  gtk_widget_set_visible (GTK_WIDGET (self->software_updates_group), show_software_updates_group (self));

  cc_panel_add_static_subpage (CC_PANEL (self), "about", CC_TYPE_ABOUT_PAGE);
  cc_panel_add_static_subpage (CC_PANEL (self), "datetime", CC_TYPE_DATE_TIME_PAGE);
  cc_panel_add_static_subpage (CC_PANEL (self), "region", CC_TYPE_REGION_PAGE);
  cc_panel_add_static_subpage (CC_PANEL (self), "remote-desktop", CC_TYPE_REMOTE_DESKTOP_PAGE);
  cc_panel_add_static_subpage (CC_PANEL (self), "users", CC_TYPE_USERS_PAGE);
}
