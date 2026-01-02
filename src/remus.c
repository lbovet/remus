#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "lv2/core/lv2.h"
#include "lv2/atom/atom.h"
#include "lv2/atom/forge.h"
#include "lv2/urid/urid.h"
#include "lv2/atom/util.h"
#include "lv2/time/time.h"
#include "lv2/state/state.h"

#define REMUS_URI "http://github.com/lbovet/remus"

#define MAX_BUFFER_SIZE 48000 * 60 * 5  // 5 minutes at 48kHz
#define TAIL_BUFFER_SIZE 1024  // Maximum tail buffer size for zero-crossing alignment
#define ZERO_CROSSING_DISTANCE 8  // Maximum distance for zero-crossing matching
#define CROSSFADE_SAMPLES 64  // Number of samples for crossfade transition

typedef enum {
	REMUS_AUDIO_IN      = 0,
	REMUS_AUDIO_OUT     = 1,
	REMUS_CONTROL       = 2,
	REMUS_RECORD_EN     = 3,
	REMUS_LOOP_LEN      = 4,
	REMUS_PERSIST_EN    = 5,
	REMUS_RECORDING_OUT = 6,
	REMUS_ARMED_OUT     = 7,
	REMUS_RECORDED_OUT  = 8
} PortIndex;

typedef struct {
	// Port buffers
	const float*      audio_in;
	float*            audio_out;
	const LV2_Atom_Sequence* control_port;
	const float*      record_enable;
	const float*      loop_length;
	const float*      persist_enable;
	float*            recording_status;
	float*            armed_status;
	float*            recorded_status;
	
	// URID map
	LV2_URID_Map* map;
	
	// URIDs
	LV2_URID atom_Blank;
	LV2_URID atom_Object;
	LV2_URID atom_Float;
	LV2_URID atom_Long;
	LV2_URID atom_Int;
	LV2_URID time_Position;
	LV2_URID time_barBeat;
	LV2_URID time_bar;
	LV2_URID time_speed;
	LV2_URID time_beatsPerMinute;
	LV2_URID time_beatsPerBar;
	
	// State URIDs
	LV2_URID remus_buffer;
	LV2_URID remus_loop_samples;
	LV2_URID remus_has_recorded;
	
	// Internal state
	float*   buffer;
	uint32_t buffer_size;
	uint32_t write_pos;
	uint32_t read_pos;
	uint32_t loop_samples;
	
	bool     recording;
	bool     has_recorded;
	bool     waiting_for_bar;
	bool     playing;
	bool     waiting_to_play;
	float    prev_record_enable;
	
	// Tail buffer for zero-crossing alignment (max TAIL_BUFFER_SIZE samples)
	float    tail_buffer[TAIL_BUFFER_SIZE];
	uint32_t tail_pos;
	bool     recording_tail;
	uint32_t tail_zero_crossings;
	int32_t  tail_min_distance;
	uint32_t stitch_position;  // Position for crossfade, 0 means not set
	
	double   sample_rate;
	float    last_bar_beat;
	int64_t  last_bar;
	
	// Transport tempo/time signature
	float    transport_bpm;
	float    transport_beats_per_bar;
	
	// Debug flag
	bool     debug_logged;
} Remus;

