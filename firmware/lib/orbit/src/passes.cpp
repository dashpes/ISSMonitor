#include "orbit/passes.h"

#include <cmath>

namespace orbit {

// TODO(daniel): see orbit/passes.h for the spec.
//
// Suggested shape:
//   t = from_unix
//   while (t < from_unix + horizon_s && found < max_passes) {
//       el = elevation at t
//       if (el > min) {
//           aos = bisect backwards between t - step and t
//           walk forward tracking the max
//           los = bisect forward across the down-crossing
//           record it, jump t past los
//       }
//       t += coarse_step
//   }
//
// Pick coarse_step small enough that you cannot step over a whole pass.
// An ISS pass is roughly 6 minutes end to end, so 60 s is safe and 300 s
// is not. Prove it with a test rather than trusting that sentence.

int findPasses(SubpointFn subpoint, void* ctx,
               const GeoPoint& observer,
               int64_t from_unix,
               int32_t horizon_s,
               double  min_elevation_deg,
               int32_t coarse_step_s,
               Pass*   out,
               int     max_passes) {
    (void)subpoint; (void)ctx; (void)observer; (void)from_unix;
    (void)horizon_s; (void)min_elevation_deg; (void)coarse_step_s;
    (void)out; (void)max_passes;
    return 0;
}

bool isVisiblePass(const GeoPoint& satellite,
                   const GeoPoint& observer,
                   int64_t unix_time) {
    (void)satellite; (void)observer; (void)unix_time;
    return false;
}

}  // namespace orbit
