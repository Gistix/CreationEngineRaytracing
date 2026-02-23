#ifndef INTEROP_H
#define INTEROP_H

#	ifdef __cplusplus
#		define INTEROP_STRUCT(name, alignment) struct alignas(alignment) name
#		define INTEROP_DATA_STRUCT(name, alignment) struct alignas(alignment) name##Data
#		define INTEROP_DATA_TYPE(name) name##Data
#		define INTEROP_ROW_MAJOR(type) type
#		define INTEROP_NAMESPACE(name) namespace name

// -----------------------------------------------------
// Core constant buffer validation
// -----------------------------------------------------
#       define VALIDATE_CBUFFER(T, EXPECTED_ALIGNMENT)                 \
    static_assert(std::is_standard_layout_v<T>,                        \
        #T " must be standard layout");                                \
                                                                       \
    static_assert(std::is_trivially_copyable_v<T>,                     \
        #T " must be trivially copyable");                             \
                                                                       \
    static_assert(sizeof(T) % EXPECTED_ALIGNMENT == 0,                 \
        #T " size must be multiple of " #EXPECTED_ALIGNMENT " (HLSL packing rule)"); \
                                                                       \
    static_assert(alignof(T) == EXPECTED_ALIGNMENT,                    \
        #T " must be " #EXPECTED_ALIGNMENT "-byte aligned")

// -----------------------------------------------------
// Explicit alignment validation
// -----------------------------------------------------
#       define VALIDATE_ALIGNMENT(T, EXPECTED_ALIGNMENT)               \
    static_assert(sizeof(T) % EXPECTED_ALIGNMENT == 0,                 \
        #T " size must be multiple of " #EXPECTED_ALIGNMENT " (HLSL packing rule)"); \
                                                                       \
    static_assert(alignof(T) == EXPECTED_ALIGNMENT,                    \
        #T " must be " #EXPECTED_ALIGNMENT "-byte aligned")

// -----------------------------------------------------
// Explicit size validation
// -----------------------------------------------------
#       define VALIDATE_SIZE(T, EXPECTED_SIZE)                          \
    static_assert(sizeof(T) == EXPECTED_SIZE,                           \
        #T " size mismatch")

// -----------------------------------------------------
// Explicit offset validation
// -----------------------------------------------------
#       define VALIDATE_OFFSET(T, MEMBER, EXPECTED_OFFSET)              \
    static_assert(offsetof(T, MEMBER) == EXPECTED_OFFSET,               \
        #T "::" #MEMBER " offset mismatch")

#	else

#		define INTEROP_STRUCT(name, alignment) struct name
#		define INTEROP_DATA_STRUCT(name, alignment) struct name
#		define INTEROP_DATA_TYPE(name) name
#		define INTEROP_ROW_MAJOR(type) row_major type
#		define INTEROP_NAMESPACE(name)

#       define VALIDATE_CBUFFER(T, EXPECTED_ALIGNMENT)
#       define VALIDATE_ALIGNMENT(T, EXPECTED_ALIGNMENT)
#       define VALIDATE_SIZE(T, EXPECTED_SIZE)
#       define VALIDATE_OFFSET(T, MEMBER, EXPECTED_OFFSET)

#   endif

#endif