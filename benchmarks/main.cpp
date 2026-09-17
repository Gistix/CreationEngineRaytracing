#include "ThreadPoolBench.h"
#include "WalkerBench.h"

#include <cstdint>
#include <iostream>

// EASTL required allocator overrides
void* operator new[](size_t size, const char*, int, unsigned, const char*, int)
{
	return new uint8_t[size];
}

void* operator new[](size_t size, size_t, size_t, const char*, int, unsigned, const char*, int)
{
	return new uint8_t[size];
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	std::cout << "Creation Engine Raytracing: CPU Microbenchmarks\n";
	std::cout << "Target: ThreadPool & ParallelTriShapeWalker Scene Traversal\n\n";

	Bench::RunThreadPoolBenchmarks();
	Bench::RunWalkerBenchmarks();

	std::cout << "\nAll benchmarks completed successfully.\n";
	return 0;
}
