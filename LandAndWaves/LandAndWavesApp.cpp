#include <cstddef>

#include "../Common/DDSTextureLoader.h"
#include "../Common/GeometryGenerator.h"
#include "LandAndWavesApp.h"

const int gNumFrameResources = 3;

LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance)
    : D3DApp(hInstance)
{}

bool LandAndWavesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    //Set4xMsaaState(true);

    // 重置命令列表为初始化命令做好准备工作
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    LoadTextures();
    BuildMaterial();
    BuildLandGeometry();
    BuildWaveGeometryBuffers();
    BuildBoxGeometry();
    BuildRoomGeometry();
    BuildSkullGeometry();
    BuildTreeSpritesGeometry();

    BuildRenderItems();
    BuildFrameResources();

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildDescriptorHeaps();
    BuildPSOs();

    // 执行初始化命令
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList *cmdsLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 等待初始化完成
    FlushCommandQueue();

    return true;
}

void LandAndWavesApp::OnResize()
{
    D3DApp::OnResize();

    auto projective = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, projective);
}

void LandAndWavesApp::Update(const GameTimer &gt)
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
    AnimateMaterials(gt);
    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);
    UpdateMaterialCB(gt);
    UpdateReflectedPassCB(gt);
}

void LandAndWavesApp::Draw(const GameTimer &gt)
{
    auto cmdListAlloc = mCurrFrameResource->cmdListAlloc;
    ThrowIfFailed(cmdListAlloc->Reset());

    if (mIsWireframe) {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
    } else {
        ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
    }

    mCommandList->RSSetViewports(1, &mScreenViewport);
    mCommandList->RSSetScissorRects(1, &mScissorRect);

    // 按照资源的用途指示其状态的转变，此处将资源从呈现状态转换为渲染目标状态
    auto resourceBarrierPresentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &resourceBarrierPresentToRenderTarget);

    // 清除后台缓冲区和深度缓冲区
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(
        DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

    // 指定要渲染的目标缓冲区
    mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

    ID3D12DescriptorHeap *descriptorHeaps[] = {mSRVDescriptorHeap.Get()};
    mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    //auto passCBIndex = mPassCBVOffset + mCurrFrameResourceIndex;
    //auto passCBVHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    //passCBVHandle.Offset(passCBIndex, mCbvSrvUavDescriptorSize);
    //mCommandList->SetGraphicsRootDescriptorTable(1, passCBVHandle);
    auto resource = mCurrFrameResource->passCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(2, resource->GetGPUVirtualAddress());

    // 先绘制不透明物体
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Opaque)]);

    mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::AlphaTested)]);

    // 绘制tree sprite
    mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::AlphaTestedTreeSprites)]);

    // 将模板缓冲区中可见的镜面像素标记为1
    mCommandList->OMSetStencilRef(1);
    mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Mirrors)]);

    // 只绘制镜子范围内的镜像（即仅绘制模板缓冲区中标记为1的像素）
    // 注意我们必须使用两个单独的渲染过程常量缓冲区，一个存储物体镜像，另一个保存光照镜像
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    mCommandList
        ->SetGraphicsRootConstantBufferView(2, resource->GetGPUVirtualAddress() + 1 * passCBByteSize);
    mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Reflected)]);

    mCommandList->SetGraphicsRootConstantBufferView(2, resource->GetGPUVirtualAddress());
    mCommandList->OMSetStencilRef(0);

    // 绘制透明的镜面，使镜像可以与之融合
    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Transparent)]);

    mCommandList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Shadow)]);

    // 按照资源的用途指示其状态的转变, 此处将资源从渲染目标状态转换为呈现状态
    auto resourceBarrierRenderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &resourceBarrierRenderTargetToPresent);

    // 完成命令记录
    ThrowIfFailed(mCommandList->Close());

    // 向命令队列添加欲执行的命令列表
    ID3D12CommandList *cmdsLists[] = {mCommandList.Get()};
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // 交换后台缓冲区与前台缓冲区
    ThrowIfFailed(mSwapChain->Present(0, 0));
    mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

    mCurrFrameResource->fence = ++mCurrentFence;

    mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void LandAndWavesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
    mLastMousePos.x = x;
    mLastMousePos.y = y;

    SetCapture(mhMainWnd);
}

void LandAndWavesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
    ReleaseCapture();
}

void LandAndWavesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
    if ((btnState & MK_LBUTTON) != 0) {
        // Make each pixel correspond to a quarter of a degree.
        float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
        float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

        // Update angles based on input to orbit camera around box.
        mTheta += dx;
        mPhi += dy;

        // Restrict the angle mPhi.
        mPhi = MathHelper::Clamp(mPhi, 0.1f, MathHelper::Pi - 0.1f);
    } else if ((btnState & MK_RBUTTON) != 0) {
        // Make each pixel correspond to 0.005 unit in the scene.
        float dx = 0.2f * static_cast<float>(x - mLastMousePos.x);
        float dy = 0.2f * static_cast<float>(y - mLastMousePos.y);

        // Update the camera radius based on input.
        mRadius += dx - dy;

        // Restrict the radius.
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void LandAndWavesApp::OnKeyboardInput(const GameTimer &gt)
{
    if (GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;

    const float dt = gt.DeltaTime();
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
        mSunTheta -= 1.0f * dt;
        mSkullTranslation.x -= 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
        mSunTheta += 1.0f * dt;
        mSkullTranslation.x += 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        mSunPhi -= 1.0f * dt;
        mSkullTranslation.y += 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        mSunPhi += 1.0f * dt;
        mSkullTranslation.y -= 1.0f * dt;
    }

    mSunPhi = MathHelper::Clamp(mSunPhi, 0.1f, XM_PIDIV2);

    // Don't let user move below ground plane.
    mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);

    // Update the new world matrix.
    XMMATRIX skullRotate = XMMatrixRotationY(0.5f * MathHelper::Pi);
    XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
    XMMATRIX skullOffset
        = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRenderItem->world, skullWorld);

    // Update reflection world matrix.
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedSkullRenderItem->world, skullWorld * R);

    // Update shadow world matrix.
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
    XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassCB.lights[0].Direction);
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
    XMStoreFloat4x4(&mShadowedSkullRenderItem->world, skullWorld * S * shadowOffsetY);

    mSkullRenderItem->numFramesDirty = gNumFrameResources;
    mReflectedSkullRenderItem->numFramesDirty = gNumFrameResources;
    mShadowedSkullRenderItem->numFramesDirty = gNumFrameResources;
}

