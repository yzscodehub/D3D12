#include "FrameResource.h"

FrameResource::FrameResource(
    ID3D12Device *device,
    uint32_t passCount,
    uint32_t objectCount,
    uint32_t materialCount,
    uint32_t waveVertexCount)
{
    ThrowIfFailed(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(cmdListAlloc.GetAddressOf())));

    passCB = std::make_unique<UploadBuffer<PassConstants>>(device, passCount, true);

    materialBuffer = std::make_unique<UploadBuffer<MaterialData>>(device, materialCount, false);
    instanceBuffer = std::make_unique<UploadBuffer<InstanceData>>(device, objectCount, false);

    wavesVB = std::make_unique<UploadBuffer<Vertex>>(device, waveVertexCount, false);

}

FrameResource::~FrameResource() {}
