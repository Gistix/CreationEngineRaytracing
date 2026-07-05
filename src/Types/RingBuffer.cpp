#include "Types/RingBuffer.h"
#include "Renderer.h"

nvrhi::IBuffer* RingBuffer::current() const
{
	return handles[Renderer::GetSingleton()->GetCurrentSlot()];
}
