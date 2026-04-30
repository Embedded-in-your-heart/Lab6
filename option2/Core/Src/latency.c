#include "latency.h"
#include <string.h>
#include <math.h>

LatencySampler latency_sampler;
uint32_t current_load_factor = 0;

void latency_init(void) {
    memset(&latency_sampler, 0, sizeof(LatencySampler));
    latency_sampler.load_level = 0;
    current_load_factor = 0;
}

static void compute_statistics(const uint32_t *samples, uint32_t count,
                               float *mean, float *stddev,
                               uint32_t *min, uint32_t *max) {
    if (count == 0) {
        *mean = 0.0f;
        *stddev = 0.0f;
        *min = 0;
        *max = 0;
        return;
    }

    // Calculate mean
    uint64_t sum = 0;
    uint32_t min_val = samples[0];
    uint32_t max_val = samples[0];

    for (uint32_t i = 0; i < count; i++) {
        sum += samples[i];
        if (samples[i] < min_val) min_val = samples[i];
        if (samples[i] > max_val) max_val = samples[i];
    }

    *mean = (float)sum / count;
    *min = min_val;
    *max = max_val;

    // Calculate standard deviation
    float variance = 0.0f;
    for (uint32_t i = 0; i < count; i++) {
        float diff = samples[i] - *mean;
        variance += diff * diff;
    }
    variance /= count;
    *stddev = sqrtf(variance);
}

LatencyStats latency_compute_stats(void) {
    LatencyStats stats;
    compute_statistics(latency_sampler.samples,
                      latency_sampler.sample_count,
                      &stats.mean,
                      &stats.stddev,
                      &stats.min,
                      &stats.max);
    stats.count = latency_sampler.sample_count;
    return stats;
}

void latency_reset_samples(void) {
    latency_sampler.sample_count = 0;
    memset(latency_sampler.samples, 0, sizeof(latency_sampler.samples));
}

static uint32_t last_interrupt_tick = 0;

void latency_record_interrupt(uint32_t tick) {
    last_interrupt_tick = tick;
}

void latency_record_task_start(uint32_t tick) {
    if (latency_sampler.sample_count < LATENCY_SAMPLES) {
        uint32_t latency = tick - last_interrupt_tick;
        latency_sampler.samples[latency_sampler.sample_count] = latency;
        latency_sampler.sample_count++;
    }
}

void latency_advance_load_level(void) {
    if (latency_sampler.load_level < LOAD_LEVELS - 1) {
        latency_sampler.load_level++;
    }
}
