/*
noise-repellent -- Noise Reduction LV2

Copyright 2016 Luciano Dato <lucianodato@gmail.com>

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Lesser General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/
*/

/**
* \file stft.c
* \author Luciano Dato
* \brief Contains a very basic STFT transform abstraction
*/

#include <fftw3.h>
#include "spectral_processing.c"

/**
* STFT handling struct.
*/
typedef struct
{
  int fft_size;
  int fft_size_2;
  fftwf_plan forward;
  fftwf_plan backward;
  float* fft_power;
  float* fft_phase;
  float* fft_magnitude;
  int block_size;
  int window_option_input; //Type of input Window for the STFT
  int window_option_output; //Type of output Window for the STFT
  int overlap_factor; //oversampling factor for overlap calculations
  float overlap_scale_factor; //Scaling factor for conserving the final amplitude
  int hop; //Hop size for the STFT
  int input_latency;
  int read_position;
  float* input_window;
  float* output_window;
  float* in_fifo;
  float* out_fifo;
  float* output_accum;
  float* input_fft_buffer;
  float* output_fft_buffer;
} STFT_transform;

//-----------STFT private---------------

void
stft_configure(STFT_transform* self, int block_size, int fft_size,
               int window_option_input, int window_option_output, int overlap_factor)
{
  //self configuration
  self->block_size = block_size;
  self->fft_size = fft_size;
  self->fft_size_2 = self->fft_size/2;
  self->window_option_input = window_option_input;
  self->window_option_output = window_option_output;
  self->overlap_factor = overlap_factor;
  self->hop = self->fft_size/self->overlap_factor;
  self->input_latency = self->fft_size - self->hop;
  self->read_position = self->input_latency;
}

void
stft_zeropad(STFT_transform* self)
{
  int k;
  int number_of_zeros = self->fft_size - self->block_size;

  //This adds zeros at the end. The right way should be an equal amount at the sides
  for(k = 0; k <= number_of_zeros - 1; k++)
  {
    self->input_fft_buffer[self->block_size - 1 + k] = 0.f;
  }
}

void
stft_fft_analysis(STFT_transform* self)
{
  int k;
  //Windowing the frame input values in the center (zero-phasing)
  for (k = 0; k < self->fft_size; k++)
  {
    self->input_fft_buffer[k] *= self->input_window[k];
  }

  //Do transform
  fftwf_execute(self->forward);
}

void
stft_fft_synthesis(STFT_transform* self)
{
  int k;
  //Do inverse transform
  fftwf_execute(self->backward);

  //Normalizing value
  for (k = 0; k < self->fft_size; k++)
  {
    self->input_fft_buffer[k] = self->input_fft_buffer[k]/self->fft_size;
  }

  //Windowing and scaling
  for(k = 0; k < self->fft_size; k++)
  {
    self->input_fft_buffer[k] = (self->output_window[k]*self->input_fft_buffer[k])/(self->overlap_scale_factor * self->overlap_factor);
  }
}

void
stft_ola(STFT_transform* self)
{
  int k;
  
  //Accumulation
  for(k = 0; k < self->block_size; k++)
  {
    self->output_accum[k] += self->input_fft_buffer[k];
  }

  //Output samples up to the hop size
  for (k = 0; k < self->hop; k++)
  {
    self->out_fifo[k] = self->output_accum[k];
  }

  //shift FFT accumulator the hop size
  memmove(self->output_accum, self->output_accum + self->hop,
          self->block_size*sizeof(float));

  //move input FIFO
  for (k = 0; k < self->input_latency; k++)
  {
    self->in_fifo[k] = self->in_fifo[k+self->hop];
  }
}

void
stft_analysis(STFT_transform* self)
{
  if(self->block_size < self->fft_size)
  {
    stft_zeropad(self);
  }
  
  stft_fft_analysis(self);

  get_info_from_bins(self->fft_power, self->fft_magnitude,
                     self->fft_phase, self->fft_size_2, 
                     self->fft_size, self->output_fft_buffer);
}

void
stft_processing(STFT_transform* self)
{
  return;
}

void
stft_synthesis(STFT_transform* self)
{
  stft_fft_synthesis(self);

  stft_ola(self);
}


//-----------STFT public---------------