void LandAndWavesApp::AnimateMaterials(const GameTimer &gt)
{
    // Scroll the water material texture coordinates.
    auto waterMat = mMaterials["water"].get();

    float &tu = waterMat->MatTransform(3, 0);
    float &tv = waterMat->MatTransform(3, 1);

    tu += 0.1f * gt.DeltaTime();
    tv += 0.02f * gt.DeltaTime();

    if (tu >= 1.0f)
        tu -= 1.0f;

    if (tv >= 1.0f)
        tv -= 1.0f;

    waterMat->MatTransform(3, 0) = tu;
    waterMat->MatTransform(3, 1) = tv;

    // Material has changed, so need to update cbuffer.
    waterMat->NumFramesDirty = gNumFrameResources;
}

void LandAndWavesApp::UpdateCamera(const GameTimer &gt)
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

void LandAndWavesApp::UpdateObjectCBs(const GameTimer &gt)
{
    auto currObjectCB = mCurrFrameResource->objectCB.get();
    for (auto &item : mAllRenderItems) {
        if (item->numFramesDirty > 0) {
            ObjectConstants objConstans;
            auto world = XMLoadFloat4x4(&item->world);
            XMStoreFloat4x4(&objConstans.world, XMMatrixTranspose(world));

            auto texTransform = XMLoadFloat4x4(&item->texTransform);
            XMStoreFloat4x4(&objConstans.texTransform, XMMatrixTranspose(texTransform));

            currObjectCB->CopyData(item->objCBIndex, objConstans);
            --item->numFramesDirty;
        }
    }
}

void LandAndWavesApp::UpdateMainPassCB(const GameTimer &gt)
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
    mMainPassCB.renderTargetSize = XMFLOAT2((float) mClientWidth, (float) mClientHeight);
    mMainPassCB.invRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
    mMainPassCB.nearZ = 1.0f;
    mMainPassCB.farZ = 1000.0f;
    mMainPassCB.totalTime = gt.TotalTime();
    mMainPassCB.deltaTime = gt.DeltaTime();
    mMainPassCB.ambientLight = {0.25f, 0.25f, 0.35f, 1.0f};
    mMainPassCB.lights[0].Direction = {0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.lights[0].Strength = {0.9f, 0.9f, 0.8f};
    mMainPassCB.lights[1].Direction = {-0.57735f, -0.57735f, 0.57735f};
    mMainPassCB.lights[1].Strength = {0.3f, 0.3f, 0.3f};
    mMainPassCB.lights[2].Direction = {0.0f, -0.707f, -0.707f};
    mMainPassCB.lights[2].Strength = {0.15f, 0.15f, 0.15f};

    auto currPassCB = mCurrFrameResource->passCB.get();
    currPassCB->CopyData(0, mMainPassCB);
}

void LandAndWavesApp::UpdateMaterialCB(const GameTimer &gt)
{
    auto materialCB = mCurrFrameResource->materialCB.get();
    for (const auto &it : mMaterials) {
        auto material = it.second.get();

        MaterialConstants mc;
        mc.DiffuseAlbedo = material->DiffuseAlbedo;
        mc.FresnelR0 = material->FresnelR0;
        mc.Roughness = material->Roughness;

        XMMATRIX matTransform = XMLoadFloat4x4(&material->MatTransform);
        XMStoreFloat4x4(&mc.MatTransform, XMMatrixTranspose(matTransform));

        materialCB->CopyData(material->MatCBIndex, mc);

        --material->NumFramesDirty;
    }
}

void LandAndWavesApp::UpdateReflectedPassCB(const GameTimer &gt)
{
    mReflectedPassCB = mMainPassCB;

    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);

    for (UINT i = 0; i < 3; ++i) {
        auto lightDir = XMLoadFloat3(&mReflectedPassCB.lights[i].Direction);
        auto reflectedLightDir = XMVector3TransformNormal(lightDir, R);
        XMStoreFloat3(&mReflectedPassCB.lights[i].Direction, reflectedLightDir);
    }

    mCurrFrameResource->passCB->CopyData(1, mReflectedPassCB);
}

void LandAndWavesApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);
    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i) {
        auto &p = grid.Vertices[i].Position;
        vertices[i].pos = p;
        vertices[i].pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].normal = GetHillsNormal(p.x, p.z);
        vertices[i].texCoord = grid.Vertices[i].TexC;
    }

    // 先创建顶点和索引缓冲，将数据拷贝到上传堆，再从上传堆提交到默认堆（此步使用命令列表记录并提交到命令队列来执行）
    const uint32_t vbByteSize = vertices.size() * sizeof(Vertex);
    const uint32_t ibByteSize = grid.GetIndices16().size() * sizeof(uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "landGeo";
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->VertexByteStride = sizeof(Vertex);
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;

    D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU);
    std::memcpy(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU);
    std::memcpy(geo->IndexBufferCPU->GetBufferPointer(), grid.GetIndices16().data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        geo->VertexBufferCPU->GetBufferPointer(),
        vbByteSize,
        geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(),
        mCommandList.Get(),
        geo->IndexBufferCPU->GetBufferPointer(),
        ibByteSize,
        geo->IndexBufferUploader);

    SubmeshGeometry subMesh;
    subMesh.IndexCount = grid.GetIndices16().size();
    subMesh.BaseVertexLocation = 0;
    subMesh.StartIndexLocation = 0;

    geo->DrawArgs["grid"] = subMesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildWaveGeometryBuffers()
{
    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    std::vector<uint16_t> indices(3 * mWaves->TriangleCount());

    int m = mWaves->RowCount();
    int n = mWaves->ColumnCount();
    int k = 0;
    for (int i = 0; i < m - 1; ++i) {
        for (int j = 0; j < n - 1; ++j) {
            indices[k] = i * n + j;
            indices[k + 1] = i * n + j + 1;
            indices[k + 2] = (i + 1) * n + j;

            indices[k + 3] = (i + 1) * n + j;
            indices[k + 4] = i * n + j + 1;
            indices[k + 5] = (i + 1) * n + j + 1;

            k += 6; // next quad
        }
    }

    UINT vbByteSize = mWaves->VertexCount() * sizeof(Vertex);
    UINT ibByteSize = (UINT) indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    // 浪的顶点是动态数据，所以仅将索引存入默认堆中
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT) indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildBoxGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

    std::vector<Vertex> vertices(box.Vertices.size());
    for (size_t i = 0; i < box.Vertices.size(); ++i) {
        auto &p = box.Vertices[i].Position;
        vertices[i].pos = p;
        vertices[i].normal = box.Vertices[i].Normal;
        vertices[i].texCoord = box.Vertices[i].TexC;
    }

    const UINT vbByteSize = (UINT) vertices.size() * sizeof(Vertex);

    std::vector<std::uint16_t> indices = box.GetIndices16();
    const UINT ibByteSize = (UINT) indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "boxGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT) indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["box"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildRoomGeometry()
{
    // Create and specify geometry.  For this sample we draw a floor
    // and a wall with a mirror on it.  We put the floor, wall, and
    // mirror geometry in one vertex buffer.
    //
    //   |--------------|
    //   |              |
    //   |----|----|----|
    //   |Wall|Mirr|Wall|
    //   |    | or |    |
    //   /--------------/
    //  /   Floor      /
    // /--------------/

    std::array<Vertex, 20> vertices
        = {// Floor: Observe we tile texture coordinates.
           Vertex(-3.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 0.0f, 4.0f), // 0
           Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f),
           Vertex(7.5f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 4.0f, 0.0f),
           Vertex(7.5f, 0.0f, -10.0f, 0.0f, 1.0f, 0.0f, 4.0f, 4.0f),

           // Wall: Observe we tile texture coordinates, and that we
           // leave a gap in the middle for the mirror.
           Vertex(-3.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 4
           Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
           Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 0.0f),
           Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.5f, 2.0f),

           Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 2.0f), // 8
           Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
           Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 0.0f),
           Vertex(7.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 2.0f, 2.0f),

           Vertex(-3.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 12
           Vertex(-3.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
           Vertex(7.5f, 6.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 0.0f),
           Vertex(7.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 6.0f, 1.0f),

           // Mirror
           Vertex(-2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 1.0f), // 16
           Vertex(-2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 0.0f, 0.0f),
           Vertex(2.5f, 4.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 0.0f),
           Vertex(2.5f, 0.0f, 0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f)};

    std::array<std::int16_t, 30> indices = {// Floor
                                            0,
                                            1,
                                            2,
                                            0,
                                            2,
                                            3,

                                            // Walls
                                            4,
                                            5,
                                            6,
                                            4,
                                            6,
                                            7,

                                            8,
                                            9,
                                            10,
                                            8,
                                            10,
                                            11,

                                            12,
                                            13,
                                            14,
                                            12,
                                            14,
                                            15,

                                            // Mirror
                                            16,
                                            17,
                                            18,
                                            16,
                                            18,
                                            19};
    SubmeshGeometry floorSubmesh;
    floorSubmesh.IndexCount = 6;
    floorSubmesh.StartIndexLocation = 0;
    floorSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry wallSubmesh;
    wallSubmesh.IndexCount = 18;
    wallSubmesh.StartIndexLocation = 6;
    wallSubmesh.BaseVertexLocation = 0;

    SubmeshGeometry mirrorSubmesh;
    mirrorSubmesh.IndexCount = 6;
    mirrorSubmesh.StartIndexLocation = 24;
    mirrorSubmesh.BaseVertexLocation = 0;

    const UINT vbByteSize = (UINT) vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT) indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "roomGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    geo->DrawArgs["floor"] = floorSubmesh;
    geo->DrawArgs["wall"] = wallSubmesh;
    geo->DrawArgs["mirror"] = mirrorSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildSkullGeometry()
{
    auto appPath = UnicodeToUTF8(GetAppPath());
    auto modelFilePath = appPath + "/Assets/Models/skull.txt";
    std::ifstream fin(modelFilePath);

    if (!fin) {
        MessageBox(0, UTF8ToUnicode(modelFilePath).c_str(), 0, 0);
        return;
    }

    UINT vcount = 0;
    UINT tcount = 0;
    std::string ignore;

    fin >> ignore >> vcount;
    fin >> ignore >> tcount;
    fin >> ignore >> ignore >> ignore >> ignore;

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i) {
        fin >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
        fin >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;

        // Model does not have texture coordinates, so just zero them out.
        vertices[i].texCoord = {0.0f, 0.0f};
    }

    fin >> ignore;
    fin >> ignore;
    fin >> ignore;

    std::vector<std::int32_t> indices(3 * tcount);
    for (UINT i = 0; i < tcount; ++i) {
        fin >> indices[i * 3 + 0] >> indices[i * 3 + 1] >> indices[i * 3 + 2];
    }

    fin.close();

    //
    // Pack the indices of all the meshes into one index buffer.
    //

    const UINT vbByteSize = (UINT) vertices.size() * sizeof(Vertex);

    const UINT ibByteSize = (UINT) indices.size() * sizeof(std::int32_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "skullGeo";

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R32_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT) indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["skull"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildTreeSpritesGeometry()
{
    static constexpr int treeCount = 16;
    std::array<TreeSpriteVertex, treeCount> vertices;
    for (UINT i = 0; i < treeCount; ++i) {
        float x = MathHelper::RandF(-45.0f, 45.0f);
        float z = MathHelper::RandF(-45.0f, 45.0f);
        float y = GetHillsHeight(x, z);

        // Move tree slightly above land height.
        y += 3.0f;

        vertices[i].pos = XMFLOAT3(x, y, z);
        vertices[i].size = XMFLOAT2(20.0f, 20.0f);
    }

    std::array<std::uint16_t, 16> indices = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

    UINT vbByteSize = vertices.size() * sizeof(TreeSpriteVertex);
    UINT ibByteSize = indices.size() * sizeof(uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "treeSpritesGeo";
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexBufferByteSize = ibByteSize;
    geo->VertexByteStride = sizeof(TreeSpriteVertex);
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;

    ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
    std::memcpy(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    std::memcpy(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(
        md3dDevice.Get(), mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    SubmeshGeometry submesh;
    submesh.IndexCount = indices.size();
    submesh.BaseVertexLocation = 0;
    submesh.StartIndexLocation = 0;

    geo->DrawArgs["points"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildDescriptorHeaps()
{
    // 创建描述符堆`
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    srvHeapDesc.NumDescriptors = mTextures.size();
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVDescriptorHeap)));

    // 使用SRV填充描述符堆
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    std::vector<ID3D12Resource *> textureResources{
        mTextures["grassTex"]->Resource.Get(),
        mTextures["waterTex"]->Resource.Get(),
        mTextures["fenceTex"]->Resource.Get(),
        mTextures["bricksTex"]->Resource.Get(),
        mTextures["checkboardTex"]->Resource.Get(),
        mTextures["iceTex"]->Resource.Get(),
        mTextures["white1x1Tex"]->Resource.Get(),
    };
    auto grassTex = mTextures["grassTex"]->Resource;
    auto waterTex = mTextures["waterTex"]->Resource;
    auto fenceTex = mTextures["fenceTex"]->Resource;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = -1;

    for (const auto resource : textureResources) {
        srvDesc.Format = resource->GetDesc().Format;
        md3dDevice->CreateShaderResourceView(resource, &srvDesc, handle);
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // 纹理数组
    auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Format = treeArrayTex->GetDesc().Format;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = -1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
    md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, handle);
}

void LandAndWavesApp::BuildRootSignature()
{
    // 创建描述符表
    //CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    //cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    //CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    //cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

    //// 根参数可以是描述符表、根描述符或根常量
    //CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    //slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    //slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);

    CD3DX12_DESCRIPTOR_RANGE srvTable;
    srvTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

    CD3DX12_ROOT_PARAMETER slotRootParameter[4];
    slotRootParameter[0]
        .InitAsDescriptorTable(1, &srvTable, D3D12_SHADER_VISIBILITY_PIXEL); // srvTabel
    slotRootParameter[1].InitAsConstantBufferView(0);                        // objectCBV
    slotRootParameter[2].InitAsConstantBufferView(1);                        // passCBV
    slotRootParameter[3].InitAsConstantBufferView(2);                        // materialCBV

    auto staticSamplers = GetStaticSamplers();

    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
        _countof(slotRootParameter),
        slotRootParameter,
        (uint32_t) staticSamplers.size(),
        staticSamplers.data(),
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // 创建根签名
    ComPtr<ID3DBlob> serializedRootSig = nullptr;
    ComPtr<ID3DBlob> errorInfo = nullptr;
    auto hr = D3D12SerializeRootSignature(
        &rootSigDesc,
        D3D_ROOT_SIGNATURE_VERSION_1,
        serializedRootSig.GetAddressOf(),
        errorInfo.GetAddressOf());

    if (errorInfo != nullptr) {
        ::OutputDebugStringA(reinterpret_cast<char *>(errorInfo->GetBufferPointer()));
    }
    ThrowIfFailed(hr);

    ThrowIfFailed(md3dDevice->CreateRootSignature(
        0,
        serializedRootSig->GetBufferPointer(),
        serializedRootSig->GetBufferSize(),
        IID_PPV_ARGS(&mRootSignature)));
}

void LandAndWavesApp::BuildShadersAndInputLayout()
{
    HRESULT hr = S_OK;

    const D3D_SHADER_MACRO defines[] = {
        {"FOG", "1"},
        {nullptr, nullptr},
    };

    const D3D_SHADER_MACRO alphaTestDefines[] = {
        {"FOG", "1"},
        {"ALPHA_TEST", "1"},
        {nullptr, nullptr},
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["opaquePS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", defines, "PS", "ps_5_0");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mShaders["treeSpriteVS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", nullptr, "VS", "vs_5_0");
    mShaders["treeSpriteGS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", nullptr, "GS", "gs_5_0");
    mShaders["treeSpritePS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_0");

    mInputLayout
        = {{"POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            offsetof(Vertex, pos),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0},
           {"NORMAL",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            offsetof(Vertex, normal),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0},
           {"TEXCOORD",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            offsetof(Vertex, texCoord),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0}};

    mTreeSpriteInputLayout
        = {{"POSITION",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            offsetof(TreeSpriteVertex, pos),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0},
           {"SIZE",
            0,
            DXGI_FORMAT_R32G32_FLOAT,
            0,
            offsetof(TreeSpriteVertex, size),
            D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA,
            0}};
}

void LandAndWavesApp::BuildPSOs()
{
    // opaque PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePSODesc{};
    ZeroMemory(&opaquePSODesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
    opaquePSODesc.InputLayout = {mInputLayout.data(), static_cast<uint32_t>(mInputLayout.size())};
    opaquePSODesc.pRootSignature = mRootSignature.Get();
    opaquePSODesc.VS
        = {reinterpret_cast<BYTE *>(mShaders["standardVS"]->GetBufferPointer()),
           mShaders["standardVS"]->GetBufferSize()};
    opaquePSODesc.PS
        = {reinterpret_cast<BYTE *>(mShaders["opaquePS"]->GetBufferPointer()),
           mShaders["opaquePS"]->GetBufferSize()};

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
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&opaquePSODesc, IID_PPV_ARGS(&mPSOs["opaque"])));

    // wireframe PSO
    auto opaqueWireframePSODesc = opaquePSODesc;
    opaqueWireframePSODesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    opaqueWireframePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &opaqueWireframePSODesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

    // transparent PSO
    auto transparentPSODesc = opaquePSODesc;
    D3D12_RENDER_TARGET_BLEND_DESC transparentBlendDesc;
    transparentBlendDesc.BlendEnable = true;
    transparentBlendDesc.LogicOpEnable = false;
    transparentBlendDesc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparentBlendDesc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
    transparentBlendDesc.BlendOp = D3D12_BLEND_OP_ADD;
    transparentBlendDesc.SrcBlendAlpha = D3D12_BLEND_ONE;
    transparentBlendDesc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparentBlendDesc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparentBlendDesc.LogicOp = D3D12_LOGIC_OP_NOOP;
    transparentBlendDesc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    transparentPSODesc.BlendState.RenderTarget[0] = transparentBlendDesc;
    ThrowIfFailed(
        md3dDevice
            ->CreateGraphicsPipelineState(&transparentPSODesc, IID_PPV_ARGS(&mPSOs["transparent"])));

    // alpha tested PSO
    auto alphaTestedPSODesc = opaquePSODesc;
    alphaTestedPSODesc.PS = {
        mShaders["alphaTestedPS"]->GetBufferPointer(),
        mShaders["alphaTestedPS"]->GetBufferSize(),
    };
    alphaTestedPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(
        md3dDevice
            ->CreateGraphicsPipelineState(&alphaTestedPSODesc, IID_PPV_ARGS(&mPSOs["alphaTested"])));

    // 用于标记模板缓冲区镜面部分的PSO
    // 禁止对渲染目标的写操作
    CD3DX12_BLEND_DESC mirrorBlendDesc(D3D12_DEFAULT);
    mirrorBlendDesc.RenderTarget[0].RenderTargetWriteMask = 0;

    D3D12_DEPTH_STENCIL_DESC mirrorDSDesc;
    mirrorDSDesc.DepthEnable = true;
    mirrorDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
    mirrorDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 定义深度测试所用的比较函数
    mirrorDSDesc.StencilEnable = true;
    mirrorDSDesc.StencilReadMask = 0xFF;
    mirrorDSDesc.StencilWriteMask = 0xFF;

    // 描述当像素片段在模板测试失败时，应该怎样更新模板缓冲区
    mirrorDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    // 描述当像素片段通过模板测试，却在深度缓冲区失败时，应该怎样更新模板缓冲区
    mirrorDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    // 描述当像素片段通过模板测试与深度测试时，应该怎样更新模板缓冲区
    mirrorDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    // 定义模板测试所用的比较函数
    mirrorDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    // 我们不渲染背面朝向的多边形，因而对这些参数的设置并不关心
    mirrorDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    mirrorDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    mirrorDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS;

    auto markMirrorsPSODesc = opaquePSODesc;
    markMirrorsPSODesc.BlendState = mirrorBlendDesc;
    markMirrorsPSODesc.DepthStencilState = mirrorDSDesc;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &markMirrorsPSODesc, IID_PPV_ARGS(&mPSOs["markStencilMirrors"])));

    // 用于渲染模板缓冲区中反射镜像的PSO
    D3D12_DEPTH_STENCIL_DESC reflectionsDSDesc;
    reflectionsDSDesc.DepthEnable = true;
    reflectionsDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    reflectionsDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS; // 定义深度测试所用的比较函数
    reflectionsDSDesc.StencilEnable = true;
    reflectionsDSDesc.StencilReadMask = 0xFF;
    reflectionsDSDesc.StencilWriteMask = 0xFF;

    // 定义模板测试所用的比较函数
    reflectionsDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    // 描述当像素片段在模板测试失败时，应该怎样更新模板缓冲区
    reflectionsDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    // 描述当像素片段通过模板测试，却在深度缓冲区失败时，应该怎样更新模板缓冲区
    reflectionsDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    // 描述当像素片段通过模板测试与深度测试时，应该怎样更新模板缓冲区
    reflectionsDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;

    // 我们不渲染背面朝向的多边形，因而对这些参数的设置并不关心
    reflectionsDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;
    reflectionsDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    reflectionsDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;

    auto drawReflecttionsPSODesc = opaquePSODesc;
    drawReflecttionsPSODesc.DepthStencilState = reflectionsDSDesc;
    drawReflecttionsPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    drawReflecttionsPSODesc.RasterizerState.FrontCounterClockwise = true;
    ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(
        &drawReflecttionsPSODesc, IID_PPV_ARGS(&mPSOs["drawStencilReflections"])));

    // 阴影对象的PSO
    D3D12_DEPTH_STENCIL_DESC shadowDSDesc;
    shadowDSDesc.DepthEnable = true;
    shadowDSDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    shadowDSDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
    shadowDSDesc.StencilEnable = true;
    shadowDSDesc.StencilReadMask = 0xff;
    shadowDSDesc.StencilWriteMask = 0xff;

    shadowDSDesc.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSDesc.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSDesc.FrontFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    shadowDSDesc.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    shadowDSDesc.BackFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSDesc.BackFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    shadowDSDesc.BackFace.StencilPassOp = D3D12_STENCIL_OP_INCR;
    shadowDSDesc.BackFace.StencilFunc = D3D12_COMPARISON_FUNC_EQUAL;

    auto shadowPSODesc = transparentPSODesc;
    shadowPSODesc.DepthStencilState = shadowDSDesc;
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&shadowPSODesc, IID_PPV_ARGS(&mPSOs["shadow"])));

    // tree sprites PSO
    auto treeSpritePSODesc = opaquePSODesc;
    treeSpritePSODesc.VS = 
    {
        reinterpret_cast<BYTE *>(mShaders["treeSpriteVS"]->GetBufferPointer()),
        mShaders["treeSpriteVS"]->GetBufferSize(),
    };

    treeSpritePSODesc.GS = 
    {
        reinterpret_cast<BYTE *>(mShaders["treeSpriteGS"]->GetBufferPointer()),
        mShaders["treeSpriteGS"]->GetBufferSize(),
    };

    treeSpritePSODesc.PS = 
    {
        reinterpret_cast<BYTE *>(mShaders["treeSpritePS"]->GetBufferPointer()),
        mShaders["treeSpritePS"]->GetBufferSize(),
    };
    treeSpritePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    treeSpritePSODesc.InputLayout = {mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size()};
    treeSpritePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&treeSpritePSODesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));

}

void LandAndWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.emplace_back(std::make_unique<FrameResource>(
            md3dDevice.Get(),
            2,
            static_cast<uint32_t>(mAllRenderItems.size()),
            static_cast<uint32_t>(mMaterials.size()),
            mWaves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    auto wavesRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wavesRenderItem->world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    XMStoreFloat4x4(&wavesRenderItem->texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    wavesRenderItem->objCBIndex = 0;
    wavesRenderItem->geo = mGeometries["waterGeo"].get();
    wavesRenderItem->mat = mMaterials["water"].get();
    wavesRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRenderItem->indexCount = wavesRenderItem->geo->DrawArgs["grid"].IndexCount;
    wavesRenderItem->startIndexLocation = wavesRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    wavesRenderItem->baseVertexLocation = wavesRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Transparent].emplace_back(wavesRenderItem.get());
    mWavesRenderItem = wavesRenderItem.get();

    auto gridRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&gridRenderItem->world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    XMStoreFloat4x4(&gridRenderItem->texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    gridRenderItem->objCBIndex = 1;
    gridRenderItem->geo = mGeometries["landGeo"].get();
    gridRenderItem->mat = mMaterials["grass"].get();
    gridRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRenderItem->indexCount = gridRenderItem->geo->DrawArgs["grid"].IndexCount;
    gridRenderItem->startIndexLocation = gridRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    gridRenderItem->baseVertexLocation = gridRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(gridRenderItem.get());

    auto boxRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRenderItem->world, XMMatrixTranslation(6.0f, -5.0f, -15.0f));
    boxRenderItem->objCBIndex = 2;
    boxRenderItem->mat = mMaterials["wirefence"].get();
    boxRenderItem->geo = mGeometries["boxGeo"].get();
    boxRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRenderItem->indexCount = boxRenderItem->geo->DrawArgs["box"].IndexCount;
    boxRenderItem->startIndexLocation = boxRenderItem->geo->DrawArgs["box"].StartIndexLocation;
    boxRenderItem->baseVertexLocation = boxRenderItem->geo->DrawArgs["box"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::AlphaTested].emplace_back(boxRenderItem.get());

    auto floorRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&floorRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    floorRenderItem->objCBIndex = 3;
    floorRenderItem->mat = mMaterials["checkertile"].get();
    floorRenderItem->geo = mGeometries["roomGeo"].get();
    floorRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floorRenderItem->indexCount = floorRenderItem->geo->DrawArgs["floor"].IndexCount;
    floorRenderItem->startIndexLocation = floorRenderItem->geo->DrawArgs["floor"].StartIndexLocation;
    floorRenderItem->baseVertexLocation = floorRenderItem->geo->DrawArgs["floor"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(floorRenderItem.get());

    auto wallsRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&wallsRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    wallsRenderItem->objCBIndex = 4;
    wallsRenderItem->mat = mMaterials["bricks"].get();
    wallsRenderItem->geo = mGeometries["roomGeo"].get();
    wallsRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallsRenderItem->indexCount = wallsRenderItem->geo->DrawArgs["wall"].IndexCount;
    wallsRenderItem->startIndexLocation = wallsRenderItem->geo->DrawArgs["wall"].StartIndexLocation;
    wallsRenderItem->baseVertexLocation = wallsRenderItem->geo->DrawArgs["wall"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(wallsRenderItem.get());

    auto skullRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&skullRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, -5.0f));
    skullRenderItem->objCBIndex = 5;
    skullRenderItem->mat = mMaterials["skullMat"].get();
    skullRenderItem->geo = mGeometries["skullGeo"].get();
    skullRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRenderItem->indexCount = skullRenderItem->geo->DrawArgs["skull"].IndexCount;
    skullRenderItem->startIndexLocation = skullRenderItem->geo->DrawArgs["skull"].StartIndexLocation;
    skullRenderItem->baseVertexLocation = skullRenderItem->geo->DrawArgs["skull"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(skullRenderItem.get());
    mSkullRenderItem = skullRenderItem.get();

    auto reflectedSkullRenderItem = std::make_unique<RenderItem>();
    *reflectedSkullRenderItem = *skullRenderItem;
    reflectedSkullRenderItem->objCBIndex = 6;
    mRenderItemLayer[(int) RenderLayer::Reflected].emplace_back(reflectedSkullRenderItem.get());
    mReflectedSkullRenderItem = reflectedSkullRenderItem.get();

    auto shadowSkullRenderItem = std::make_unique<RenderItem>();
    *shadowSkullRenderItem = *skullRenderItem;
    shadowSkullRenderItem->objCBIndex = 7;
    shadowSkullRenderItem->mat = mMaterials["shadowMat"].get();
    mRenderItemLayer[(int) RenderLayer::Shadow].emplace_back(shadowSkullRenderItem.get());
    mShadowedSkullRenderItem = shadowSkullRenderItem.get();

    auto mirrorRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&mirrorRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    mirrorRenderItem->objCBIndex = 8;
    mirrorRenderItem->mat = mMaterials["icemirror"].get();
    mirrorRenderItem->geo = mGeometries["roomGeo"].get();
    mirrorRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mirrorRenderItem->indexCount = mirrorRenderItem->geo->DrawArgs["mirror"].IndexCount;
    mirrorRenderItem->startIndexLocation
        = mirrorRenderItem->geo->DrawArgs["mirror"].StartIndexLocation;
    mirrorRenderItem->baseVertexLocation
        = mirrorRenderItem->geo->DrawArgs["mirror"].BaseVertexLocation;
    mRenderItemLayer[(int) RenderLayer::Mirrors].emplace_back(mirrorRenderItem.get());
    mRenderItemLayer[(int) RenderLayer::Transparent].emplace_back(mirrorRenderItem.get());

    auto treeSpritesRenderItem = std::make_unique<RenderItem>();
    treeSpritesRenderItem->world = MathHelper::Identity4x4();
    treeSpritesRenderItem->objCBIndex = 9;
    treeSpritesRenderItem->mat = mMaterials["treeSprites"].get();
    treeSpritesRenderItem->geo = mGeometries["treeSpritesGeo"].get();
    treeSpritesRenderItem->primitiveType = D3D10_PRIMITIVE_TOPOLOGY_POINTLIST;
    treeSpritesRenderItem->indexCount = treeSpritesRenderItem->geo->DrawArgs["points"].IndexCount;
    treeSpritesRenderItem->baseVertexLocation
        = treeSpritesRenderItem->geo->DrawArgs["points"].BaseVertexLocation;
    treeSpritesRenderItem->startIndexLocation
        = treeSpritesRenderItem->geo->DrawArgs["points"].StartIndexLocation;
    mRenderItemLayer[(int) RenderLayer::AlphaTestedTreeSprites].emplace_back(
        treeSpritesRenderItem.get());

    mAllRenderItems.emplace_back(std::move(wavesRenderItem));
    mAllRenderItems.emplace_back(std::move(gridRenderItem));
    mAllRenderItems.emplace_back(std::move(boxRenderItem));
    mAllRenderItems.emplace_back(std::move(wallsRenderItem));
    mAllRenderItems.emplace_back(std::move(floorRenderItem));
    mAllRenderItems.emplace_back(std::move(skullRenderItem));
    mAllRenderItems.emplace_back(std::move(reflectedSkullRenderItem));
    mAllRenderItems.emplace_back(std::move(shadowSkullRenderItem));
    mAllRenderItems.emplace_back(std::move(mirrorRenderItem));
    mAllRenderItems.emplace_back(std::move(treeSpritesRenderItem));
}

void LandAndWavesApp::BuildMaterial()
{
    auto grass = std::make_unique<Material>();
    grass->Name = "grass";
    grass->MatCBIndex = 0;
    grass->DiffuseSrvHeapIndex = 0;
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grass->Roughness = 0.125f;

    auto water = std::make_unique<Material>();
    water->Name = "water";
    water->MatCBIndex = 1;
    water->DiffuseSrvHeapIndex = 1;
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
    water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    water->Roughness = 0.0f;

    auto wirefence = std::make_unique<Material>();
    wirefence->Name = "wirefence";
    wirefence->MatCBIndex = 2;
    wirefence->DiffuseSrvHeapIndex = 2;
    wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    wirefence->Roughness = 0.25f;

    auto bricks = std::make_unique<Material>();
    bricks->Name = "bricks";
    bricks->MatCBIndex = 3;
    bricks->DiffuseSrvHeapIndex = 3;
    bricks->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricks->Roughness = 0.25f;

    auto checkertile = std::make_unique<Material>();
    checkertile->Name = "checkertile";
    checkertile->MatCBIndex = 4;
    checkertile->DiffuseSrvHeapIndex = 4;
    checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkertile->Roughness = 0.3f;

    auto icemirror = std::make_unique<Material>();
    icemirror->Name = "icemirror";
    icemirror->MatCBIndex = 5;
    icemirror->DiffuseSrvHeapIndex = 5;
    icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
    icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    icemirror->Roughness = 0.5f;

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = 6;
    skullMat->DiffuseSrvHeapIndex = 6;
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;

    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = 7;
    shadowMat->DiffuseSrvHeapIndex = 6;
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
    shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
    shadowMat->Roughness = 0.0f;

    auto treeSprites = std::make_unique<Material>();
    treeSprites->Name = "treeSprites";
    treeSprites->MatCBIndex = 8;
    treeSprites->DiffuseSrvHeapIndex = 7;
    treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    treeSprites->Roughness = 0.125f;

    mMaterials[grass->Name] = std::move(grass);
    mMaterials[water->Name] = std::move(water);
    mMaterials[wirefence->Name] = std::move(wirefence);
    mMaterials[bricks->Name] = std::move(bricks);
    mMaterials[checkertile->Name] = std::move(checkertile);
    mMaterials[icemirror->Name] = std::move(icemirror);
    mMaterials[skullMat->Name] = std::move(skullMat);
    mMaterials[shadowMat->Name] = std::move(shadowMat);
    mMaterials[treeSprites->Name] = std::move(treeSprites);
}

void LandAndWavesApp::LoadTextures()
{
    std::vector<std::pair<std::string, std::wstring>> texInfo = {
        {"grassTex", L"/Assets/Textures/grass.dds"},
        {"waterTex", L"/Assets/Textures/water1.dds"},
        {"fenceTex", L"/Assets/Textures/WireFence.dds"},
        {"bricksTex", L"/Assets/Textures/bricks3.dds"},
        {"checkboardTex", L"/Assets/Textures/checkboard.dds"},
        {"iceTex", L"/Assets/Textures/ice.dds"},
        {"white1x1Tex", L"/Assets/Textures/white1x1.dds"},
        {"treeArrayTex", L"/Assets/Textures/treeArray2.dds"},
    };

    for (const auto &it : texInfo) {
        auto tex = std::make_unique<Texture>();
        tex->Name = it.first;
        tex->Filename = GetAppPath() + it.second;
        ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(
            md3dDevice.Get(),
            mCommandList.Get(),
            tex->Filename.c_str(),
            tex->Resource,
            tex->UploadHeap));

        mTextures[it.first] = std::move(tex);
    }
}

void LandAndWavesApp::DrawRenderItems(
    ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &renderItems)
{
    auto objCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    auto matCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(MaterialConstants));

    auto resourceObj = mCurrFrameResource->objectCB->Resource();
    auto resourceMat = mCurrFrameResource->materialCB->Resource();

    for (const auto &item : renderItems) {
        cmdList->IASetIndexBuffer(&item->geo->IndexBufferView());
        cmdList->IASetVertexBuffers(0, 1, &item->geo->VertexBufferView());
        cmdList->IASetPrimitiveTopology(item->primitiveType);

        auto objCBAddress = resourceObj->GetGPUVirtualAddress();
        objCBAddress += item->objCBIndex * objCbByteSize;
        auto materialCBAddress = resourceMat->GetGPUVirtualAddress();
        materialCBAddress += item->mat->MatCBIndex * matCbByteSize;
        CD3DX12_GPU_DESCRIPTOR_HANDLE texAddress(
            mSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
        texAddress.Offset(item->mat->DiffuseSrvHeapIndex, mCbvSrvUavDescriptorSize);

        cmdList->SetGraphicsRootDescriptorTable(0, texAddress);
        cmdList->SetGraphicsRootConstantBufferView(1, objCBAddress);
        cmdList->SetGraphicsRootConstantBufferView(3, materialCBAddress);

        cmdList->DrawIndexedInstanced(
            item->indexCount, 1, item->startIndexLocation, item->baseVertexLocation, 0);
    }
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> LandAndWavesApp::GetStaticSamplers()
{
    // Applications usually only need a handful of samplers.  So just define them all up front
    // and keep them available as part of the root signature.

    const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
        0,                                // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT,   // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
        1,                                 // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_POINT,    // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
        2,                                // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,  // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
        3,                                 // shaderRegister
        D3D12_FILTER_MIN_MAG_MIP_LINEAR,   // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP,  // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP); // addressW

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
        4,                               // shaderRegister
        D3D12_FILTER_ANISOTROPIC,        // filter
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_WRAP, // addressW
        0.0f,                            // mipLODBias
        8);                              // maxAnisotropy

    const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
        5,                                // shaderRegister
        D3D12_FILTER_ANISOTROPIC,         // filter
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressU
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressV
        D3D12_TEXTURE_ADDRESS_MODE_CLAMP, // addressW
        0.0f,                             // mipLODBias
        8);                               // maxAnisotropy

    return {pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp};
}

