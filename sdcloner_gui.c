// sdcloner_gui.c
// GTK3 GUI frontend for SD Cloner Engine (block-device aware selectors)
// License: GPLv3

#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>
#include "sdcloner_engine.h"

typedef struct {
    GtkWidget *win;
    GtkWidget *label_source;
    GtkWidget *label_dest;
    GtkWidget *label_status;
    GtkWidget *progress;
    gchar     *source_dev;     // e.g., "/dev/sdd"
    gchar     *dest_dev;       // e.g., "/dev/sdb"
    gchar     *image_path;     // selected via File->Open Image...
    gboolean   busy;
    pthread_t  worker;
} App;

static void set_status(App *app, const char *msg) {
    gtk_label_set_text(GTK_LABEL(app->label_status), msg);
}

static void set_progress_busy(App *app, gboolean busy) {
    app->busy = busy;
    if (busy) {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress), "Working...");
    } else {
        gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 0.0);
        gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app->progress), "Idle");
    }
}

static gboolean pulse_cb(gpointer data) {
    App *app = (App*)data;
    if (app->busy) {
        gtk_progress_bar_pulse(GTK_PROGRESS_BAR(app->progress));
    }
    return TRUE; // keep timer
}

// ---------- Helpers: block-device listing & validation ----------
static char* lsblk_list(void) {
    const char *cmd =
        "lsblk -pnro NAME,SIZE,MODEL,TYPE,RM | awk '$4==\"disk\"{print $1\"|\"$2\"|\"$3\"|\"$4\"|\"$5}'";
    FILE *fp = popen(cmd, "r");
    if (!fp) return NULL;
    char *buf = NULL; size_t cap = 0, len = 0;
    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
        size_t add = strlen(line);
        if (len + add + 1 > cap) {
            size_t ncap = cap ? cap*2 : 8192;
            while (ncap < len + add + 1) ncap *= 2;
            buf = (char*)realloc(buf, ncap);
            cap = ncap;
        }
        memcpy(buf + len, line, add);
        len += add;
        buf[len] = '\0';
    }
    pclose(fp);
    return buf; // caller free()
}

static gboolean is_block_device(const char *path) {
    struct stat st;
    if (stat(path, &st) != 0) return FALSE;
    return S_ISBLK(st.st_mode);
}

// Returns g_strdup() of selected block device path (or NULL)
static char* pick_block_device(GtkWindow *parent, const char *title) {
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        title, parent, GTK_DIALOG_MODAL,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Select", GTK_RESPONSE_ACCEPT, NULL);

    GtkWidget *content = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget *scroll = gtk_scrolled_window_new(NULL, NULL);
    gtk_widget_set_size_request(scroll, 640, 300);
    gtk_container_add(GTK_CONTAINER(content), scroll);

    GtkListStore *store = gtk_list_store_new(5, G_TYPE_STRING, G_TYPE_STRING,
                                                G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING);
    char *txt = lsblk_list();
    if (txt && *txt) {
        char *save=NULL;
        for (char *line=strtok_r(txt, "\n", &save); line; line=strtok_r(NULL, "\n", &save)) {
            char *name=strtok(line, "|");
            char *size=strtok(NULL, "|");
            char *model=strtok(NULL, "|");
            char *type=strtok(NULL, "|");
            char *rm  =strtok(NULL, "|");
            if (!name||!size||!type) continue;
            GtkTreeIter it;
            gtk_list_store_append(store, &it);
            gtk_list_store_set(store, &it,
                               0, name,
                               1, size,
                               2, model ? model : "",
                               3, type,
                               4, rm ? rm : "0", -1);
        }
    }
    free(txt);

    GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
    g_object_unref(store);

    GtkCellRenderer *r = gtk_cell_renderer_text_new();
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Device", r, "text", 0, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Size",   r, "text", 1, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Model",  r, "text", 2, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "Type",   r, "text", 3, NULL);
    gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view), -1, "RM",     r, "text", 4, NULL);

    gtk_container_add(GTK_CONTAINER(scroll), view);
    gtk_widget_show_all(dlg);

    char *result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        GtkTreeSelection *sel = gtk_tree_view_get_selection(GTK_TREE_VIEW(view));
        GtkTreeModel *model; GtkTreeIter iter;
        if (gtk_tree_selection_get_selected(sel, &model, &iter)) {
            gchar *dev=NULL; gtk_tree_model_get(model, &iter, 0, &dev, -1);
            if (dev && is_block_device(dev)) result = g_strdup(dev);
            g_free(dev);
        }
    }
    gtk_widget_destroy(dlg);
    return result; // g_free() by caller
}