void
stft_reset(STFT_transform* self)
{
  //Reset all arrays
  initialize_array(self->input_fft_buffer,0.f,self->fft_size);
  initialize_array(self->output_fft_buffer,0.f,self->fft_size);
  initialize_array(self->input_window,0.f,self->fft_size);
  initialize_array(self->output_window,0.f,self->fft_size);
  initialize_array(self->in_fifo,0.f,self->block_size);
  initialize_array(self->out_fifo,0.f,self->block_size);
  initialize_array(self->output_accum,0.f,self->block_size*2);
  initialize_array(self->fft_power,0.f,self->fft_size_2+1);
  initialize_array(self->fft_magnitude,0.f,self->fft_size_2+1);
  initialize_array(self->fft_phase,0.f,self->fft_size_2+1);
}

void
stft_free(STFT_transform* self)
{
  fftwf_free(self->input_fft_buffer);
  fftwf_free(self->output_fft_buffer);
	fftwf_destroy_plan(self->forward);
	fftwf_destroy_plan(self->backward);
	free(self->input_window);
	free(self->output_window);
	free(self->in_fifo);
	free(self->out_fifo);
	free(self->output_accum);
	free(self->fft_power);
	free(self->fft_magnitude);
	free(self->fft_phase);
	free(self);
}


STFT_transform*
stft_init(int block_size, int fft_size,int window_option_input,
          int window_option_output, int overlap_factor)
{
  //Allocate object
  STFT_transform *self = (STFT_transform*)malloc(sizeof(STFT_transform));

  stft_configure(self, block_size, fft_size, window_option_input, window_option_output,
                 overlap_factor);

  //Individual array allocation

  //STFT window related
  self->input_window = (float*)malloc(self->fft_size * sizeof(float));
  self->output_window = (float*)malloc(self->fft_size * sizeof(float));

  //fifo buffer init
  self->in_fifo = (float*)malloc(self->block_size * sizeof(float));
  self->out_fifo = (float*)malloc(self->block_size * sizeof(float));

  //buffer for OLA
  self->output_accum = (float*)malloc((self->block_size*2) * sizeof(float));

  //FFTW related
  self->input_fft_buffer = (float*)fftwf_malloc(self->fft_size * sizeof(float));
  self->output_fft_buffer = (float*)fftwf_malloc(self->fft_size * sizeof(float));
  self->forward = fftwf_plan_r2r_1d(self->fft_size, self->input_fft_buffer,
                                       self->output_fft_buffer, FFTW_R2HC,
                                       FFTW_ESTIMATE);
  self->backward = fftwf_plan_r2r_1d(self->fft_size, self->output_fft_buffer,
                                        self->input_fft_buffer, FFTW_HC2R,
                                        FFTW_ESTIMATE);

  //Arrays for getting bins info
  self->fft_power = (float*)malloc((self->fft_size_2+1) * sizeof(float));
  self->fft_magnitude = (float*)malloc((self->fft_size_2+1) * sizeof(float));
  self->fft_phase = (float*)malloc((self->fft_size_2+1) * sizeof(float));

  //Initialize all arrays with zeros
  stft_reset(self);

  //Window combination initialization (pre processing window post processing window)
  fft_pre_and_post_window(self->input_window, self->output_window, self->block_size,
                          self->window_option_input, self->window_option_output,
                          &self->overlap_scale_factor);

  return self;
}

void
stft_get_power_spectrum(float* power_spectrum, STFT_transform* self)
{
  memcpy(power_spectrum, self->fft_power, sizeof(float)*(self->fft_size_2+1));
}

void
stft_get_magnitude_spectrum(float* magnitude_spectrum, STFT_transform* self)
{
  memcpy(magnitude_spectrum, self->fft_magnitude, sizeof(float)*(self->fft_size_2+1));
}

void
stft_run(STFT_transform* self, int n_samples, const float* input, float* output)
{
  int k,j;

  for (k = 0; k < n_samples; k++)
  {
    //Read samples given by the host and write samples to the host
    self->in_fifo[self->read_position] = input[k];
    output[k] = self->out_fifo[self->read_position - self->input_latency];
    self->read_position++;

    if(self->read_position >= self->block_size)
    {
      //Fill the fft_buffer and reset the read position
      for(j = 0; j < self->block_size; j++)
      {
        self->input_fft_buffer[j] = self->in_fifo[j];
      }
      self->read_position = self->input_latency;

      //Do fft analysis
      stft_analysis(self);

      //Do processing
      //stft_processing(self);

      //Do synthesis
      stft_synthesis(self);
    }
  }
}