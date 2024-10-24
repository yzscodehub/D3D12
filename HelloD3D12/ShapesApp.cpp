#include <cstddef>

#include "ShapesApp.h"
#include "../Common/GeometryGenerator.h"

const int gNumFrameResources = 3;


ShapesApp::ShapesApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
}

bool ShapesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // 重置命令列表为初始化命令做好准备工作
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();

    BuildShapeGeometry();
    BuildRenderItems();
    BuildFrameResources();
    BuildDescriptorHeaps();
    BuildConstantBufferViews();
    BuildPSOs();

    // 执行初始化命令
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 等待初始化完成
    FlushCommandQueue();

    return true;
}

void ShapesApp::OnResize()
{
    D3DApp::OnResize();

    auto projective = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, projective);
}

void ShapesApp::Update(const GameTimer& gt)
{
    OnKeyboardInput(gt);
    UpdateCamera(gt);

    mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
    mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

    if (mCurrFrameResource->fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->fence) {
        auto eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
        ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->fence, eventHandle));
        WaitForSingleObject(eventHandle, INFINITE);
        CloseHandle(eventHandle);
    }


    // 更新
    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
    auto cmdListAlloc = mCurrFrameResource->cmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    if (mIsWireframe) {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
    }
    else {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
    }


    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 按照资源的用途指示其状态的转变，此处将资源从呈现状态转换为渲染目标状态
    auto resourceBarrierPresentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &resourceBarrierPresentToRenderTarget);

    // 清除后台缓冲区和深度缓冲区
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),
        Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    // 指定要渲染的目标缓冲区
    mCommandList->OMSetRenderTargets(1,
        &CurrentBackBufferView(),
        true,
        &DepthStencilView());

    ID3D12DescriptorHeap* descriptorHeaps[] = { mCBVDescriptorHeap.Get() };
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    auto passCBIndex = mPassCBVOffset + mCurrFrameResourceIndex;
    auto passCBVHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    passCBVHandle.Offset(passCBIndex, mCbvSrvUavDescriptorSize);
    mCommandList->SetGraphicsRootDescriptorTable(1, passCBVHandle);

    DrawRenderItems(mCommandList.Get(), mOpaqueRenderItems);

    // 按照资源的用途指示其状态的转变, 此处将资源从渲染目标状态转换为呈现状态
    auto resourceBarrierRenderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &resourceBarrierRenderTargetToPresent);

    // 完成命令记录
    ThrowIfFailed(mCommandList->Close());

    // 向命令队列添加欲执行的命令列表
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 交换后台缓冲区与前台缓冲区
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->fence = ++mCurrentFence;
    
    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0)
    {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    }
    else if ((btnState & MK_RBUTTON) != 0)
    {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.005f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.005f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 3.0f, 15.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
    if (GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
    // 将球坐标转换为笛卡尔坐标
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    // 构建观察矩阵
    auto pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    auto target = XMVectorZero();
    auto up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    auto view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
    auto currObjectCB = mCurrFrameResource->objectCB.get();
    for (auto& item : mAllRenderItems) {
        if (item->numFramesDirty > 0) {
            auto world = XMLoadFloat4x4(&item->world);
            ObjectConstants objConstans;
            XMStoreFloat4x4(&objConstans.world, XMMatrixTranspose(world));

            currObjectCB->CopyData(item->objCBIndex, objConstans);
            --item->numFramesDirty;
        }
    }
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
    auto view = XMLoadFloat4x4(&mView);
    auto proj = XMLoadFloat4x4(&mProj);
    auto viewProj = XMMatrixMultiply(view, proj);

    auto invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
    auto invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
    auto invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

    XMStoreFloat4x4(&mMainPassCB.view, XMMatrixTranspose(view));
    XMStoreFloat4x4(&mMainPassCB.proj, XMMatrixTranspose(proj));
    XMStoreFloat4x4(&mMainPassCB.viewProj, XMMatrixTranspose(viewProj));
    XMStoreFloat4x4(&mMainPassCB.invView, XMMatrixTranspose(invView));
    XMStoreFloat4x4(&mMainPassCB.invProj, XMMatrixTranspose(invProj));
    XMStoreFloat4x4(&mMainPassCB.invViewProj, XMMatrixTranspose(invViewProj));

    mMainPassCB.eyePos = mEyePos;
    mMainPassCB.renderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
    mMainPassCB.invRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.nearZ = 1.0f;
    mMainPassCB.farZ = 1000.0f;
    mMainPassCB.totalTime = gt.TotalTime();
    mMainPassCB.deltaTime = gt.DeltaTime();

    auto currPassCB = mCurrFrameResource->passCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
    auto objCount = (UINT)mOpaqueRenderItems.size();
    auto numDescriptor = (objCount + 1) * gNumFrameResources;

    // 保存渲染过程CBV的起始偏移量，在此程序中是最后3个描述符
    mPassCBVOffset = objCount * gNumFrameResources;

    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
    cbvHeapDesc.NumDescriptors = numDescriptor;
    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    cbvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCBVDescriptorHeap)));
}

