#include "ComputeBackend.hpp"
#include "CpuComputeBackend.hpp"

namespace pelpaint::render {

Backend Backend::Create(void* /*metalDevice*/)
{
    // TODO: select Vulkan/OpenGL compute backend when ENABLE_VULKAN_BACKEND is set.
    return Backend{ std::make_unique<CpuComputeBackend>() };
}

} // namespace pelpaint::render