static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
	Remus* remus = (Remus*)calloc(1, sizeof(Remus));
	if (!remus) {
		return NULL;
	}
	
	// Get URID map feature
	for (int i = 0; features[i]; i++) {
		if (!strcmp(features[i]->URI, LV2_URID__map)) {
			remus->map = (LV2_URID_Map*)features[i]->data;
		}
	}
	
	if (!remus->map) {
		free(remus);
		return NULL;
	}
	
	// Map URIDs
	remus->atom_Blank = remus->map->map(remus->map->handle, LV2_ATOM__Blank);
	remus->atom_Object = remus->map->map(remus->map->handle, LV2_ATOM__Object);
	remus->atom_Float = remus->map->map(remus->map->handle, LV2_ATOM__Float);
	remus->atom_Long = remus->map->map(remus->map->handle, LV2_ATOM__Long);
	remus->atom_Int = remus->map->map(remus->map->handle, LV2_ATOM__Int);
	remus->time_Position = remus->map->map(remus->map->handle, LV2_TIME__Position);
	remus->time_barBeat = remus->map->map(remus->map->handle, LV2_TIME__barBeat);
	remus->time_bar = remus->map->map(remus->map->handle, LV2_TIME__bar);
	remus->time_speed = remus->map->map(remus->map->handle, LV2_TIME__speed);
	remus->time_beatsPerMinute = remus->map->map(remus->map->handle, LV2_TIME__beatsPerMinute);
	remus->time_beatsPerBar = remus->map->map(remus->map->handle, LV2_TIME__beatsPerBar);
	
	// Map state URIDs
	remus->remus_buffer = remus->map->map(remus->map->handle, REMUS_URI "#buffer");
	remus->remus_loop_samples = remus->map->map(remus->map->handle, REMUS_URI "#loop_samples");
	remus->remus_has_recorded = remus->map->map(remus->map->handle, REMUS_URI "#has_recorded");
	
	remus->sample_rate = rate;
	remus->buffer_size = MAX_BUFFER_SIZE;
	remus->buffer = (float*)calloc(remus->buffer_size, sizeof(float));
	
	if (!remus->buffer) {
		free(remus);
		return NULL;
	}
	
	remus->write_pos = 0;
	remus->read_pos = 0;
	remus->recording = false;
	remus->has_recorded = false;
	remus->waiting_for_bar = false;
	remus->playing = false;
	remus->waiting_to_play = false;
	remus->prev_record_enable = 0.0f;
	remus->loop_samples = 0;
	remus->tail_pos = 0;
	remus->recording_tail = false;
	remus->tail_zero_crossings = 0;
	remus->tail_min_distance = TAIL_BUFFER_SIZE;
remus->stitch_position = 0;
	remus->last_bar_beat = -1.0f;
	remus->last_bar = -1;
	remus->transport_bpm = 120.0f;
	remus->transport_beats_per_bar = 4.0f;
	remus->debug_logged = false;
	
	return (LV2_Handle)remus;
}

static void
connect_port(LV2_Handle instance,
             uint32_t   port,
             void*      data)
{
	Remus* remus = (Remus*)instance;
	
	switch ((PortIndex)port) {
	case REMUS_AUDIO_IN:
		remus->audio_in = (const float*)data;
		break;
	case REMUS_AUDIO_OUT:
		remus->audio_out = (float*)data;
		break;
	case REMUS_CONTROL:
		remus->control_port = (const LV2_Atom_Sequence*)data;
		break;
	case REMUS_RECORD_EN:
		remus->record_enable = (const float*)data;
		break;
	case REMUS_LOOP_LEN:
		remus->loop_length = (const float*)data;
		break;
	case REMUS_PERSIST_EN:
		remus->persist_enable = (const float*)data;
		break;
	case REMUS_RECORDING_OUT:
		remus->recording_status = (float*)data;
		break;
	case REMUS_ARMED_OUT:
		remus->armed_status = (float*)data;
		break;
	case REMUS_RECORDED_OUT:
		remus->recorded_status = (float*)data;
		break;
	}
}static void
activate(LV2_Handle instance)
{
	Remus* remus = (Remus*)instance;
	
	fprintf(stderr, "REMUS: activate() called - has_recorded=%d before clear\n", remus->has_recorded);
	
	// Don't clear buffer if we have restored data
	// Only reset the playback position and state flags
	if (!remus->has_recorded) {
		// Only clear buffer if no data was restored
		memset(remus->buffer, 0, remus->buffer_size * sizeof(float));
		remus->loop_samples = 0;
	}
	
	remus->write_pos = 0;
	remus->read_pos = 0;
	remus->recording = false;
	remus->waiting_for_bar = false;
	remus->playing = false;
	remus->waiting_to_play = false;
	remus->prev_record_enable = 0.0f;
	remus->loop_samples = 0;
	remus->tail_pos = 0;
	remus->recording_tail = false;
	remus->tail_zero_crossings = 0;
	remus->tail_min_distance = TAIL_BUFFER_SIZE;
remus->stitch_position = 0;
	remus->last_bar_beat = -1.0f;
	remus->last_bar = -1;
	remus->transport_bpm = 120.0f;
	remus->transport_beats_per_bar = 4.0f;
}

