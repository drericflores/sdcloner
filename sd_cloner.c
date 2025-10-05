#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

GtkWidget *label_source, *label_dest, *label_status, *progress_bar;
gboolean running = FALSE;

void on_quit(GtkWidget *widget, gpointer data) {
    gtk_main_quit();
}

void run_command(const char *cmd) {
    gchar *output = NULL;
    g_spawn_command_line_sync(cmd, &output, NULL, NULL, NULL);
    if (output) g_free(output);
}

void on_read_source(GtkWidget *widget, gpointer data) {
    GtkWidget *dialog = gtk_file_chooser_dialog_new(
        "Select Source Device (/dev/...)", NULL,
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT,
        NULL);

    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        char *filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
        gtk_label_set_text(GTK_LABEL(label_source), filename);
        g_free(filename);
    }
    gtk_widget_destroy(dialog);
}

void on_copy(GtkWidget *widget, gpointer data) {
    gtk_label_set_text(GTK_LABEL(label_status), "Copying source to image...");
    run_command("notify-send 'SD Cloner' 'Simulating copy operation...'");
}

void on_burn(GtkWidget *widget, gpointer data) {
    gtk_label_set_text(GTK_LABEL(label_status), "Burning image to destination...");
    run_command("notify-send 'SD Cloner' 'Simulating burn operation...'");
}

void on_about(GtkWidget *widget, gpointer data) {
    GtkWidget *about_window = gtk_dialog_new_with_buttons(
        "About SD Cloner",
        NULL,
        GTK_DIALOG_MODAL,
        "_Close", GTK_RESPONSE_CLOSE,
        NULL);

    GtkWidget *notebook = gtk_notebook_new();

    GtkWidget *tab1 = gtk_text_view_new();
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab1)),
        "Author: Dr. Eric Oliver Flores\n"
        "Date: 10/25\n"
        "Version: 1.0\n"
        "License: GPLv3\n\n"
        "© 2025 Dr. Eric O. Flores",
        -1);
    GtkWidget *tab2 = gtk_text_view_new();
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab2)),
        "Technologies Used:\n\n"
        "- C Language\n"
        "- GTK3 GUI Toolkit\n"
        "- GLib Threads\n"
        "- dd, gzip, lsblk, df, parted\n"
        "- Pop!_OS / Ubuntu compatible",
        -1);

    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab1, gtk_label_new("About"));
    gtk_notebook_append_page(GTK_NOTEBOOK(notebook), tab2, gtk_label_new("Technologies"));

    gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(about_window))), notebook);
    gtk_widget_show_all(about_window);
    g_signal_connect_swapped(about_window, "response", G_CALLBACK(gtk_widget_destroy), about_window);
}

GtkWidget* make_menu_bar(GtkWidget *window) {
    GtkAccelGroup *accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(window), accel);

    GtkWidget *menubar = gtk_menu_bar_new();

    // File Menu
    GtkWidget *file_menu = gtk_menu_new();
    GtkWidget *file_item = gtk_menu_item_new_with_mnemonic("_File");
    GtkWidget *quit_item = gtk_menu_item_new_with_mnemonic("_Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(file_menu), quit_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(file_item), file_menu);
    gtk_widget_add_accelerator(quit_item, "activate", accel, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_quit), NULL);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), file_item);

    // Tools Menu
    GtkWidget *tools_menu = gtk_menu_new();
    GtkWidget *tools_item = gtk_menu_item_new_with_mnemonic("_Tools");
    GtkWidget *read_item = gtk_menu_item_new_with_mnemonic("_Read Source");
    GtkWidget *copy_item = gtk_menu_item_new_with_mnemonic("_Copy");
    GtkWidget *burn_item = gtk_menu_item_new_with_mnemonic("_Burn to Destination");
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), read_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), copy_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(tools_menu), burn_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(tools_item), tools_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), tools_item);
    g_signal_connect(read_item, "activate", G_CALLBACK(on_read_source), NULL);
    g_signal_connect(copy_item, "activate", G_CALLBACK(on_copy), NULL);
    g_signal_connect(burn_item, "activate", G_CALLBACK(on_burn), NULL);

    // Help Menu
    GtkWidget *help_menu = gtk_menu_new();
    GtkWidget *help_item = gtk_menu_item_new_with_mnemonic("_Help");
    GtkWidget *about_item = gtk_menu_item_new_with_mnemonic("_About");
    gtk_menu_shell_append(GTK_MENU_SHELL(help_menu), about_item);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(help_item), help_menu);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), help_item);
    g_signal_connect(about_item, "activate", G_CALLBACK(on_about), NULL);

    return menubar;
}

int main(int argc, char *argv[]) {
    gtk_init(&argc, &argv);

    GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "SD Card Cloner");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 400);
    g_signal_connect(window, "destroy", G_CALLBACK(on_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 5);
    GtkWidget *menubar = make_menu_bar(window);

    label_source = gtk_label_new("Source: (not selected)");
    label_dest = gtk_label_new("Destination: (not selected)");
    label_status = gtk_label_new("Status: Idle");
    progress_bar = gtk_progress_bar_new();

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), label_source, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), label_dest, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), label_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), progress_bar, FALSE, FALSE, 5);

    gtk_container_add(GTK_CONTAINER(window), vbox);
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}

