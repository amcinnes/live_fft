#ifndef _PITCH_H
#define _PITCH_H

#define PITCH_WINDOW_SIZE 4096
#define SAMPLE_RATE 48000

extern float *pitch_in_buffer;
void pitch_init(void);
void pitch_calculate(float *pitch, float *clarity);

#endif
