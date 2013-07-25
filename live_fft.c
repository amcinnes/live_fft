#include <string.h>
#include <math.h>
#include <complex.h>
#include <gtk/gtk.h>
#include <pulse/pulseaudio.h>
#include <pulse/glib-mainloop.h>
#include <fftw3.h>
#include "pitch.h"

// SAMPLE_RATE and PITCH_WINDOW_SIZE are, for the moment, in pitch.h

// #define SAMPLE_RATE 48000
#define FFT_WINDOW_SIZE 4800
#define FFT_WINDOW_SPACING 4800

// #define PITCH_WINDOW_SIZE 4096
#define PITCH_WINDOW_SPACING 1024
#define PITCH_POINTS 1000

#define PITCH_GRID_TYPE 3
#define PITCH_GRID 50
#define PITCH_MIN 50
#define PITCH_MAX 400
#define PITCH_LOGARITHMIC 1

#define H_LOGARITHMIC 0
#define H_GRID_TYPE 1 // 0 = no grid. 1 = linear grid. 2 = fixed logarithmic grid. 3 = piano keys
#define H_MIN 0
#define H_MAX 24000 // Hz
#define H_GRID 1000

#define V_LOGARITHMIC 1
#define V_SHOW_GRID 1
#define V_MIN -0.0001
#define V_MAX 0.001
#define V_GRID 0.0001
#define V_LOG_MAX 0 // dBFS
#define V_LOG_MIN -120
#define V_LOG_GRID 10

#define MARGIN 4

#define _XOPEN_SOURCE

// TODO there are lots of return codes we should check & possible errors we
// should handle

// TODO a configuration interface

enum {
	MODE_FFT = 0,
	MODE_PITCH = 1
};

int mode;
float *window_buffer;
int in_pos;
int window_size, window_spacing;

fftwf_plan plan;
float *fft_in_buffer;
fftwf_complex *fft_out_buffer;
int fft_valid;
GtkWidget *fft_da;

float clarity[PITCH_POINTS];
float pitch[PITCH_POINTS];
GtkWidget *pitch_da;

void fft_process_window() {
	fftwf_execute(plan);
	fft_valid = 1;
	gtk_widget_queue_draw(GTK_WIDGET(fft_da));
}

void pitch_process_window() {
	memmove(clarity,clarity+1,(PITCH_POINTS-1)*sizeof(float));
	memmove(pitch,pitch+1,(PITCH_POINTS-1)*sizeof(float));
	pitch_calculate(&pitch[PITCH_POINTS-1],&clarity[PITCH_POINTS-1]);
	gtk_widget_queue_draw(GTK_WIDGET(pitch_da));
}

void audio_read_callback(pa_stream *stream, size_t n, void *dummy) {
	size_t nread;
	const void *data;
	pa_stream_peek(stream,&data,&nread);

	int stream_pos = 0;
	while (stream_pos<nread) {
		int space_in_window = window_size*sizeof(float)-in_pos;
		int data_in_buffer = nread-stream_pos;
		int to_copy = data_in_buffer>space_in_window?space_in_window:data_in_buffer;
		if (in_pos<0) {
			if (in_pos+to_copy>0) {
				memcpy(window_buffer,data+stream_pos-in_pos,to_copy+in_pos);
			}
		} else {
			memcpy(((void *)window_buffer)+in_pos,data+stream_pos,to_copy);
		}
		stream_pos += to_copy;
		in_pos += to_copy;
		if (in_pos==window_size*sizeof(float)) {
			switch (mode) {
			case MODE_FFT:
				fft_process_window();
				break;
			case MODE_PITCH:
				pitch_process_window();
				break;
			}
			if (window_spacing<window_size) {
				memmove(window_buffer,window_buffer+window_spacing,(window_size-window_spacing)*sizeof(float));
			}
			in_pos -= window_spacing*sizeof(float);
		}
	}

	if (nread>0) pa_stream_drop(stream);
}