void ShapesApp::BuildConstantBufferViews()
{
    // 每个帧资源的每个物体都需要一个对应的CBV描述符
    {
        UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
        auto objCount = (UINT)mOpaqueRenderItems.size();

        for (uint32_t i = 0; i < gNumFrameResources; ++i) {
            auto resource = mFrameResources[i]->objectCB->Resource();
            for (uint32_t j = 0; j < objCount; ++j) {
                auto address = resource->GetGPUVirtualAddress();
                address += j * objCBByteSize;   // 偏移到缓冲区中第j个物体的常量缓冲区地址

                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
                cbvDesc.BufferLocation = address;
                cbvDesc.SizeInBytes = objCBByteSize;

                auto heapIndex = i * objCount + j;
                auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
                handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

                md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
            }
        }
    }

    // 最后3个描述符是每帧资源的渲染过程CBV
    {
        UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

        for (auto i = 0; i < gNumFrameResources; ++i) {
            auto resource = mFrameResources[i]->passCB->Resource();
            // 每个帧资源的渲染过程缓冲区只存有一个常量缓冲区
            auto address = resource->GetGPUVirtualAddress();

            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
            cbvDesc.BufferLocation = address;
            cbvDesc.SizeInBytes = passCBByteSize;

            auto heapIndex = mPassCBVOffset + i;
            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
            handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

            md3dDevice->CreateConstantBufferView(&cbvDesc, handle);

        }
    }
}

void ShapesApp::BuildRootSignature()
{
    // 创建描述符表
    CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

    // 根参数可以是描述符表、根描述符或根常量
    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 创建根签名
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorInfo = nullptr;
    auto hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(), errorInfo.GetAddressOf());

    if (errorInfo != nullptr)
    {
        ::OutputDebugStringA(reinterpret_cast<char*>(errorInfo->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));

}