// ---------------- File → Open Image... ----------------
static void on_open_image(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    GtkWidget *dlg = gtk_file_chooser_dialog_new(
        "Open Image (.img or .img.gz)",
        GTK_WINDOW(app->win),
        GTK_FILE_CHOOSER_ACTION_OPEN,
        "_Cancel", GTK_RESPONSE_CANCEL,
        "_Open", GTK_RESPONSE_ACCEPT,
        NULL);

    GtkFileFilter *flt = gtk_file_filter_new();
    gtk_file_filter_set_name(flt, "Disk Images");
    gtk_file_filter_add_pattern(flt, "*.img");
    gtk_file_filter_add_pattern(flt, "*.img.gz");
    gtk_file_chooser_add_filter(GTK_FILE_CHOOSER(dlg), flt);

    if (gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT) {
        char *path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dlg));
        g_free(app->image_path);
        app->image_path = g_strdup(path);
        gchar *msg = g_strdup_printf("Image selected: %s", path);
        set_status(app, msg);
        g_free(msg);
        g_free(path);
    }
    gtk_widget_destroy(dlg);
}

// --------------- Tools → Select Source ----------------
static void on_select_source(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    char *choice = pick_block_device(GTK_WINDOW(app->win), "Select Source Block Device");
    if (!choice) { set_status(app, "Source selection canceled."); return; }
    if (!is_block_device(choice)) { set_status(app, "Not a block device."); g_free(choice); return; }
    g_free(app->source_dev);
    app->source_dev = g_strdup(choice);
    gtk_label_set_text(GTK_LABEL(app->label_source), app->source_dev);
    gchar *msg = g_strdup_printf("Source set to %s", app->source_dev);
    set_status(app, msg);
    g_free(msg);
    g_free(choice);
}

// --------------- Tools → Select Destination -----------
static void on_select_dest(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    char *choice = pick_block_device(GTK_WINDOW(app->win), "Select Destination Block Device");
    if (!choice) { set_status(app, "Destination selection canceled."); return; }
    if (!is_block_device(choice)) { set_status(app, "Not a block device."); g_free(choice); return; }
    g_free(app->dest_dev);
    app->dest_dev = g_strdup(choice);
    gtk_label_set_text(GTK_LABEL(app->label_dest), app->dest_dev);
    gchar *msg = g_strdup_printf("Destination set to %s", app->dest_dev);
    set_status(app, msg);
    g_free(msg);
    g_free(choice);
}

// ---------------- Background job helpers --------------
typedef struct { App *app; } JobCtx;

static gboolean ui_done_ok(gpointer data) {
    JobCtx *jc = (JobCtx*)data;
    set_status(jc->app, "Operation completed successfully.");
    set_progress_busy(jc->app, FALSE);
    free(jc);
    return FALSE;
}
static gboolean ui_done_fail(gpointer data) {
    JobCtx *jc = (JobCtx*)data;
    set_status(jc->app, "Operation failed (see terminal logs).");
    set_progress_busy(jc->app, FALSE);
    free(jc);
    return FALSE;
}

