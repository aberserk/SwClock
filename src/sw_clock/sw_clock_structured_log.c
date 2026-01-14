/**
 * @file sw_clock_structured_log.c
 * @brief SwClock Structured Logging Implementation
 */

#include "sw_clock_structured_log.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>

#define MAX_PATH_LEN 512
#define MAX_METADATA_ENTRIES 32

/**
 * @brief Metadata entry (key-value pair)
 */
typedef struct {
    char key[64];
    char value[256];
} metadata_entry_t;

/**
 * @brief Structured logger internal state
 */
struct swclock_structured_logger {
    char test_name[128];
    char output_path[MAX_PATH_LEN];
    char test_run_id[40];  // UUID string
    swclock_log_format_t format;
    FILE* fp;
    
    // Configuration
    swclock_config_snapshot_t config;
    bool config_written;
    
    // Metadata
    metadata_entry_t metadata[MAX_METADATA_ENTRIES];
    int metadata_count;
    
    // Sample tracking
    uint64_t sample_count;
    uint64_t start_timestamp_ns;
    
    // State flags
    bool header_written;
    bool finalized;
};

/**
 * @brief Generate a simple UUID v4
 */
static void generate_uuid(char* uuid_buf, size_t buf_size) {
    // Simple pseudo-random UUID (not cryptographic quality)
    srand(time(NULL) ^ getpid());
    snprintf(uuid_buf, buf_size,
             "%08x-%04x-4%03x-%04x-%012lx",
             rand(), rand() & 0xFFFF, rand() & 0xFFF,
             (rand() & 0x3FFF) | 0x8000,
             ((long)rand() << 32) | rand());
}

/**
 * @brief Get current timestamp in ISO 8601 format
 */
static void get_iso8601_timestamp(char* buf, size_t buf_size) {
    time_t now = time(NULL);
    struct tm* tm_info = gmtime(&now);
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
}

/**
 * @brief Create output directory if it doesn't exist
 */
static int ensure_directory(const char* path) {
    struct stat st = {0};
    if (stat(path, &st) == -1) {
        if (mkdir(path, 0755) != 0) {
            return -1;
        }
    }
    return 0;
}

/**
 * @brief Write JSONL header (metadata preamble)
 */
static int write_jsonl_header(swclock_structured_logger_t* logger) {
    if (logger->header_written) return 0;
    
    char timestamp[64];
    get_iso8601_timestamp(timestamp, sizeof(timestamp));
    
    // Write JSON-LD context and metadata
    fprintf(logger->fp,
            "{\n"
            "  \"@context\": \"https://swclock.org/schema/v2.0.0/test-log.jsonld\",\n"
            "  \"@type\": \"PerformanceTestLog\",\n"
            "  \"testRunId\": \"%s\",\n"
            "  \"swclockVersion\": \"v2.0.0\",\n"
            "  \"testName\": \"%s\",\n"
            "  \"startTime\": \"%s\",\n",
            logger->test_run_id,
            logger->test_name,
            timestamp);
    
    // Write configuration if available
    if (logger->config_written) {
        fprintf(logger->fp,
                "  \"config\": {\n"
                "    \"Kp_ppm_per_s\": %.3f,\n"
                "    \"Ki_ppm_per_s2\": %.3f,\n"
                "    \"max_ppm\": %.1f,\n"
                "    \"poll_ns\": %lld,\n"
                "    \"phase_eps_ns\": %lld\n"
                "  },\n",
                logger->config.kp_ppm_per_s,
                logger->config.ki_ppm_per_s2,
                logger->config.max_ppm,
                (long long)logger->config.poll_ns,
                (long long)logger->config.phase_eps_ns);
    }
    
    // Write custom metadata
    if (logger->metadata_count > 0) {
        fprintf(logger->fp, "  \"metadata\": {\n");
        for (int i = 0; i < logger->metadata_count; i++) {
            fprintf(logger->fp, "    \"%s\": \"%s\"%s\n",
                    logger->metadata[i].key,
                    logger->metadata[i].value,
                    (i < logger->metadata_count - 1) ? "," : "");
        }
        fprintf(logger->fp, "  },\n");
    }
    
    // Begin samples array
    fprintf(logger->fp, "  \"samples\": [\n");
    
    logger->header_written = true;
    return 0;
}

/**
 * @brief Write JSONL footer
 */
