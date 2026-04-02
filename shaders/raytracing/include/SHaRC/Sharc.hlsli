#if defined(SHARC)

#   ifndef SHARC_DEPENDENCY_HLSLI
#       define SHARC_DEPENDENCY_HLSLI

#       define SHARC_ENABLE_64_BIT_ATOMICS 0

#       ifndef SHARC_UPDATE
#           define SHARC_UPDATE 0
#       endif

#       ifndef SHARC_RESOLVE
#           define SHARC_RESOLVE 0
#       endif

#       ifndef SHARC_CAPACITY
#           define SHARC_CAPACITY (4 * 1024 * 1024)    
#       endif

#       define SHARC_SEPARATE_EMISSIVE 1

// In query path (non-update, non-resolve), buffers are bound as read-only SRVs.
// Override RW_STRUCTURED_BUFFER to expand to StructuredBuffer for type compatibility.
#       if !SHARC_UPDATE && !SHARC_RESOLVE
#           define RW_STRUCTURED_BUFFER(name, type) StructuredBuffer<type> name
#       endif

#       include "Raytracing/Include/SHARC/SharcCommon.h"

#   endif // SHARC_DEPENDENCY_HLSLI

#endif // SHARC