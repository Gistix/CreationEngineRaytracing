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

#       include "Raytracing/Include/SHARC/SharcCommon.h"

#   endif // SHARC_DEPENDENCY_HLSLI

#endif // SHARC