void audio_connected_callback(pa_context *ctx, void *window) {
	pa_context_state_t state = pa_context_get_state(ctx);
	if (state==PA_CONTEXT_READY) {
		// Create pulseaudio stream
		struct pa_sample_spec ss;
		ss.format = PA_SAMPLE_FLOAT32NE;
		ss.rate = SAMPLE_RATE;
		ss.channels = 1;
		struct pa_channel_map map;
		map.channels = 1;
		map.map[0] = PA_CHANNEL_POSITION_MONO;
		pa_stream *stream = pa_stream_new(ctx,"capture",&ss,&map);
		struct pa_buffer_attr attr;
		pa_stream_set_read_callback(stream,audio_read_callback,window);
		attr.maxlength = FFT_WINDOW_SIZE*sizeof(float);
		attr.fragsize = FFT_WINDOW_SIZE*sizeof(float);
		pa_stream_connect_record(stream,NULL,&attr,PA_STREAM_ADJUST_LATENCY);
	}
}

void hline(cairo_t *cr, int width, int height, double y, char *label) {
	char buf[20];
	if (label==NULL) {
		snprintf(buf,20,"%.0lf",y);
		label = buf;
	}
	double py;
	if (V_LOGARITHMIC) {
		py = height*(1-(y-V_LOG_MIN)/((double)V_LOG_MAX-V_LOG_MIN));
	} else {
		py = height*(1-(y-V_MIN)/(V_MAX-V_MIN));
	}
	cairo_move_to(cr,0,py);
	cairo_line_to(cr,width,py);
	cairo_stroke(cr);

	// Draw label
	cairo_text_extents_t extents;
	cairo_text_extents(cr,label,&extents);
	double baseline = py+3;
	double tx = 5;
	double top = baseline+extents.y_bearing;
	cairo_set_source_rgb(cr,1,1,1);
	cairo_rectangle(cr,tx+extents.x_bearing-MARGIN,top-MARGIN,extents.width+2*MARGIN,extents.height+2*MARGIN);
	cairo_fill(cr);
	cairo_set_source_rgb(cr,0,0,0);
	cairo_move_to(cr,tx,baseline);
	cairo_show_text(cr,label);
}

void draw_v_grid(cairo_t *cr, int width, int height) {
	cairo_set_source_rgb(cr,0,0,0);
	cairo_set_line_width(cr,1);
	if (V_SHOW_GRID) {
		if (V_LOGARITHMIC) {
			int first_line = floor(V_LOG_MIN/(double)V_LOG_GRID);
			int last_line = ceil(V_LOG_MAX/(double)V_LOG_GRID);
			for (int i=first_line;i<=last_line;i++) {
				hline(cr,width,height,i*V_LOG_GRID,NULL);
			}
		} else {
			int first_line = floor(V_MIN/(double)V_GRID);
			int last_line = ceil(V_MAX/(double)V_GRID);
			for (int i=first_line;i<=last_line;i++) {
				hline(cr,width,height,i*V_GRID,NULL);
			}
		}
	}
}

void vline(cairo_t *cr, int width, int height, double x, char *label) {
	char buf[20];
	if (label==NULL) {
		snprintf(buf,20,"%.0lf",x);
		label = buf;
	}
	double px;
	if (H_LOGARITHMIC) {
		px = width*(log(x)-log(H_MIN))/(log(H_MAX)-log(H_MIN));
	} else {
		px = width*(x-H_MIN)/(H_MAX-H_MIN);
	}
	cairo_move_to(cr,px,0);
	cairo_line_to(cr,px,height);
	cairo_stroke(cr);
	// Draw label
	double ty = height-10;
	cairo_text_extents_t extents;
	cairo_text_extents(cr,label,&extents);
	double top = ty+extents.y_bearing;
	cairo_set_source_rgb(cr,1,1,1);
	cairo_rectangle(cr,px-extents.width/2-MARGIN,top-MARGIN,extents.width+2*MARGIN,extents.height+2*MARGIN);
	cairo_fill(cr);
	cairo_set_source_rgb(cr,0,0,0);
	cairo_move_to(cr,px-extents.width/2-extents.x_bearing,ty);
	cairo_show_text(cr,label);
}