void LandAndWavesApp::UpdateWaves(const GameTimer &gt)
{
    static float t_base = 0.0f;
    if ((mTimer.TotalTime() - t_base) >= 0.25f) {
        t_base += 0.25f;

        int i = MathHelper::Rand(4, mWaves->RowCount() - 5);
        int j = MathHelper::Rand(4, mWaves->ColumnCount() - 5);

        float r = MathHelper::RandF(0.2f, 0.5f);

        mWaves->Disturb(i, j, r);
    }

    // Update the wave simulation.
    mWaves->Update(gt.DeltaTime());

    // Update the wave vertex buffer with the new solution.
    auto currWavesVB = mCurrFrameResource->wavesVB.get();
    for (int i = 0; i < mWaves->VertexCount(); ++i) {
        Vertex v;

        v.pos = mWaves->Position(i);
        v.normal = mWaves->Normal(i);

        v.texCoord.x = 0.5f + v.pos.x / mWaves->Width();
        v.texCoord.y = 0.5f - v.pos.z / mWaves->Depth();

        currWavesVB->CopyData(i, v);
    }

    mWavesRenderItem->geo->VertexBufferGPU = currWavesVB->Resource();
}

float LandAndWavesApp::GetHillsHeight(float x, float z) const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 LandAndWavesApp::GetHillsNormal(float x, float z) const
{
    XMFLOAT3
    n(-0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
      1.0f,
      -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}
