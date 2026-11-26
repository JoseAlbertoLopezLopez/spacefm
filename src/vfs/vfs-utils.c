/*
 *      vfs-utils.c
 *
 *      Copyright 2008 PCMan <pcman.tw@gmail.com>
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 3 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *      MA 02110-1301, USA.
 */

#include <glib/gi18n.h>
#include <string.h>

#include "vfs-utils.h"
#include "settings.h"  //MOD

GdkPixbuf* vfs_load_icon( GtkIconTheme* theme, const char* icon_name, int size )
{
    GdkPixbuf* icon = NULL;
    const char* file;

    if ( !icon_name )
        return NULL;

    GtkIconInfo* inf = gtk_icon_theme_lookup_icon( theme, icon_name, size,
                                             GTK_ICON_LOOKUP_USE_BUILTIN |
                                             GTK_ICON_LOOKUP_FORCE_SIZE );

    if ( !inf && icon_name[0] == '/' )
        return gdk_pixbuf_new_from_file_at_size ( icon_name, size, size, NULL );
    
    if( G_UNLIKELY( ! inf ) )
        return NULL;

    file = gtk_icon_info_get_filename( inf );
    if( G_LIKELY( file ) )
        icon = gdk_pixbuf_new_from_file_at_size( file, size, size, NULL );
    else
    {
        icon = gtk_icon_info_get_builtin_pixbuf( inf );
        g_object_ref( icon );
    }
    gtk_icon_info_free( inf );
/*
    if( G_LIKELY( icon ) )
    {
        // scale down the icon if it's too big
        int width, height;
        height = gdk_pixbuf_get_height(icon);
        width = gdk_pixbuf_get_width(icon);

        if( G_UNLIKELY( height > size || width > size ) )
        {
            GdkPixbuf* scaled;
            if( height > width )
            {
                width = size * height / width;
                height = size;
            }
            else if( height < width )
            {
                height = size * width / height;
                width = size;
            }
            else
                height = width = size;
            scaled = gdk_pixbuf_scale_simple( icon, width, height, GDK_INTERP_BILINEAR );
            g_object_unref( icon );
            icon = scaled;
        }
    }
*/
    return icon;
}

/*
 * Load an icon using GTK's themed renderer when possible.  Symbolic icons
 * (-symbolic) need to be colored with the widget's style context; loading the
 * SVG file directly yields the flat gray color embedded in the icon file.
 * The returned pixbuf has size * scale pixels, ready to be wrapped in a
 * cairo surface with the same scale (avoids bilinear re-scaling by GTK).
 */
GdkPixbuf* vfs_load_icon_at_scale( GtkWidget* widget, const char* icon_name,
                                   int size, int scale )
{
#if GTK_CHECK_VERSION( 3, 10, 0 )
    GtkIconTheme* theme;
    GtkIconInfo* info;
    GdkPixbuf* icon = NULL;
    int pixel_size;

    if ( scale < 1 )
        scale = 1;
    pixel_size = size * scale;

    if ( !( widget && icon_name && icon_name[0] ) )
        return vfs_load_icon( gtk_icon_theme_get_default(), icon_name,
                                                              pixel_size );

    theme = gtk_icon_theme_get_default();
    info = gtk_icon_theme_lookup_icon_for_scale( theme, icon_name, size, scale,
                       GTK_ICON_LOOKUP_USE_BUILTIN | GTK_ICON_LOOKUP_FORCE_SIZE );
    if ( G_UNLIKELY( !info ) )
        return vfs_load_icon( theme, icon_name, pixel_size );

    if ( gtk_icon_info_is_symbolic( info ) )
        icon = gtk_icon_info_load_symbolic_for_context(
                    info, gtk_widget_get_style_context( widget ), NULL, NULL );
    else
        icon = gtk_icon_theme_load_icon_for_scale( theme, icon_name, size, scale,
                       GTK_ICON_LOOKUP_USE_BUILTIN | GTK_ICON_LOOKUP_FORCE_SIZE,
                       NULL );

    if ( !icon )
        icon = gtk_icon_theme_load_icon_for_scale( theme, icon_name, size, scale,
                                     GTK_ICON_LOOKUP_USE_BUILTIN, NULL );

    gtk_icon_info_free( info );
    return icon;
#else
    (void)scale;
    return vfs_load_icon( gtk_icon_theme_get_default(), icon_name, size );
#endif
}

/* Load a themed icon at logical size for widgets that expect a 1x pixbuf. */
GdkPixbuf* vfs_load_icon_for_widget( GtkWidget* widget, const char* icon_name,
                                                                     int size )
{
    return vfs_load_icon_at_scale( widget, icon_name, size, 1 );
}

#ifdef HAVE_HAL
static char* find_su_program( GError** err )
{
    char* su = NULL;

#ifdef PREFERABLE_SUDO_PROG
    su = g_find_program_in_path( PREFERABLE_SUDO_PROG );
#endif
    // Use default search rules
    if ( ! su )
        su = get_valid_gsu();
    if ( ! su )
        su = g_find_program_in_path( "ktsuss" );
    if ( ! su )
        su = g_find_program_in_path( "gksudo" );
    if ( ! su )
        su = g_find_program_in_path( "gksu" );
    if ( ! su )
        su = g_find_program_in_path( "kdesu" );
        
    if ( ! su )
        g_set_error( err, G_SPAWN_ERROR, G_SPAWN_ERROR_FAILED, _( "su command not found" ) ); //MOD

    return su;
}

gboolean vfs_sudo_cmd_sync( const char* cwd, char* argv[],
                            int* exit_status,
                            char** pstdout, char** pstderr, GError** err )  //MOD
{
    char *su;  //MOD
    gboolean ret;

    if ( ! ( su = find_su_program( err ) ) )
        return FALSE;

    argv[0] = su;
    if ( ! strstr( su, "ktsuss" ) )  //MOD
    {
        // Combine arguments for gksu, kdesu, etc but not for ktsuss
        argv[1] = g_strdup_printf( "%s %s '%s'", argv[1], argv[2], argv[3] );
        argv[2] = NULL;
    }

    ret = g_spawn_sync( cwd, argv, NULL,
                   G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                   NULL, NULL,
                   pstdout, pstderr, exit_status, err );
    return ret;
}
#endif

/*

gboolean vfs_sudo_cmd_async( const char* cwd, const char* cmd, GError** err )
{
    char *su, *argv[3];
    gboolean ret;

    if ( ! ( su = find_su_program( err ) ) )
        return FALSE;

    argv[0] = su;
    argv[1] = g_strdup( cmd );
    argv[2] = NULL;

    ret = g_spawn_async( cwd, argv, NULL,
                   G_SPAWN_STDOUT_TO_DEV_NULL | G_SPAWN_STDERR_TO_DEV_NULL,
                   NULL, NULL, NULL, err );

    return ret;
}

static char* argv_to_cmdline( char** argv )
{
    GString* cmd;
    char* quoted;
    cmd = g_string_new(NULL);
    while( *argv )
    {
        quoted = g_shell_quote( *argv );
        g_string_append( cmd, quoted );
        g_free( quoted );
    }
    return g_string_free( cmd, FALSE );
}
*/
