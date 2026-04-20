#include "rnd_seed.h"

namespace WhiteoutDex::particle {

// Default-seeded with a constant so early compaction dice rolls are
// reproducible across runs (the service re-seeds during Init if desired).
RndSeed g_globalRnd{0xA5A5A5A5u};

} // namespace WhiteoutDex::particle