void ShapesApp::BuildShadersAndInputLayout()
{

    HRESULT hr = S_OK;

    wchar_t path[MAX_PATH];
    GetModuleFileName(NULL, path, MAX_PATH);
    std::wstring exePath(path);
    auto rf = exePath.rfind('\\');
    exePath = exePath.substr(0, rf);


    mShaders["standardVS"] = d3dUtil::CompileShader(exePath + L"/Assets/Shaders/color.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(exePath + L"/Assets/Shaders/color.hlsl", nullptr, "PS", "ps_5_1");

    mInputLayout =
    {
         {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,  offsetof(Vertex, pos), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
         {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}
    };
}

void ShapesApp::BuildShapeGeometry()
{
    GeometryGenerator geoGen;
    auto box = geoGen.CreateBox(1.5f, 0.5f, 1.5f, 3);
    auto grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    auto sphere = geoGen.CreateSphere(0.5f, 20, 20);
    auto cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT)box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT)box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT)box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

    auto totalVertexCount =
        box.Vertices.size() +
        grid.Vertices.size() +
        sphere.Vertices.size() +
        cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
    {
        vertices[k].pos = box.Vertices[i].Position;
        vertices[k].color = XMFLOAT4(DirectX::Colors::DarkGreen);
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
    {
        vertices[k].pos = grid.Vertices[i].Position;
        vertices[k].color = XMFLOAT4(DirectX::Colors::ForestGreen);
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
    {
        vertices[k].pos = sphere.Vertices[i].Position;
        vertices[k].color = XMFLOAT4(DirectX::Colors::Crimson);
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
    {
        vertices[k].pos = cylinder.Vertices[i].Position;
        vertices[k].color = XMFLOAT4(DirectX::Colors::SteelBlue);
    }

    std::vector<uint16_t> indices;
    indices.insert(indices.end(), box.GetIndices16().begin(), box.GetIndices16().end());
    indices.insert(indices.end(), grid.GetIndices16().begin(), grid.GetIndices16().end());
    indices.insert(indices.end(), sphere.GetIndices16().begin(), sphere.GetIndices16().end());
    indices.insert(indices.end(), cylinder.GetIndices16().begin(), cylinder.GetIndices16().end());

    const auto vbByteSize = (uint32_t)vertices.size() * sizeof(Vertex);
    const auto ibByteSize = (uint32_t)indices.size() * sizeof(uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    std::memcpy(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    std::memcpy(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = (UINT)ibByteSize;
    geo->VertexBufferByteSize = (UINT)vbByteSize;

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void ShapesApp::BuildPSOs()
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSODesc{};
    ZeroMemory(&opaquePSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePSODesc.InputLayout = { mInputLayout.data(), static_cast<uint32_t>(mInputLayout.size()) };
    opaquePSODesc.pRootSignature = mRootSignature.Get();
    opaquePSODesc.VS = {
        reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
        mShaders["standardVS"]->GetBufferSize()
    };
    opaquePSODesc.PS = {
        reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
        mShaders["opaquePS"]->GetBufferSize()
    };

    opaquePSODesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    opaquePSODesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    opaquePSODesc.SampleMask = UINT_MAX;
    opaquePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    opaquePSODesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
    opaquePSODesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;

    opaquePSODesc.DSVFormat = mDepthStencilFormat;
    opaquePSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);

    opaquePSODesc.NumRenderTargets = 1;
    opaquePSODesc.RTVFormats[0] = mBackBufferFormat;

    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePSODesc,
        IID_PPV_ARGS(&mPSOs["opaque"])));

    auto opaqueWireframePSODesc = opaquePSODesc;
    opaqueWireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    opaqueWireframePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePSODesc,
        IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));
}

void ShapesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.emplace_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, static_cast<uint32_t>(mAllRenderItems.size())));
    }
}

