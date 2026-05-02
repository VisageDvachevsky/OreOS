#include "kernel.h"

#define TERRAIN_W ORE_GFX_LOGICAL_WIDTH
#define TERRAIN_H ORE_GFX_LOGICAL_HEIGHT
#define TERRAIN_MAX_WORKERS 8U

typedef struct {
    volatile uint32_t active;
    volatile uint32_t generation;
    volatile uint32_t worker_count;
    volatile uint32_t started_mask;
    volatile uint32_t done_mask;
    OreTerrainJob job;
    uint8_t *buffer;
    volatile uint32_t objects;
} TerrainSharedJob;

static TerrainSharedJob terrain_job;

static uint32_t terrain_hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

static int32_t terrain_noise(int32_t x, int32_t z, uint64_t seed) {
    uint32_t h = terrain_hash((uint32_t)x * 73856093U ^ (uint32_t)z * 19349663U ^ (uint32_t)seed);
    return (int32_t)(h & 255U) - 128;
}

static void draw_rect(uint8_t *buffer, int32_t x0, int32_t y0, int32_t w, int32_t h, uint8_t color) {
    for (int32_t y = 0; y < h; ++y) {
        int32_t py = y0 + y;
        if (py < 0 || py >= (int32_t)TERRAIN_H) continue;
        for (int32_t x = 0; x < w; ++x) {
            int32_t px = x0 + x;
            if (px < 0 || px >= (int32_t)TERRAIN_W) continue;
            buffer[(uint32_t)py * TERRAIN_W + (uint32_t)px] = color;
        }
    }
}

static void clear_sky(uint8_t *buffer, const OreTerrainJob *job, uint32_t y0, uint32_t y1) {
    int32_t horizon = 154 + job->pitch / 24;
    for (uint32_t y = y0; y < y1; ++y) {
        uint8_t c = y < 58 ? 1 : (y < 124 ? 2 : 3);
        for (uint32_t x = 0; x < TERRAIN_W; ++x) buffer[y * TERRAIN_W + x] = c;
    }
    if (y0 < (uint32_t)horizon && y1 > (uint32_t)(horizon - 34)) {
        uint32_t start = y0 > (uint32_t)(horizon - 34) ? y0 : (uint32_t)(horizon - 34);
        uint32_t end = y1 < (uint32_t)horizon ? y1 : (uint32_t)horizon;
        for (uint32_t y = start; y < end; ++y) {
            for (uint32_t x = 0; x < TERRAIN_W; ++x) {
                int32_t mx = (int32_t)x - 320 + job->yaw / 7;
                int32_t peak = horizon - 12 - terrain_noise(mx / 42, 11, job->seed) / 5;
                if ((int32_t)y >= peak) buffer[y * TERRAIN_W + x] = 7;
            }
        }
    }
}

static void render_voxel_slice(uint8_t *buffer, const OreTerrainJob *job, uint32_t x0, uint32_t x1) {
    int32_t horizon = 154 + job->pitch / 24;
    for (uint32_t y = (uint32_t)horizon; y < TERRAIN_H;) {
        int32_t dy = (int32_t)y - horizon + 1;
        uint32_t y_step = dy < 95 ? 3U : (dy < 185 ? 2U : 1U);
        int32_t depth = 36000 / (dy + 10);
        int32_t wz = job->camera_z + depth * 12;
        int32_t curve = terrain_noise(wz / 330, 17, job->seed) * dy / 150;
        int32_t center = 320 + job->yaw / 2 + curve;
        int32_t half = 10 + dy * dy / 360;
        if (half > 305) half = 305;
        int32_t bank = job->yaw / 12;
        uint8_t ground = (dy > 235 && (((uint32_t)(wz / 160) & 1U) == 0)) ? 5 : 4;
        uint8_t shoulder = dy > 170 ? 9 : 6;
        uint8_t water = (((uint32_t)(wz / 80 + y / 7) & 1U) == 0) ? 14 : 3;
        uint8_t grid = (((uint32_t)(wz / 96) & 7U) == 0) ? 15 : 0;
        for (uint32_t x = x0; x < x1; ++x) {
            int32_t sx = (int32_t)x - center - bank;
            uint8_t c = ground;
            if (sx > -half && sx < half) c = water;
            else if (sx > -half - 22 && sx < half + 22) c = shoulder;
            if (grid && dy > 45) c = sx > -half && sx < half ? 15 : 9;
            int32_t lane1 = -half * 2 / 3;
            int32_t lane2 = half * 2 / 3;
            if ((sx >= lane1 - 1 && sx <= lane1 + 1) || (sx >= lane2 - 1 && sx <= lane2 + 1)) {
                c = 8;
            }
            if ((sx >= -2 && sx <= 2) || (sx >= -half - 2 && sx <= -half + 2) || (sx >= half - 2 && sx <= half + 2)) {
                c = 15;
            }
            buffer[y * TERRAIN_W + x] = c;
            if (y_step == 2U && y + 1 < TERRAIN_H) buffer[(y + 1) * TERRAIN_W + x] = c;
            if (y_step == 3U && y + 1 < TERRAIN_H) buffer[(y + 1) * TERRAIN_W + x] = c;
            if (y_step == 3U && y + 2 < TERRAIN_H) buffer[(y + 2) * TERRAIN_W + x] = c;
        }
        y += y_step;
    }
}