const char *notename[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

void draw_h_grid(cairo_t *cr, int width, int height) {
	cairo_set_source_rgb(cr,0,0,0);
	cairo_set_line_width(cr,1);
	int first_line, last_line;
	switch (H_GRID_TYPE) {
	case 0:
		// No grid
		break;
	case 1:
		// Linear grid
		first_line = floor(H_MIN/(double)H_GRID);
		last_line = ceil(H_MAX/(double)H_GRID);
		for (int i=first_line;i<=last_line;i++) {
			vline(cr,width,height,i*H_GRID,NULL);
		}
		break;
	case 2:
		// Fixed logarithmic grid
		// Grid lines at 1, 2, 3, 5, 10, etc up to 50000
		for (int i=1;i<100000;i*=10) {
			vline(cr,width,height,i,NULL);
			vline(cr,width,height,i*2,NULL);
			vline(cr,width,height,i*3,NULL);
			vline(cr,width,height,i*5,NULL);
		}
		break;
	case 3:
		// Piano keys
		for (int i=0;i<128;i++) {
			double freq = 440 * pow(2,(i-69)/12.);
			char buf[20];
			snprintf(buf,20,"%s%d",notename[i%12],i/12-1);
			vline(cr,width,height,freq,buf);
		}
		break;
	}
}

gboolean draw_fft(GtkWidget *window, cairo_t *cr, gpointer dummy) {
	int width = gtk_widget_get_allocated_width(window);
	int height = gtk_widget_get_allocated_height(window);
	// Clear window to white
	cairo_set_source_rgb(cr,1,1,1);
	cairo_paint(cr);
	// Draw grids
	draw_v_grid(cr,width,height);
	draw_h_grid(cr,width,height);

	if (fft_valid) {
		// Work out which points of the FFT we need
		// The first point is DC, 0Hz.
		// Each point increases the frequency by SAMPLE_RATE/FFT_WINDOW_SIZE Hz.
		// The last point is point FFT_WINDOW_SIZE/2, and is at the Nyquist freq, SAMPLE_RATE/2.
		int first_point = H_MIN/(SAMPLE_RATE/(double)FFT_WINDOW_SIZE)-1;
		int last_point = H_MAX/(SAMPLE_RATE/(double)FFT_WINDOW_SIZE)+1;
		if (H_LOGARITHMIC) {
			if (first_point<1) first_point = 1;
		} else {
			if (first_point<0) first_point = 0;
		}
		if (last_point>FFT_WINDOW_SIZE/2) last_point = FFT_WINDOW_SIZE/2;
		// Plot FFT
		for (int i=first_point;i<=last_point;i++) {
			double freq = i*SAMPLE_RATE/(double)FFT_WINDOW_SIZE;
			double power = pow(cabs(fft_out_buffer[i])/FFT_WINDOW_SIZE,2)*2;
			double x,y;
			if (H_LOGARITHMIC) {
				x = (log(freq)-log(H_MIN))/(log(H_MAX)-log(H_MIN))*width;
			} else {
				x = (freq-H_MIN)/(H_MAX-H_MIN)*width;
			}
			if (V_LOGARITHMIC) {
				double db = 10*log(power)/log(10);
				y = height * (1-(db-V_LOG_MIN)/(V_LOG_MAX-V_LOG_MIN));
			} else {
				y = height * (1-(power-V_MIN)/(V_MAX-V_MIN));
			}
			if (i==first_point) cairo_move_to(cr,x,y);
			else cairo_line_to(cr,x,y);
		}
		cairo_set_source_rgb(cr,0,0,1);
		cairo_set_line_width(cr,2);
		cairo_stroke(cr);
	}
	return FALSE;
}

void pitchline(cairo_t *cr, int width, int height, double y, char *label) {
	char buf[20];
	if (label==NULL) {
		snprintf(buf,20,"%.0lf",y);
		label = buf;
	}
	double py;
	if (PITCH_LOGARITHMIC) {
		py = height*(1-(log(y)-log(PITCH_MIN))/(log(PITCH_MAX)-log(PITCH_MIN)));
	} else {
		py = height*(1-(y-PITCH_MIN)/(PITCH_MAX-PITCH_MIN));
	}
	cairo_move_to(cr,0,py);
	cairo_line_to(cr,width,py);
	cairo_stroke(cr);

	// Draw label
	cairo_text_extents_t extents;
	cairo_text_extents(cr,label,&extents);
	double baseline = py+3;
	double tx = 5;
	double top = baseline+extents.y_bearing;
	cairo_set_source_rgb(cr,1,1,1);
	cairo_rectangle(cr,tx+extents.x_bearing-MARGIN,top-MARGIN,extents.width+2*MARGIN,extents.height+2*MARGIN);
	cairo_fill(cr);
	cairo_set_source_rgb(cr,0,0,0);
	cairo_move_to(cr,tx,baseline);
	cairo_show_text(cr,label);

	tx = width - extents.width - 5;
	cairo_set_source_rgb(cr,1,1,1);
	cairo_rectangle(cr,tx+extents.x_bearing-MARGIN,top-MARGIN,extents.width+2*MARGIN,extents.height+2*MARGIN);
	cairo_fill(cr);
	cairo_set_source_rgb(cr,0,0,0);
	cairo_move_to(cr,tx,baseline);
	cairo_show_text(cr,label);
}

void draw_pitch_grid(cairo_t *cr, int width, int height) {
	cairo_set_source_rgb(cr,0,0,0);
	cairo_set_line_width(cr,1);
	int first_line, last_line;
	switch (PITCH_GRID_TYPE) {
	case 0:
		// No grid
		break;
	case 1:
		// Linear grid
		first_line = floor(PITCH_MIN/(double)PITCH_GRID);
		last_line = ceil(PITCH_MAX/(double)PITCH_GRID);
		for (int i=first_line;i<=last_line;i++) {
			pitchline(cr,width,height,i*PITCH_GRID,NULL);
		}
		break;
	case 2:
		// Fixed logarithmic grid
		// Grid lines at 1, 2, 3, 5, 10, etc up to 50000
		for (int i=1;i<100000;i*=10) {
			pitchline(cr,width,height,i,NULL);
			pitchline(cr,width,height,i*2,NULL);
			pitchline(cr,width,height,i*3,NULL);
			pitchline(cr,width,height,i*5,NULL);
		}
		break;
	case 3:
		// Piano keys
		for (int i=0;i<128;i++) {
			double freq = 440 * pow(2,(i-69)/12.);
			char buf[20];
			snprintf(buf,20,"%s%d",notename[i%12],i/12-1);
			pitchline(cr,width,height,freq,buf);
		}
		break;
	}
}

gboolean draw_pitch(GtkWidget *window, cairo_t *cr, gpointer dummy) {
	int width = gtk_widget_get_allocated_width(window);
	int height = gtk_widget_get_allocated_height(window);
	// Clear window to white
	cairo_set_source_rgb(cr,1,1,1);
	cairo_paint(cr);
	draw_pitch_grid(cr,width,height);
	int line_exists = 0;
	for (int i=0;i<PITCH_POINTS;i++) {
		double x = width * i/(double)PITCH_POINTS;
		double y;
		if (PITCH_LOGARITHMIC) {
			y = height*(1-(log(pitch[i])-log(PITCH_MIN))/(log(PITCH_MAX)-log(PITCH_MIN)));
		} else {
			y = height*(1-(pitch[i]-PITCH_MIN)/(PITCH_MAX-PITCH_MIN));
		}
		if (clarity[i]>0 && y>0 && y<height) {
			if (line_exists) cairo_line_to(cr,x,y);
			else cairo_move_to(cr,x,y);
			line_exists = 1;
		} else {
			line_exists = 0;
		}
	}
	cairo_set_source_rgb(cr,0,0,1);
	cairo_set_line_width(cr,2);
	cairo_stroke(cr);
	return FALSE;
}

void switch_tab(GtkNotebook *tabs, GtkWidget *page, guint page_num, gpointer dummy) {
	// First, clear data for the current mode
	switch (mode) {
	case MODE_FFT:
		fft_valid = 0;
		break;
	case MODE_PITCH:
		for (int i=0;i<PITCH_POINTS;i++) {
			clarity[i] = 0;
		}
		break;
	}
	// Then, set new mode, with window spacing and size
	mode = page_num;
	switch (mode) {
	case MODE_FFT:
		window_size = FFT_WINDOW_SIZE;
		window_spacing = FFT_WINDOW_SPACING;
		window_buffer = fft_in_buffer;
		in_pos = 0;
		break;
	case MODE_PITCH:
		window_size = PITCH_WINDOW_SIZE;
		window_spacing = PITCH_WINDOW_SPACING;
		window_buffer = pitch_in_buffer;
		in_pos = 0;
		break;
	}
	in_pos = 0;
}

int main(int argc, char **argv) {
	gtk_init(&argc,&argv);

	// Create window and stuff
	GtkWidget *window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(window),"Live spectrum display");
	gtk_window_set_default_size(GTK_WINDOW(window),640,480);
	g_signal_connect(window,"destroy",gtk_main_quit,NULL);
	GtkWidget *tabs = gtk_notebook_new();
	g_signal_connect(tabs,"switch-page",G_CALLBACK(switch_tab),NULL);
	gtk_container_add(GTK_CONTAINER(window),tabs);
	// Note: the order of pages here has to match the order of MODE constants
	fft_da = gtk_drawing_area_new();
	g_signal_connect(fft_da,"draw",G_CALLBACK(draw_fft),NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabs),fft_da,NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(tabs),fft_da,"Spectrum");
	pitch_da = gtk_drawing_area_new();
	g_signal_connect(pitch_da,"draw",G_CALLBACK(draw_pitch),NULL);
	gtk_notebook_append_page(GTK_NOTEBOOK(tabs),pitch_da,NULL);
	gtk_notebook_set_tab_label_text(GTK_NOTEBOOK(tabs),pitch_da,"Pitch");
	gtk_widget_show_all(window);

	// Create pulseaudio context
	pa_glib_mainloop *pgm = pa_glib_mainloop_new(NULL);
	pa_mainloop_api *pma = pa_glib_mainloop_get_api(pgm);
	pa_context *ctx = pa_context_new(pma,"Live spectrum");
	pa_context_set_state_callback(ctx,audio_connected_callback,fft_da);
	pa_context_connect(ctx,NULL,0,NULL);

	// Set up FFT
	fft_out_buffer = fftwf_malloc(sizeof(fftwf_complex)*(FFT_WINDOW_SIZE/2+1));
	fft_in_buffer = fftwf_malloc(sizeof(float)*FFT_WINDOW_SIZE);
	plan = fftwf_plan_dft_r2c_1d(FFT_WINDOW_SIZE,fft_in_buffer,fft_out_buffer,FFTW_ESTIMATE);

	// Set up pitch thing
	pitch_init();

	// Set up window and mode
	mode = MODE_FFT;
	window_size = FFT_WINDOW_SIZE;
	window_spacing = FFT_WINDOW_SPACING;
	window_buffer = fft_in_buffer;

	gtk_main();
}