void ShapesApp::BuildRenderItems()
{
    auto boxRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRenderItem->world, 
        XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));
    boxRenderItem->objCBIndex = 0;
    boxRenderItem->geo = mGeometries["shapeGeo"].get();
    boxRenderItem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRenderItem->indexCount = boxRenderItem->geo->DrawArgs["box"].IndexCount;
    boxRenderItem->startIndexLocation = boxRenderItem->geo->DrawArgs["box"].StartIndexLocation;
    boxRenderItem->baseVertexLocation = boxRenderItem->geo->DrawArgs["box"].BaseVertexLocation;
    mAllRenderItems.emplace_back(std::move(boxRenderItem));

    auto gridRitem = std::make_unique<RenderItem>();
    gridRitem->world = MathHelper::Identity4x4();
    gridRitem->objCBIndex = 1;
    gridRitem->geo = mGeometries["shapeGeo"].get();
    gridRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRitem->indexCount = gridRitem->geo->DrawArgs["grid"].IndexCount;
    gridRitem->startIndexLocation = gridRitem->geo->DrawArgs["grid"].StartIndexLocation;
    gridRitem->baseVertexLocation = gridRitem->geo->DrawArgs["grid"].BaseVertexLocation;
    mAllRenderItems.push_back(std::move(gridRitem));

    UINT objCBIndex = 2;
    for (int i = 0; i < 5; ++i)
    {
        auto leftCylRitem = std::make_unique<RenderItem>();
        auto rightCylRitem = std::make_unique<RenderItem>();
        auto leftSphereRitem = std::make_unique<RenderItem>();
        auto rightSphereRitem = std::make_unique<RenderItem>();

        XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
        XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

        XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
        XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

        XMStoreFloat4x4(&leftCylRitem->world, rightCylWorld);
        leftCylRitem->objCBIndex = objCBIndex++;
        leftCylRitem->geo = mGeometries["shapeGeo"].get();
        leftCylRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftCylRitem->indexCount = leftCylRitem->geo->DrawArgs["cylinder"].IndexCount;
        leftCylRitem->startIndexLocation = leftCylRitem->geo->DrawArgs["cylinder"].StartIndexLocation;
        leftCylRitem->baseVertexLocation = leftCylRitem->geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&rightCylRitem->world, leftCylWorld);
        rightCylRitem->objCBIndex = objCBIndex++;
        rightCylRitem->geo = mGeometries["shapeGeo"].get();
        rightCylRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightCylRitem->indexCount = rightCylRitem->geo->DrawArgs["cylinder"].IndexCount;
        rightCylRitem->startIndexLocation = rightCylRitem->geo->DrawArgs["cylinder"].StartIndexLocation;
        rightCylRitem->baseVertexLocation = rightCylRitem->geo->DrawArgs["cylinder"].BaseVertexLocation;

        XMStoreFloat4x4(&leftSphereRitem->world, leftSphereWorld);
        leftSphereRitem->objCBIndex = objCBIndex++;
        leftSphereRitem->geo = mGeometries["shapeGeo"].get();
        leftSphereRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        leftSphereRitem->indexCount = leftSphereRitem->geo->DrawArgs["sphere"].IndexCount;
        leftSphereRitem->startIndexLocation = leftSphereRitem->geo->DrawArgs["sphere"].StartIndexLocation;
        leftSphereRitem->baseVertexLocation = leftSphereRitem->geo->DrawArgs["sphere"].BaseVertexLocation;

        XMStoreFloat4x4(&rightSphereRitem->world, rightSphereWorld);
        rightSphereRitem->objCBIndex = objCBIndex++;
        rightSphereRitem->geo = mGeometries["shapeGeo"].get();
        rightSphereRitem->primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        rightSphereRitem->indexCount = rightSphereRitem->geo->DrawArgs["sphere"].IndexCount;
        rightSphereRitem->startIndexLocation = rightSphereRitem->geo->DrawArgs["sphere"].StartIndexLocation;
        rightSphereRitem->baseVertexLocation = rightSphereRitem->geo->DrawArgs["sphere"].BaseVertexLocation;

        mAllRenderItems.push_back(std::move(leftCylRitem));
        mAllRenderItems.push_back(std::move(rightCylRitem));
        mAllRenderItems.push_back(std::move(leftSphereRitem));
        mAllRenderItems.push_back(std::move(rightSphereRitem));
    }

    for (auto& e : mAllRenderItems)
        mOpaqueRenderItems.push_back(e.get());
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems)
{
    for (const auto& item : renderItems) {
        cmdList->IASetIndexBuffer(&item->geo->IndexBufferView());
        cmdList->IASetVertexBuffers(0, 1, &item->geo->VertexBufferView());
        cmdList->IASetPrimitiveTopology(item->primitiveType);

        auto cbvIndex = mCurrFrameResourceIndex * (uint32_t)mOpaqueRenderItems.size() + item->objCBIndex;
        auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);
        cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);

        cmdList->DrawIndexedInstanced(item->indexCount, 1, item->startIndexLocation, item->baseVertexLocation, 0);
    }
}