static int write_jsonl_footer(swclock_structured_logger_t* logger) {
    // Close samples array
    fprintf(logger->fp, "\n  ]\n}\n");
    return 0;
}

swclock_structured_logger_t* swclock_logger_create(
    const char* test_name,
    swclock_log_format_t format,
    const char* output_dir)
{
    if (!test_name) {
        errno = EINVAL;
        return NULL;
    }
    
    swclock_structured_logger_t* logger = calloc(1, sizeof(*logger));
    if (!logger) return NULL;
    
    // Initialize logger
    strncpy(logger->test_name, test_name, sizeof(logger->test_name) - 1);
    logger->format = format;
    generate_uuid(logger->test_run_id, sizeof(logger->test_run_id));
    
    // Determine output path
    const char* dir = output_dir ? output_dir : ".";
    
    const char* ext = ".jsonl";
    if (format == SWCLOCK_LOG_FORMAT_LEGACY_CSV) {
        ext = ".csv";
    } else if (format == SWCLOCK_LOG_FORMAT_MSGPACK) {
        ext = ".msgpack";
    }
    
    snprintf(logger->output_path, sizeof(logger->output_path),
             "%s/%s%s", dir, test_name, ext);
    
    // Create directory if needed
    if (ensure_directory(dir) != 0) {
        free(logger);
        return NULL;
    }
    
    // Open output file
    logger->fp = fopen(logger->output_path, "w");
    if (!logger->fp) {
        free(logger);
        return NULL;
    }
    
    return logger;
}

int swclock_logger_write_config(
    swclock_structured_logger_t* logger,
    const swclock_config_snapshot_t* config)
{
    if (!logger || !config) {
        errno = EINVAL;
        return -1;
    }
    
    memcpy(&logger->config, config, sizeof(logger->config));
    logger->config_written = true;
    return 0;
}

int swclock_logger_write_metadata(
    swclock_structured_logger_t* logger,
    const char* key,
    const char* value)
{
    if (!logger || !key || !value) {
        errno = EINVAL;
        return -1;
    }
    
    if (logger->metadata_count >= MAX_METADATA_ENTRIES) {
        errno = ENOMEM;
        return -1;
    }
    
    metadata_entry_t* entry = &logger->metadata[logger->metadata_count++];
    strncpy(entry->key, key, sizeof(entry->key) - 1);
    strncpy(entry->value, value, sizeof(entry->value) - 1);
    
    return 0;
}

int swclock_logger_write_sample(
    swclock_structured_logger_t* logger,
    uint64_t timestamp_ns,
    int64_t te_ns)
{
    if (!logger || logger->finalized) {
        errno = EINVAL;
        return -1;
    }
    
    // Write header on first sample
    if (!logger->header_written && logger->format == SWCLOCK_LOG_FORMAT_JSONL) {
        write_jsonl_header(logger);
    }
    
    // Track start time
    if (logger->sample_count == 0) {
        logger->start_timestamp_ns = timestamp_ns;
    }
    
    // Write sample based on format
    if (logger->format == SWCLOCK_LOG_FORMAT_JSONL) {
        // Add comma separator except for first sample
        if (logger->sample_count > 0) {
            fprintf(logger->fp, ",\n");
        }
        fprintf(logger->fp, "    {\"t_ns\": %llu, \"te_ns\": %lld}",
                (unsigned long long)timestamp_ns,
                (long long)te_ns);
    } else if (logger->format == SWCLOCK_LOG_FORMAT_LEGACY_CSV) {
        fprintf(logger->fp, "%llu,%lld\n",
                (unsigned long long)timestamp_ns,
                (long long)te_ns);
    }
    
    logger->sample_count++;
    return 0;
}

void swclock_logger_finalize(swclock_structured_logger_t* logger) {
    if (!logger || logger->finalized) return;
    
    if (logger->format == SWCLOCK_LOG_FORMAT_JSONL) {
        write_jsonl_footer(logger);
    }
    
    if (logger->fp) {
        fclose(logger->fp);
        logger->fp = NULL;
    }
    
    logger->finalized = true;
    free(logger);
}

const char* swclock_logger_get_path(const swclock_structured_logger_t* logger) {
    return logger ? logger->output_path : NULL;
}

uint64_t swclock_logger_get_sample_count(const swclock_structured_logger_t* logger) {
    return logger ? logger->sample_count : 0;
}
