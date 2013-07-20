#define SAMPLE_RATE 48000

#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>

int main(int argc, char **argv) {
	gtk_init(&argc,&argv);
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_widget_show(window);
	g_signal_connect(window,"destroy",gtk_main_quit,NULL);

	pa_glib_mainloop *pgm = pa_glib_mainloop_new(NULL);
	pa_mainloop_api *pma = pa_glib_mainloop_get_api(pgm);
	pa_context *ctx = pa_context_new(pma,"live FFT");
	struct pa_sample_spec ss;
	ss.format = PA_SAMPLE_S16LE;
	ss.rate = SAMPLE_RATE;
	ss.channels = 1;
	struct pa_channel_map map;
	map.channels = 1;
	map.map[0] = PA_CHANNEL_POSITION_MONO;
	pa_stream_new(ctx,"capture",&ss,&map);

	gtk_main();
}