static void draw_objects_for_slice(uint8_t *buffer, const OreTerrainJob *job, uint32_t y0, uint32_t y1, uint32_t *objects) {
    int32_t horizon = 158 + (job->pitch / 24);
    for (uint32_t i = 0; i < 42; ++i) {
        uint32_t h = terrain_hash((uint32_t)job->seed ^ i * 977U);
        int32_t base_z = (int32_t)(h & 4095U);
        int32_t dz = base_z - (job->camera_z & 4095);
        if (dz < 0) dz += 4096;
        dz += 90;
        int32_t world_x = (int32_t)((h >> 11) & 2047U) - 1024;
        if (world_x > -150 && world_x < 150) world_x += world_x < 0 ? -190 : 190;
        int32_t x = 320 + job->yaw / 3 + ((world_x - job->camera_x / 3) * 280) / dz;
        int32_t y = horizon + 18 + 36000 / dz;
        int32_t size = 3 + 1500 / dz;
        if (size > 28) size = 28;
        if ((uint32_t)y + (uint32_t)(size * 3) < y0 || (uint32_t)y >= y1) continue;
        uint32_t kind = (h >> 24) % 4U;
        if (kind == 0) {
            draw_rect(buffer, x, y - size * 3, size / 2 + 1, size * 3, 6);
            draw_rect(buffer, x - size, y - size * 4, size * 2 + 2, size * 2, 5);
        } else if (kind == 1) {
            draw_rect(buffer, x - size, y, size * 2 + 1, size / 2 + 1, 15);
            draw_rect(buffer, x + size / 2, y - size / 2, size / 2 + 1, size / 2 + 1, 8);
        } else if (kind == 2) {
            draw_rect(buffer, x - size, y - size, size * 2 + 2, size, 12);
            draw_rect(buffer, x - size / 2, y - size * 2, size, size, 12);
            draw_rect(buffer, x + size, y - size * 2, size + 5, size / 2 + 1, 12);
        } else {
            draw_rect(buffer, x - size / 2, y - size / 2, size + 1, size + 1, 7);
        }
        if (objects) (*objects)++;
    }
}

static void render_slice(uint32_t worker, uint32_t worker_count) {
    uint32_t x0 = (TERRAIN_W * worker) / worker_count;
    uint32_t x1 = (TERRAIN_W * (worker + 1)) / worker_count;
    render_voxel_slice(terrain_job.buffer, &terrain_job.job, x0, x1);
}

void terrain_worker_poll(uint32_t cpu_id) {
    if (!terrain_job.active) return;
    uint32_t workers = terrain_job.worker_count;
    if (cpu_id == 0 || cpu_id >= workers || cpu_id >= TERRAIN_MAX_WORKERS) return;
    uint32_t bit = 1U << cpu_id;
    if (terrain_job.done_mask & bit) return;
    __sync_fetch_and_or(&terrain_job.started_mask, bit);
    render_slice(cpu_id, workers);
    __sync_fetch_and_or(&terrain_job.done_mask, bit);
}

int terrain_render_user(const OreTerrainJob *job, uint8_t *buffer, uint64_t len, OreTerrainResult *result) {
    if (!job || !buffer || !result) return -ORE_EINVAL;
    if (len != (uint64_t)TERRAIN_W * TERRAIN_H) return -ORE_EINVAL;
    uint32_t workers = g_cpu_count;
    if (workers == 0) workers = 1;
    if (workers > TERRAIN_MAX_WORKERS) workers = TERRAIN_MAX_WORKERS;
    terrain_job.job = *job;
    terrain_job.buffer = buffer;
    terrain_job.worker_count = workers;
    terrain_job.started_mask = 1U;
    terrain_job.done_mask = 0;
    terrain_job.objects = 0;
    terrain_job.generation++;
    clear_sky(buffer, job, 0, TERRAIN_H);
    terrain_job.active = 1;

    uint64_t start = timer_ticks(0);
    render_slice(0, workers);
    __sync_fetch_and_or(&terrain_job.done_mask, 1U);
    uint32_t all = workers >= 32 ? 0xffffffffU : ((1U << workers) - 1U);
    for (uint32_t spin = 0; spin < 4000000U && (terrain_job.done_mask & all) != all; ++spin) {
        __asm__ volatile("pause");
    }
    for (uint32_t cpu = 1; cpu < workers; ++cpu) {
        uint32_t bit = 1U << cpu;
        if (!(terrain_job.done_mask & bit)) {
            __sync_fetch_and_or(&terrain_job.started_mask, bit);
            render_slice(cpu, workers);
            __sync_fetch_and_or(&terrain_job.done_mask, bit);
        }
    }
    uint32_t objects = 0;
    draw_objects_for_slice(buffer, job, 0, TERRAIN_H, &objects);
    terrain_job.objects = objects;
    terrain_job.active = 0;

    uint64_t checksum = 1469598103934665603ULL;
    for (uint64_t i = 0; i < len; i += 97) {
        checksum ^= buffer[i];
        checksum *= 1099511628211ULL;
    }
    result->workers_used = workers;
    result->objects_rendered = terrain_job.objects;
    result->collision_flags = 0;
    result->reserved = 0;
    result->checksum = checksum;
    result->render_ticks = timer_ticks(0) - start;
    return 0;
}
