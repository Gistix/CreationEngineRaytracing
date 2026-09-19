cmake_minimum_required(VERSION 3.21)

add_executable(cert_benchmarks
    main.cpp
    ThreadPoolBench.h
    ThreadPoolBench.cpp
    WalkerBench.h
    WalkerBench.cpp
)

target_compile_features(cert_benchmarks PRIVATE cxx_std_23)

target_include_directories(cert_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

target_link_libraries(cert_benchmarks PRIVATE
    EASTL
    unordered_dense::unordered_dense
)

if(MSVC)
    target_compile_options(cert_benchmarks PRIVATE
        /arch:AVX
        /W4
        /O2
        /Oi
        /Ot
        /permissive-
        /utf-8
    )
endif()