// ---------------- Tools → Read Source -----------------
static void* worker_read(void *arg) {
    JobCtx *jc = (JobCtx*)arg;
    App *app = jc->app;
    int rc = sdcloner_clone(app->source_dev, NULL, 0);
    g_idle_add(rc==0 ? ui_done_ok : ui_done_fail, jc);
    return NULL;
}

static void on_read_source(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    if (app->busy) return;
    if (!app->source_dev) { set_status(app, "Please select a source device first."); return; }
    set_status(app, "Reading source to local image...");
    set_progress_busy(app, TRUE);
    JobCtx *jc = (JobCtx*)calloc(1,sizeof(JobCtx));
    jc->app = app;
    pthread_create(&app->worker, NULL, worker_read, jc);
    pthread_detach(app->worker);
}

// --------------- Tools → Burn to Destination ----------
static void* worker_burn(void *arg) {
    JobCtx *jc = (JobCtx*)arg;
    App *app = jc->app;
    int rc = -1;
    if (app->image_path && app->dest_dev) {
        rc = burn_image_to_disk(app->image_path, app->dest_dev);
    } else if (app->source_dev && app->dest_dev) {
        rc = sdcloner_clone(app->source_dev, app->dest_dev, 0);
    }
    g_idle_add(rc==0 ? ui_done_ok : ui_done_fail, jc);
    return NULL;
}

static void on_burn_dest(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    if (app->busy) return;
    if (!app->dest_dev) { set_status(app, "Please select a destination device."); return; }
    if (!app->image_path && !app->source_dev) {
        set_status(app, "Load an image or select a source.");
        return;
    }
    set_status(app, "Burning to destination...");
    set_progress_busy(app, TRUE);
    JobCtx *jc = (JobCtx*)calloc(1,sizeof(JobCtx));
    jc->app = app;
    pthread_create(&app->worker, NULL, worker_burn, jc);
    pthread_detach(app->worker);
}

// ---------------- Help → About ------------------------
static void on_about(GtkWidget *w, gpointer user) {
    App *app = (App*)user;
    GtkWidget *dlg = gtk_dialog_new_with_buttons(
        "About SD Cloner", GTK_WINDOW(app->win), GTK_DIALOG_MODAL,
        "_Close", GTK_RESPONSE_CLOSE, NULL);

    GtkWidget *nb = gtk_notebook_new();

    GtkWidget *tab1 = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tab1), FALSE);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab1)),
        "Author: Dr. Eric Oliver Flores\n"
        "Date: 10/25\n"
        "Version: 1.0\n"
        "License: GPLv3\n\n"
        "© 2025 Dr. Eric O. Flores",
        -1);

    GtkWidget *tab2 = gtk_text_view_new();
    gtk_text_view_set_editable(GTK_TEXT_VIEW(tab2), FALSE);
    gtk_text_buffer_set_text(
        gtk_text_view_get_buffer(GTK_TEXT_VIEW(tab2)),
        "Technologies Used:\n"
        "- C (C17)\n"
        "- GTK 3 (GLib)\n"
        "- dd, gzip, parted, rsync, losetup, lsblk, blkid\n"
        "- Pop!_OS / Ubuntu 22.04\n",
        -1);

    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tab1, gtk_label_new("About"));
    gtk_notebook_append_page(GTK_NOTEBOOK(nb), tab2, gtk_label_new("Technologies"));

    GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_pack_start(GTK_BOX(box), nb, TRUE, TRUE, 6);
    gtk_widget_show_all(dlg);
    g_signal_connect_swapped(dlg, "response", G_CALLBACK(gtk_widget_destroy), dlg);
}