static void
run(LV2_Handle instance, uint32_t n_samples)
{
	Remus* remus = (Remus*)instance;
	
	// One-time debug log after restore
	if (!remus->debug_logged && remus->has_recorded) {
		fprintf(stderr, "REMUS: In run() - has_recorded=%d, loop_samples=%u, recording=%d, waiting_for_bar=%d\n",
		        remus->has_recorded, remus->loop_samples, remus->recording, remus->waiting_for_bar);
		remus->debug_logged = true;
	}
	
	const float* const audio_in   = remus->audio_in;
	float* const       audio_out  = remus->audio_out;
	const float        rec_enable = *remus->record_enable;
	const float        loop_len   = *remus->loop_length;
	
	// Process time position events
	float current_bar_beat = remus->last_bar_beat;
	int64_t current_bar = remus->last_bar;
	bool transport_rolling = false;
	
	LV2_ATOM_SEQUENCE_FOREACH(remus->control_port, ev) {
		if (ev->body.type == remus->atom_Blank || ev->body.type == remus->atom_Object) {
			const LV2_Atom_Object* obj = (const LV2_Atom_Object*)&ev->body;
			if (obj->body.otype == remus->time_Position) {
				// Extract transport position, tempo, and time signature
				LV2_Atom *bar_beat = NULL, *bar = NULL, *speed = NULL;
				LV2_Atom *bpm_atom = NULL, *beats_per_bar_atom = NULL;
				lv2_atom_object_get(obj,
				                    remus->time_barBeat, &bar_beat,
				                    remus->time_bar, &bar,
				                    remus->time_speed, &speed,
				                    remus->time_beatsPerMinute, &bpm_atom,
				                    remus->time_beatsPerBar, &beats_per_bar_atom,
				                    NULL);
				
				if (bar_beat && bar_beat->type == remus->atom_Float) {
					current_bar_beat = ((const LV2_Atom_Float*)bar_beat)->body;
				}
				if (bar && bar->type == remus->atom_Long) {
					current_bar = ((const LV2_Atom_Long*)bar)->body;
				}
				if (speed && speed->type == remus->atom_Float) {
					transport_rolling = (((const LV2_Atom_Float*)speed)->body > 0.0f);
				}
				if (bpm_atom && bpm_atom->type == remus->atom_Float) {
					remus->transport_bpm = ((const LV2_Atom_Float*)bpm_atom)->body;
				}
				if (beats_per_bar_atom && beats_per_bar_atom->type == remus->atom_Float) {
					remus->transport_beats_per_bar = ((const LV2_Atom_Float*)beats_per_bar_atom)->body;
				}
			}
		}
	}
	
	// Calculate loop length in samples using transport tempo
	// beats_per_bar * bars * 60 / bpm * sample_rate
	const uint32_t loop_beats = (uint32_t)(remus->transport_beats_per_bar * loop_len);
	const uint32_t new_loop_samples = (uint32_t)((loop_beats * 60.0 * remus->sample_rate) / remus->transport_bpm);
	
	// Detect record enable edge (on to off transition)
	const bool rec_start = (rec_enable <= 0.5f) && (remus->prev_record_enable > 0.5f);

	remus->prev_record_enable = rec_enable;
	
	// Stop recording on manual restart
	if (remus->recording && rec_start) {
		if (remus->recording) {
			remus->recording = false;
		}
		if (remus->recording_tail) {
			remus->recording_tail = false;
			remus->has_recorded = true;
			remus->tail_pos = 0;
			fprintf(stderr, "REMUS: Tail recording stopped manually\n");
		}
	}

	// On record start, wait for next bar boundary
	if (rec_start) {
		remus->waiting_for_bar = true;
		remus->loop_samples = new_loop_samples;
		
		// Clamp to buffer size
		if (remus->loop_samples > remus->buffer_size) {
			remus->loop_samples = remus->buffer_size;
		}
	}

	// Check if we've crossed a bar boundary
	if (remus->waiting_for_bar && transport_rolling) {
		// Detect bar change
		if (current_bar >= 0 && remus->last_bar >= 0 && current_bar != remus->last_bar) {
			// New bar started - begin recording
			remus->recording = true;
			remus->waiting_for_bar = false;
			remus->write_pos = 0;
			remus->read_pos = 0;
			remus->has_recorded = false;
		}
		// Also check if barBeat wrapped around to detect bar boundary
		else if (current_bar_beat >= 0.0f && remus->last_bar_beat >= 0.0f &&
		         current_bar_beat < remus->last_bar_beat) {
			remus->recording = true;
			remus->waiting_for_bar = false;
			remus->write_pos = 0;
			remus->read_pos = 0;
			remus->has_recorded = false;
		}
	}
	
	// Handle playback alignment with transport
	if (remus->has_recorded && remus->loop_samples > 0 && !remus->recording) {
		if (!transport_rolling) {
			// Transport stopped - stop playing
			remus->playing = false;
			remus->waiting_to_play = false;
		} else if (!remus->playing && !remus->waiting_to_play) {
			// Transport started and we're not playing
			// If we're at the very beginning (bar 0, beat 0), start immediately
			if (current_bar == 0 && current_bar_beat >= 0.0f && current_bar_beat < 0.1f) {
				remus->playing = true;
				remus->waiting_to_play = false;
				remus->read_pos = 0;
			} else {
				// Otherwise wait for next bar boundary
				remus->waiting_to_play = true;
			}
		} else if (remus->waiting_to_play) {
			// Check if we've crossed a bar boundary
			if (current_bar >= 0 && remus->last_bar >= 0 && current_bar != remus->last_bar) {
				// New bar started - begin playing
				remus->playing = true;
				remus->waiting_to_play = false;
				remus->read_pos = 0;
			} else if (current_bar_beat >= 0.0f && remus->last_bar_beat >= 0.0f &&
			           current_bar_beat < remus->last_bar_beat) {
				// Bar wrapped - begin playing
				remus->playing = true;
				remus->waiting_to_play = false;
				remus->read_pos = 0;
			}
		}
	}
	
	// Update loop length if parameters changed and not currently recording
	if (!remus->recording && !remus->waiting_for_bar && remus->has_recorded) {
		if (new_loop_samples != remus->loop_samples) {
			remus->loop_samples = new_loop_samples;
			if (remus->loop_samples > remus->buffer_size) {
				remus->loop_samples = remus->buffer_size;
			}
			// Ensure read position is within new loop bounds
			if (remus->read_pos >= remus->loop_samples) {
				remus->read_pos = 0;
			}
		}
	}
	
	for (uint32_t i = 0; i < n_samples; i++) {
		if (remus->recording) {
			// Record input to buffer
			if (remus->write_pos < remus->loop_samples) {
				remus->buffer[remus->write_pos] = audio_in[i];
				remus->write_pos++;
				
				// If we've filled the loop, start tail recording
				if (remus->write_pos >= remus->loop_samples) {
					remus->recording = false;
					remus->recording_tail = true;
					remus->tail_pos = 0;
					remus->tail_zero_crossings = 0;
				remus->tail_min_distance = TAIL_BUFFER_SIZE;
remus->stitch_position = 0;
					fprintf(stderr, "REMUS: Loop filled (%u samples), starting tail recording\n", remus->loop_samples);
				}
			}
			
			// Silence output while recording
			audio_out[i] = 0.0f;
		} else if (remus->recording_tail) {
			// Record into tail buffer and search for zero-crossings
			
			if (remus->tail_pos < TAIL_BUFFER_SIZE) {
				remus->tail_buffer[remus->tail_pos] = audio_in[i];
				remus->tail_pos++;
				
				// Start searching after we have at least 2 samples (need at least one crossing)
				if (remus->tail_pos >= 2) {
					// Check if we just crossed zero in the tail buffer
					bool tail_crossing = false;
					bool tail_positive = false;
					uint32_t t = remus->tail_pos - 1;
					
					if ((remus->tail_buffer[t-1] < 0.0f && remus->tail_buffer[t] >= 0.0f)) {
						tail_crossing = true;
						tail_positive = true;
					} else if ((remus->tail_buffer[t-1] > 0.0f && remus->tail_buffer[t] <= 0.0f)) {
						tail_crossing = true;
						tail_positive = false;
					}
					
					if (tail_crossing) {
						// Count this zero-crossing
						remus->tail_zero_crossings++;
						
					// Search for matching zero-crossing in loop start
					// Search relative to tail_pos, within ±ZERO_CROSSING_DISTANCE samples
					uint32_t loop_search_start = (t > ZERO_CROSSING_DISTANCE) ? (t - ZERO_CROSSING_DISTANCE) : 1;
					uint32_t loop_search_end = (t + ZERO_CROSSING_DISTANCE < remus->loop_samples) ? (t + ZERO_CROSSING_DISTANCE) : remus->loop_samples - 1;
						for (uint32_t l = loop_search_start; l <= loop_search_end; l++) {
							bool loop_crossing = false;
							bool loop_positive = false;
							
							// Check for zero crossing in loop
							if ((remus->buffer[l-1] < 0.0f && remus->buffer[l] >= 0.0f)) {
								loop_crossing = true;
								loop_positive = true;
							} else if ((remus->buffer[l-1] > 0.0f && remus->buffer[l] <= 0.0f)) {
								loop_crossing = true;
								loop_positive = false;
							}
							
							// Check if crossings match in direction
							if (loop_crossing && (tail_positive == loop_positive)) {
								int32_t distance = abs((int32_t)t - (int32_t)l);
								
								// Track minimum distance
								if (distance < remus->tail_min_distance) {
									remus->tail_min_distance = distance;
								}

								// Calculate midpoint
								uint32_t midpoint = (t + l) / 2;

								// Match found within threshold and crossfade is possible
								if (distance <= ZERO_CROSSING_DISTANCE 
									&& midpoint >= (CROSSFADE_SAMPLES / 2)
									&& midpoint + 1 < (TAIL_BUFFER_SIZE - CROSSFADE_SAMPLES / 2)
									&& remus->stitch_position == 0) {  // Only set once
									// Set the stitch position to the midpoint between the two zero-crossings
									remus->stitch_position = midpoint + 1;
									
									fprintf(stderr, "REMUS: Found zero-crossing match - tail[%u] and loop[%u], distance=%d samples, slope=%s, midpoint=%u\n",
											t, l, distance,
											tail_positive ? "positive" : "negative", midpoint);
									fprintf(stderr, "REMUS: Continuing tail recording to collect crossfade samples (need %u more samples)\n",
											remus->stitch_position + CROSSFADE_SAMPLES / 2 - remus->tail_pos);
									break;
								}
							}
						}
					}
				}
				
				// Check if we have enough samples for crossfade after finding a match
				if (remus->stitch_position > 0) {
					uint32_t samples_needed = remus->stitch_position + CROSSFADE_SAMPLES / 2;
					if (remus->tail_pos >= samples_needed) {
						fprintf(stderr, "REMUS: Collected enough samples for crossfade (%u samples)\n", remus->tail_pos);
						remus->recording_tail = false;
						remus->has_recorded = true;
					}
				}
				
				// Check if tail buffer is full without finding a match
				if (remus->tail_pos >= TAIL_BUFFER_SIZE && remus->stitch_position == 0) {
					// Choose closest position able to crossfade
					remus->stitch_position = CROSSFADE_SAMPLES / 2;
					fprintf(stderr, "REMUS: Tail buffer full - will use position %u (no zero-crossing match)\n", remus->stitch_position);
					
					remus->recording_tail = false;
					remus->has_recorded = true;
					if (remus->tail_min_distance < TAIL_BUFFER_SIZE) {
						fprintf(stderr, "REMUS: Finishing without zero-crossing alignment. "
						        "Considered %u zero-crossings, minimal distance found: %d samples\n",
						        remus->tail_zero_crossings, remus->tail_min_distance);
					} else {
						fprintf(stderr, "REMUS: Finishing without zero-crossing alignment. "
						        "Considered %u zero-crossings, no matching crossings found in loop\n",
						        remus->tail_zero_crossings);
					}
				}
			}
			
			// Silence output while recording tail
			audio_out[i] = 0.0f;
		}
		
		// Perform crossfade after tail recording is complete
		if (!remus->recording_tail && remus->stitch_position > 0 && remus->tail_pos > 0) {
			const uint32_t half_crossfade = CROSSFADE_SAMPLES / 2;
			uint32_t crossfade_start = remus->stitch_position - half_crossfade;
			
			// Copy samples from tail buffer before the crossfade zone
			if (crossfade_start > 0) {
				memcpy(remus->buffer, remus->tail_buffer, crossfade_start * sizeof(float));
				fprintf(stderr, "REMUS: Copied %u samples before crossfade zone\n", crossfade_start);
			}
			
			// Apply crossfade centered around stitch_position
			for (uint32_t cf = 0; cf < CROSSFADE_SAMPLES; cf++) {
				// Position in both buffers
				uint32_t pos = crossfade_start + cf;
			
				// Linear crossfade: fade out loop, fade in tail
				float fade_in = (float)cf / (float)(CROSSFADE_SAMPLES - 1);
				float fade_out = 1.0f - fade_in;
				
				remus->buffer[pos] = remus->tail_buffer[pos] * fade_out + remus->buffer[pos] * fade_in;
			}
			
			fprintf(stderr, "REMUS: Applied %u-sample crossfade around position %u for click-free transition\n", CROSSFADE_SAMPLES, remus->stitch_position);
			
			// Reset for next time
			remus->tail_pos = 0;
			remus->stitch_position = 0;
		}
		
		if (remus->playing && remus->has_recorded && remus->loop_samples > 0) {
			// Playback loop (only when playing)
			audio_out[i] = remus->buffer[remus->read_pos];
			
			remus->read_pos++;
			if (remus->read_pos >= remus->loop_samples) {
				remus->read_pos = 0;
			}
		} else {
			// No recording, pass through or silence
			audio_out[i] = 0.0f;
		}
	}
	
	// Update recording status outputs
	if (remus->recording_status) {
		*remus->recording_status = (remus->recording || remus->recording_tail) ? 1.0f : 0.0f;
	}
	if (remus->armed_status) {
		*remus->armed_status = remus->waiting_for_bar ? 1.0f : 0.0f;
	}
	if (remus->recorded_status) {
		*remus->recorded_status = remus->has_recorded ? 1.0f : 0.0f;
	}
	
	// Save current position for next cycle
	remus->last_bar_beat = current_bar_beat;
	remus->last_bar = current_bar;
}

