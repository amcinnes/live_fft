#define SAMPLE_RATE 48000

#include <gtk/gtk.h>

int main(int argc, char **argv) {
	gtk_init(&argc,&argv);
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_show(window);
	g_signal_connect(window,"destroy",gtk_main_quit,NULL);
	gtk_main();
}
