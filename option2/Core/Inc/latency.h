#ifndef LATENCY_H
#define LATENCY_H

#include <stdint.h>
#include <math.h>

#define LATENCY_SAMPLES 100
#define LOAD_LEVELS 4

typedef struct {
    uint32_t samples[LATENCY_SAMPLES];  // 延遲樣本陣列（單位：ms）
    uint32_t sample_count;               // 當前樣本數
    uint32_t load_level;                 // 0-3，對應 0%, 25%, 50%, 100%
} LatencySampler;

typedef struct {
    float mean;       // 平均延遲（ms）
    float stddev;     // 標準差（ms）
    uint32_t min;     // 最小延遲（ms）
    uint32_t max;     // 最大延遲（ms）
    uint32_t count;   // 樣本數
} LatencyStats;

// 全局採樣器實例
extern LatencySampler latency_sampler;
extern uint32_t current_load_factor;

// 函數聲明
void latency_init(void);
void latency_record_interrupt(uint32_t tick);
void latency_record_task_start(uint32_t tick);
void latency_advance_load_level(void);
LatencyStats latency_compute_stats(void);
void latency_reset_samples(void);

#endif // LATENCY_H
