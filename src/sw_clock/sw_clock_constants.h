//
// sw_clock_constants.h
// Constants used by the SwClock implementation
//
#ifndef SW_CLOCK_CONSTANTS_H
#define SW_CLOCK_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

// Constants used by the ntp_adjtime() frequency field
#define NTP_SCALE_SHIFT   16                // Frequency units are (ppm << 16)
#define NTP_SCALE_FACTOR  (1L << NTP_SCALE_SHIFT)  // 65536

// Units
#define PPM_TO_PPB   1000LL       // parts-per-million to parts-per-billion  
#define NS_PER_SEC   1000000000LL
#define US_PER_SEC   1000000LL
#define MS_PER_SEC   1000LL

#define NS_PER_US    1000LL
#define NS_PER_MS    1000000LL
#define SEC_PER_NS   (1.0 / (double)NS_PER_SEC)


#ifdef __cplusplus
} // extern "C"
#endif

#endif // SW_CLOCK_CONSTANTS_H
