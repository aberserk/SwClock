/**
 * @file test_metadata.cpp
 * @brief Test metadata collection implementation
 */

#include "test_metadata.h"
#include <cstring>
#include <cstdio>
#include <ctime>
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/sysctl.h>
#include <cstdlib>

void generate_test_run_uuid(char* uuid_buf, size_t buf_size) {
    // Simple pseudo-random UUID v4 (not cryptographic quality)
    srand(static_cast<unsigned>(time(nullptr)) ^ getpid());
    snprintf(uuid_buf, buf_size,
             "%08x-%04x-4%03x-%04x-%012lx",
             rand(), rand() & 0xFFFF, rand() & 0xFFF,
             (rand() & 0x3FFF) | 0x8000,
             ((long)rand() << 32) | rand());
}

void get_iso8601_timestamp(char* buf, size_t buf_size) {
    time_t now = time(nullptr);
    struct tm* tm_info = gmtime(&now);
    strftime(buf, buf_size, "%Y-%m-%dT%H:%M:%SZ", tm_info);
}

int get_system_hostname(char* buf, size_t buf_size) {
    return gethostname(buf, buf_size);
}

int get_cpu_model(char* buf, size_t buf_size) {
#ifdef __APPLE__
    size_t size = buf_size;
    if (sysctlbyname("machdep.cpu.brand_string", buf, &size, nullptr, 0) == 0) {
        return 0;
    }
#endif
    
    // Fallback
    snprintf(buf, buf_size, "Unknown CPU");
    return -1;
}

int get_os_info(char* name_buf, size_t name_size, 
                char* version_buf, size_t version_size) {
    struct utsname info;
    if (uname(&info) != 0) {
        return -1;
    }
    
    snprintf(name_buf, name_size, "%s", info.sysname);
    snprintf(version_buf, version_size, "%s", info.release);
    return 0;
}

double get_system_load() {
    double loadavg[3];
    if (getloadavg(loadavg, 3) != -1) {
        return loadavg[0];  // 1-minute average
    }
    return -1.0;
}

int get_cpu_count() {
#ifdef __APPLE__
    int mib[2] = {CTL_HW, HW_NCPU};
    int cpu_count = 0;
    size_t len = sizeof(cpu_count);
    if (sysctl(mib, 2, &cpu_count, &len, nullptr, 0) == 0) {
        return cpu_count;
    }
#else
    long cpu_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_count > 0) {
        return static_cast<int>(cpu_count);
    }
#endif
    return -1;
}

void collect_test_metadata(
    test_metadata_t* meta,
    const char* test_name,
    double kp,
    double ki,
    double max_ppm,
    int64_t poll_ns,
    int64_t phase_eps_ns)
{
    if (!meta) return;
    
    memset(meta, 0, sizeof(*meta));
    
    // Test identification
    generate_test_run_uuid(meta->test_run_id, sizeof(meta->test_run_id));
    snprintf(meta->test_name, sizeof(meta->test_name), "%s", test_name);
    snprintf(meta->swclock_version, sizeof(meta->swclock_version), "v2.0.0");
    
    // Configuration
    meta->kp_ppm_per_s = kp;
    meta->ki_ppm_per_s2 = ki;
    meta->max_ppm = max_ppm;
    meta->poll_ns = poll_ns;
    meta->phase_eps_ns = phase_eps_ns;
    
    // System information
    get_os_info(meta->os_name, sizeof(meta->os_name),
                meta->os_version, sizeof(meta->os_version));
    get_cpu_model(meta->cpu_model, sizeof(meta->cpu_model));
    get_system_hostname(meta->hostname, sizeof(meta->hostname));
    
    // Timing reference
    snprintf(meta->reference_clock, sizeof(meta->reference_clock),
             "CLOCK_MONOTONIC_RAW");
    
    // Test conditions
    get_iso8601_timestamp(meta->start_time_iso8601, 
                          sizeof(meta->start_time_iso8601));
    snprintf(meta->timezone, sizeof(meta->timezone), "UTC");
    
    // Environment
    meta->ambient_temp_c = -273.15;  // Not available
    meta->system_load_avg = get_system_load();
    meta->cpu_count = get_cpu_count();
    
    // Compliance standard
    snprintf(meta->compliance_standard, sizeof(meta->compliance_standard),
             "ITU-T G.8260 Class C");
}