// ---------------- Menu bar ----------------------------
static GtkWidget* build_menubar(App *app) {
    GtkAccelGroup *accel = gtk_accel_group_new();
    gtk_window_add_accel_group(GTK_WINDOW(app->win), accel);

    GtkWidget *menubar = gtk_menu_bar_new();

    // File
    GtkWidget *m_file = gtk_menu_new();
    GtkWidget *i_file = gtk_menu_item_new_with_mnemonic("_File");
    GtkWidget *i_open = gtk_menu_item_new_with_mnemonic("_Open Image...");
    GtkWidget *i_quit = gtk_menu_item_new_with_mnemonic("_Quit");
    gtk_menu_shell_append(GTK_MENU_SHELL(m_file), i_open);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_file), i_quit);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(i_file), m_file);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), i_file);
    g_signal_connect(i_open, "activate", G_CALLBACK(on_open_image), app);
    g_signal_connect(i_quit, "activate", G_CALLBACK(gtk_main_quit), NULL);
    gtk_widget_add_accelerator(i_quit, "activate", accel, GDK_KEY_q, GDK_CONTROL_MASK, GTK_ACCEL_VISIBLE);

    // Tools
    GtkWidget *m_tools = gtk_menu_new();
    GtkWidget *i_tools = gtk_menu_item_new_with_mnemonic("_Tools");
    GtkWidget *i_sel_src = gtk_menu_item_new_with_mnemonic("Select _Source...");
    GtkWidget *i_sel_dst = gtk_menu_item_new_with_mnemonic("Select _Destination...");
    GtkWidget *i_read    = gtk_menu_item_new_with_mnemonic("_Read Source");
    GtkWidget *i_burn    = gtk_menu_item_new_with_mnemonic("_Burn to Destination");
    gtk_menu_shell_append(GTK_MENU_SHELL(m_tools), i_sel_src);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_tools), i_sel_dst);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_tools), i_read);
    gtk_menu_shell_append(GTK_MENU_SHELL(m_tools), i_burn);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(i_tools), m_tools);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), i_tools);
    g_signal_connect(i_sel_src, "activate", G_CALLBACK(on_select_source), app);
    g_signal_connect(i_sel_dst, "activate", G_CALLBACK(on_select_dest),  app);
    g_signal_connect(i_read,    "activate", G_CALLBACK(on_read_source),  app);
    g_signal_connect(i_burn,    "activate", G_CALLBACK(on_burn_dest),    app);

    // Help
    GtkWidget *m_help = gtk_menu_new();
    GtkWidget *i_help = gtk_menu_item_new_with_mnemonic("_Help");
    GtkWidget *i_about= gtk_menu_item_new_with_mnemonic("_About");
    gtk_menu_shell_append(GTK_MENU_SHELL(m_help), i_about);
    gtk_menu_item_set_submenu(GTK_MENU_ITEM(i_help), m_help);
    gtk_menu_shell_append(GTK_MENU_SHELL(menubar), i_help);
    g_signal_connect(i_about, "activate", G_CALLBACK(on_about), app);

    return menubar;
}

// ---------------- Main window -------------------------
int main(int argc, char **argv) {
    gtk_init(&argc, &argv);

    App app = {0};
    app.win = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(app.win), "SD Card Cloner (GUI)");
    gtk_window_set_default_size(GTK_WINDOW(app.win), 760, 460);
    g_signal_connect(app.win, "destroy", G_CALLBACK(gtk_main_quit), NULL);

    GtkWidget *vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *menubar = build_menubar(&app);

    app.label_source = gtk_label_new("Source: (not selected)");
    app.label_dest   = gtk_label_new("Destination: (not selected)");
    app.label_status = gtk_label_new("Status: Idle");
    app.progress     = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app.progress), TRUE);
    gtk_progress_bar_set_text(GTK_PROGRESS_BAR(app.progress), "Idle");

    gtk_box_pack_start(GTK_BOX(vbox), menubar, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.label_source, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.label_dest,   FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.label_status, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(vbox), app.progress,     FALSE, FALSE, 6);

    gtk_container_add(GTK_CONTAINER(app.win), vbox);
    gtk_widget_show_all(app.win);

    // progress pulser
    g_timeout_add(200, pulse_cb, &app);

    gtk_main();

    g_free(app.source_dev);
    g_free(app.dest_dev);
    g_free(app.image_path);
    return 0;
}