static void
deactivate(LV2_Handle instance)
{
	// Nothing to do here
}

static void
cleanup(LV2_Handle instance)
{
	Remus* remus = (Remus*)instance;
	free(remus->buffer);
	free(remus);
}

static LV2_State_Status
save(LV2_Handle                instance,
     LV2_State_Store_Function  store,
     LV2_State_Handle          handle,
     uint32_t                  flags,
     const LV2_Feature* const* features)
{
	Remus* remus = (Remus*)instance;
	
	fprintf(stderr, "REMUS: save() called - persist_enable=%f, has_recorded=%d, loop_samples=%u\n",
	        *remus->persist_enable, remus->has_recorded, remus->loop_samples);
	
	// Check if persistence is enabled
	if (*remus->persist_enable < 0.5f) {
		fprintf(stderr, "REMUS: Persistence disabled, not saving\n");
		return LV2_STATE_SUCCESS;  // Don't save if disabled
	}
	
	// Only save if we have recorded data
	if (!remus->has_recorded || remus->loop_samples == 0) {
		fprintf(stderr, "REMUS: No recorded data to save\n");
		return LV2_STATE_SUCCESS;
	}
	
	fprintf(stderr, "REMUS: Saving %u samples\n", remus->loop_samples);
	
	// Save the loop buffer as a vector of floats
	store(handle, remus->remus_buffer,
	      remus->buffer,
	      remus->loop_samples * sizeof(float),
	      remus->atom_Float,
	      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
	
	// Save loop length
	store(handle, remus->remus_loop_samples,
	      &remus->loop_samples,
	      sizeof(uint32_t),
	      remus->atom_Long,
	      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
	
	// Save has_recorded flag
	uint32_t has_rec = remus->has_recorded ? 1 : 0;
	store(handle, remus->remus_has_recorded,
	      &has_rec,
	      sizeof(uint32_t),
	      remus->atom_Long,
	      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
	
	fprintf(stderr, "REMUS: State saved successfully\n");
	return LV2_STATE_SUCCESS;
}

static LV2_State_Status
restore(LV2_Handle                  instance,
        LV2_State_Retrieve_Function retrieve,
        LV2_State_Handle            handle,
        uint32_t                    flags,
        const LV2_Feature* const*   features)
{
	Remus* remus = (Remus*)instance;
	
	fprintf(stderr, "REMUS: restore() called\n");
	
	// Retrieve loop_samples
	size_t size;
	uint32_t type;
	uint32_t rflags;
	
	const void* loop_samples_data = retrieve(
		handle, remus->remus_loop_samples, &size, &type, &rflags);
	
	if (loop_samples_data && type == remus->atom_Long) {
		remus->loop_samples = *(const uint32_t*)loop_samples_data;
		fprintf(stderr, "REMUS: Restored loop_samples=%u\n", remus->loop_samples);
		
		// Clamp to buffer size
		if (remus->loop_samples > remus->buffer_size) {
			remus->loop_samples = remus->buffer_size;
		}
	} else {
		fprintf(stderr, "REMUS: Failed to restore loop_samples (data=%p, type=%u, expected=%u)\n",
		        loop_samples_data, type, remus->atom_Long);
	}
	
	// Retrieve has_recorded flag
	const void* has_rec_data = retrieve(
		handle, remus->remus_has_recorded, &size, &type, &rflags);
	
	if (has_rec_data && type == remus->atom_Long) {
		remus->has_recorded = (*(const uint32_t*)has_rec_data != 0);
		fprintf(stderr, "REMUS: Restored has_recorded=%d\n", remus->has_recorded);
	} else {
		fprintf(stderr, "REMUS: Failed to restore has_recorded\n");
	}
	
	// Retrieve buffer data
	const void* buffer_data = retrieve(
		handle, remus->remus_buffer, &size, &type, &rflags);
	
	if (buffer_data && remus->loop_samples > 0) {
		// Calculate expected size
		size_t expected_size = remus->loop_samples * sizeof(float);
		size_t copy_size = (size < expected_size) ? size : expected_size;
		
		fprintf(stderr, "REMUS: Restoring %zu bytes of buffer data (expected %zu)\n", copy_size, expected_size);
		
		// Copy buffer data
		memcpy(remus->buffer, buffer_data, copy_size);
		
		// Reset playback position
		remus->read_pos = 0;
		remus->recording = false;
		remus->waiting_for_bar = false;
		
		fprintf(stderr, "REMUS: After restore - recording=%d, has_recorded=%d, loop_samples=%u, read_pos=%u\n",
		        remus->recording, remus->has_recorded, remus->loop_samples, remus->read_pos);
	} else {
		fprintf(stderr, "REMUS: No buffer data to restore (data=%p, loop_samples=%u)\n",
		        buffer_data, remus->loop_samples);
	}
	
	fprintf(stderr, "REMUS: State restored successfully\n");
	return LV2_STATE_SUCCESS;
}

static const LV2_State_Interface state_interface = {
	save,
	restore
};

static const void*
extension_data(const char* uri)
{
	if (!strcmp(uri, LV2_STATE__interface)) {
		return &state_interface;
	}
	return NULL;
}

static const LV2_Descriptor descriptor = {
	REMUS_URI,
	instantiate,
	connect_port,
	activate,
	run,
	deactivate,
	cleanup,
	extension_data
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
	return index == 0 ? &descriptor : NULL;
}
