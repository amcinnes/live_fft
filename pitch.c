#include <stdio.h>
#include <string.h>
#include <complex.h>
#include <fftw3.h>
#include <math.h>
#include "pitch.h"

#define K 0.8
#define FFT_SIZE (PITCH_WINDOW_SIZE+PITCH_WINDOW_SIZE/2)

// TODO
// Find out why clarity jumps straight from 0 to about 0.8
// If windows overlap by 75%, surely this shouldn't happen

fftwf_plan plan1, plan2;
float *pitch_in_buffer;
fftwf_complex *intermediate_buffer;
float *nsdf_buffer;

void pitch_init() {
	pitch_in_buffer = fftwf_malloc(sizeof(float)*FFT_SIZE);
	for (int i=PITCH_WINDOW_SIZE;i<PITCH_WINDOW_SIZE+PITCH_WINDOW_SIZE/2;i++) {
		pitch_in_buffer[i] = 0;
	}
	intermediate_buffer = fftwf_malloc((FFT_SIZE/2+1)*sizeof(fftw_complex));
	nsdf_buffer = fftwf_malloc(FFT_SIZE*sizeof(float));
	plan1 = fftwf_plan_dft_r2c_1d(FFT_SIZE,pitch_in_buffer,intermediate_buffer,FFTW_ESTIMATE);
	plan2 = fftwf_plan_dft_c2r_1d(FFT_SIZE,intermediate_buffer,nsdf_buffer,FFTW_ESTIMATE);
}

int find_max(float *b, int length, float threshold) {
	// First, find the maximum of the whole array
	float max_value = 0;
	for (int i=0;i<length;i++) {
		if (b[i]>max_value) max_value = b[i];
	}
	// Find the first maximum which is within threshold of it
	int max = -1;
	int state = 0;
	for (int i=0;i<length;i++) {
		if (state==0) {
			if (b[i]<0) state = 1;
		} else if (state==1) {
			if (b[i]>=0) {
				state = 2;
				max = -1;
			}
		} else if (state==2) {
			if (max==-1||b[i]>b[max]) {
				// In the paper they check if the point is a local maximum too, I think
				// I don't do that.
				max = i;
			}
			if (b[i]<0||i==length-1) {
				if (b[max]>=threshold*max_value) {
					return max;
				}
				state = 1;
			}
		}
	}
	return -1;
}

void pitch_calculate(float *pitch, float *clarity) {
	// First we want to calculate autocorrelation
	// result[tau] = sum from j=0 up to j<W, of buffer[j]*buffer[j+tau]
	// tau goes from 0 to W/2

	// FFT
	fftwf_execute(plan1);
	// We will get some complex coefficients. Multiply each by its conjugate
	for (int i=0;i<FFT_SIZE/2+1;i++) {
		intermediate_buffer[i] *= conj(intermediate_buffer[i]);
		intermediate_buffer[i] /= FFT_SIZE;
	}
	// Inverse FFT
	fftwf_execute(plan2);
	// Now we have the autocorrelation

	// Now we want to calculate m
	// m[tau] = sum from j=0 to j<W of buffer[j]^2, + sum from j=tau to j<W+tau of buffer[j]^2
	float m = 2*nsdf_buffer[0];
	for (int i=0;i<PITCH_WINDOW_SIZE/2;i++) {
		nsdf_buffer[i] = 2*nsdf_buffer[i]/m;
		m -= pitch_in_buffer[i]*pitch_in_buffer[i];
	}

	// Ok, now we have the nsdf
	int max = find_max(nsdf_buffer,PITCH_WINDOW_SIZE/2,K);
	if (max==-1||max==PITCH_WINDOW_SIZE/2-1) {
		// We ignore the PITCH_WINDOW_SIZE/2-1 case because it's probably spurious
		// and because we can't do parabolic interpolation on it
		*pitch = 0;
		*clarity = 0;
	} else {
		// parabolic interpolation
		float y = nsdf_buffer[max];
		float yl = nsdf_buffer[max-1];
		float yr = nsdf_buffer[max+1];
		float estimated_max = max + (yl-yr)/(2*(yl+yr-2*y));
		*clarity = y-(yl-yr)*(yl-yr)/(8*(yl+yr-2*y));
		*pitch = SAMPLE_RATE/estimated_max;
	}
}
