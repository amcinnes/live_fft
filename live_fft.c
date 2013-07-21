#define SAMPLE_RATE 48000

// TODO there are lots of return codes we should check & possible errors we
// should handle

/*
TODO features:

Set FFT size
Set sample rate
Set window function
Vertical and horizontal scales:
	Log or linear
	Set range
	Set gridlines
*/

#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <fftw3.h>

void audio_read_callback(pa_stream *stream, size_t n, void *dummy) {
	size_t nread;
	const void *data;
	pa_stream_peek(stream,&data,&nread);
	printf("Callback! %d\n",(int)nread);
	if (nread>0) pa_stream_drop(stream);
}

void audio_connected_callback(pa_context *ctx, void *dummy) {
	pa_context_state_t state = pa_context_get_state(ctx);
	if (state==PA_CONTEXT_READY) {
		// Create pulseaudio stream
		struct pa_sample_spec ss;
		ss.format = PA_SAMPLE_S16LE;
		ss.rate = SAMPLE_RATE;
		ss.channels = 1;
		struct pa_channel_map map;
		map.channels = 1;
		map.map[0] = PA_CHANNEL_POSITION_MONO;
		pa_stream *stream = pa_stream_new(ctx,"capture",&ss,&map);
		pa_stream_set_read_callback(stream,audio_read_callback,NULL);
		pa_stream_connect_record(stream,NULL,NULL,0);
	}
}

gboolean draw(GtkWidget *window, cairo_t *cr, gpointer dummy) {
	// Clear window to white
	cairo_set_source_rgb(cr,1,1,1);
	cairo_paint(cr);
	// Draw a line across it
	cairo_set_source_rgb(cr,0,0,1);
	cairo_move_to(cr,0,100);
	cairo_line_to(cr,100,100);
	cairo_stroke(cr);
	return FALSE;
}

int main(int argc, char **argv) {
	gtk_init(&argc,&argv);
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	g_signal_connect(window,"destroy",gtk_main_quit,NULL);
	g_signal_connect(window,"draw",G_CALLBACK(draw),NULL);
	gtk_widget_set_app_paintable(window,TRUE);
	gtk_widget_show(window);

	// Create pulseaudio context
	pa_glib_mainloop *pgm = pa_glib_mainloop_new(NULL);
	pa_mainloop_api *pma = pa_glib_mainloop_get_api(pgm);
	pa_context *ctx = pa_context_new(pma,"live FFT");
	pa_context_set_state_callback(ctx,audio_connected_callback,NULL);
	pa_context_connect(ctx,NULL,0,NULL);

	gtk_main();
}
