/**
 * @file sw_clock_events.c
 * @brief Event type utilities implementation
 */

#include "sw_clock_events.h"
#include <string.h>

const char* swclock_event_type_name(swclock_event_type_t event_type) {
    switch (event_type) {
        case SWCLOCK_EVENT_ADJTIME_CALL:       return "ADJTIME_CALL";
        case SWCLOCK_EVENT_ADJTIME_RETURN:     return "ADJTIME_RETURN";
        case SWCLOCK_EVENT_PI_ENABLE:          return "PI_ENABLE";
        case SWCLOCK_EVENT_PI_DISABLE:         return "PI_DISABLE";
        case SWCLOCK_EVENT_PI_STEP:            return "PI_STEP";
        case SWCLOCK_EVENT_PHASE_SLEW_START:   return "PHASE_SLEW_START";
        case SWCLOCK_EVENT_PHASE_SLEW_DONE:    return "PHASE_SLEW_DONE";
        case SWCLOCK_EVENT_FREQUENCY_CLAMP:    return "FREQUENCY_CLAMP";
        case SWCLOCK_EVENT_THRESHOLD_CROSS:    return "THRESHOLD_CROSS";
        case SWCLOCK_EVENT_CLOCK_RESET:        return "CLOCK_RESET";
        case SWCLOCK_EVENT_LOG_START:          return "LOG_START";
        case SWCLOCK_EVENT_LOG_STOP:           return "LOG_STOP";
        case SWCLOCK_EVENT_LOG_MARKER:         return "LOG_MARKER";
        default:                                return "UNKNOWN";
    }
}

size_t swclock_event_payload_size(swclock_event_type_t event_type) {
    switch (event_type) {
        case SWCLOCK_EVENT_ADJTIME_CALL:
        case SWCLOCK_EVENT_ADJTIME_RETURN:
            return sizeof(swclock_event_adjtime_payload_t);
        
        case SWCLOCK_EVENT_PI_STEP:
            return sizeof(swclock_event_pi_step_payload_t);
        
        case SWCLOCK_EVENT_PHASE_SLEW_START:
        case SWCLOCK_EVENT_PHASE_SLEW_DONE:
            return sizeof(swclock_event_phase_slew_payload_t);
        
        case SWCLOCK_EVENT_FREQUENCY_CLAMP:
            return sizeof(swclock_event_frequency_clamp_payload_t);
        
        case SWCLOCK_EVENT_THRESHOLD_CROSS:
            return sizeof(swclock_event_threshold_payload_t);
        
        case SWCLOCK_EVENT_LOG_MARKER:
            return sizeof(swclock_event_marker_payload_t);
        
        case SWCLOCK_EVENT_PI_ENABLE:
        case SWCLOCK_EVENT_PI_DISABLE:
        case SWCLOCK_EVENT_CLOCK_RESET:
        case SWCLOCK_EVENT_LOG_START:
        case SWCLOCK_EVENT_LOG_STOP:
            return 0;  // No payload
        
        default:
            return 0;
    }
}
