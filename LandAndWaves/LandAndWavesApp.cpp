#include <cstddef>
#include <iostream>

#include "../Common/DDSTextureLoader.h"
#include "../Common/GeometryGenerator.h"
#include "LandAndWavesApp.h"

const int gNumFrameResources = 3;

const std::string gSkyBoxTexName("skyBoxTex");

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
    BuildShapeGeometry();
    BuildLandGeometry();
    BuildWaveGeometryBuffers();
    BuildBoxGeometry();
    BuildRoomGeometry();
    BuildSkullGeometry();
    //BuildTreeSpritesGeometry();

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

    mCamera.SetLens(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);

    BoundingFrustum::CreateFromMatrix(mCamFrustum, mCamera.GetProj());
}

void LandAndWavesApp::Update(const GameTimer &gt)
{
    OnKeyboardInput(gt);
    //UpdateCamera(gt);

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
    UpdateInstanceBuffer(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);
    UpdateMaterialBuffer(gt);
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

    auto curPasssResource = mCurrFrameResource->passCB->Resource();
    mCommandList->SetGraphicsRootShaderResourceView(
        1, mCurrFrameResource->materialBuffer->Resource()->GetGPUVirtualAddress());
    mCommandList->SetGraphicsRootConstantBufferView(2, curPasssResource->GetGPUVirtualAddress());
    
    CD3DX12_GPU_DESCRIPTOR_HANDLE srvHandle(mSRVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    mCommandList->SetGraphicsRootDescriptorTable(3, srvHandle);
    mCommandList->SetGraphicsRootDescriptorTable(4, srvHandle.Offset(1, mCbvSrvUavDescriptorSize));

    // 先绘制不透明物体
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Opaque)]);

    mCommandList->SetPipelineState(mPSOs["alphaTested"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::AlphaTested)]);

    // 绘制tree sprite
    /*mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::AlphaTestedTreeSprites)]);*/

    // 将模板缓冲区中可见的镜面像素标记为1
    mCommandList->OMSetStencilRef(1);
    mCommandList->SetPipelineState(mPSOs["markStencilMirrors"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Mirrors)]);

    // 只绘制镜子范围内的镜像（即仅绘制模板缓冲区中标记为1的像素）
    // 注意我们必须使用两个单独的渲染过程常量缓冲区，一个存储物体镜像，另一个保存光照镜像
    UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
    mCommandList->SetGraphicsRootConstantBufferView(
        2, curPasssResource->GetGPUVirtualAddress() + 1 * passCBByteSize);
    mCommandList->SetPipelineState(mPSOs["drawStencilReflections"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Reflected)]);

    mCommandList->SetGraphicsRootConstantBufferView(2, curPasssResource->GetGPUVirtualAddress());
    mCommandList->OMSetStencilRef(0);

    // 绘制透明的镜面，使镜像可以与之融合
    mCommandList->SetPipelineState(mPSOs["transparent"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Transparent)]);

    mCommandList->SetPipelineState(mPSOs["shadow"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Shadow)]);

    mCommandList->SetPipelineState(mPSOs["sky"].Get());
    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[(int) RenderLayer::Sky]);

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

        mCamera.Pitch(dy);
        mCamera.RotateY(dx);
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

    if (GetAsyncKeyState('2') & 0x8000)
        mFrustumCullingEnabled = true;

    if (GetAsyncKeyState('3') & 0x8000)
        mFrustumCullingEnabled = false;

    const float dt = gt.DeltaTime();
    if (GetAsyncKeyState(VK_LEFT) & 0x8000 || GetAsyncKeyState('A') & 0x8000) {
        mCamera.Strafe(-10.0f * dt);
    }

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000 || GetAsyncKeyState('D') & 0x8000) {
        mCamera.Strafe(10.0f * dt);
    }

    if (GetAsyncKeyState(VK_UP) & 0x8000 || GetAsyncKeyState('W') & 0x8000) {
        mCamera.Walk(10.0f * dt);
    }

    if (GetAsyncKeyState(VK_DOWN) & 0x8000 || GetAsyncKeyState('S') & 0x8000) {
        mCamera.Walk(-10.0f * dt);
    }

    mCamera.UpdateViewMatrix();

    ChangeSkullTranslation(gt);
}

void LandAndWavesApp::ChangeSkullTranslation(const GameTimer &gt)
{
    const float dt = gt.DeltaTime();
    if (GetAsyncKeyState(VK_LEFT) & 0x8000) {
        mSkullTranslation.x -= 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_RIGHT) & 0x8000) {
        mSkullTranslation.x += 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_UP) & 0x8000) {
        mSkullTranslation.y += 1.0f * dt;
    }

    if (GetAsyncKeyState(VK_DOWN) & 0x8000) {
        mSkullTranslation.y -= 1.0f * dt;
    }

    // Don't let user move below ground plane.
    mSkullTranslation.y = MathHelper::Max(mSkullTranslation.y, 0.0f);

    // Update the new world matrix.
    XMMATRIX skullRotate = XMMatrixRotationY(0.5f * MathHelper::Pi);
    XMMATRIX skullScale = XMMatrixScaling(0.45f, 0.45f, 0.45f);
    XMMATRIX skullOffset
        = XMMatrixTranslation(mSkullTranslation.x, mSkullTranslation.y, mSkullTranslation.z);
    XMMATRIX skullWorld = skullRotate * skullScale * skullOffset;
    XMStoreFloat4x4(&mSkullRenderItem->instances[0].world, skullWorld);

    // Update reflection world matrix.
    XMVECTOR mirrorPlane = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f); // xy plane
    XMMATRIX R = XMMatrixReflect(mirrorPlane);
    XMStoreFloat4x4(&mReflectedSkullRenderItem->instances[0].world, skullWorld * R);

    // Update shadow world matrix.
    XMVECTOR shadowPlane = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f); // xz plane
    XMVECTOR toMainLight = -XMLoadFloat3(&mMainPassCB.lights[0].Direction);
    XMMATRIX S = XMMatrixShadow(shadowPlane, toMainLight);
    XMMATRIX shadowOffsetY = XMMatrixTranslation(0.0f, 0.001f, 0.0f);
    XMStoreFloat4x4(&mShadowedSkullRenderItem->instances[0].world, skullWorld * S * shadowOffsetY);

    mSkullRenderItem->numFramesDirty = gNumFrameResources;
    mReflectedSkullRenderItem->numFramesDirty = gNumFrameResources;
    mShadowedSkullRenderItem->numFramesDirty = gNumFrameResources;
}

void LandAndWavesApp::AnimateMaterials(const GameTimer &gt)
{
    // Scroll the water material texture coordinates.
    auto waterMat = mMaterials["waterMat"].get();

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

//void LandAndWavesApp::UpdateCamera(const GameTimer &gt)
//{
//    // 将球坐标转换为笛卡尔坐标
//    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
//    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
//    mEyePos.y = mRadius * cosf(mPhi);
//
//    // 构建观察矩阵
//    auto pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
//    auto target = XMVectorZero();
//    auto up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
//
//    auto view = XMMatrixLookAtLH(pos, target, up);
//    XMStoreFloat4x4(&mView, view);
//}

void LandAndWavesApp::UpdateInstanceBuffer(const GameTimer &gt)
{
    auto view = mCamera.GetView();
    auto invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);

    auto currInstanceBuffer = mCurrFrameResource->instanceBuffer.get();
    auto allVisibleCount = 0;
    for (auto &item : mAllRenderItems) {
        auto currItemVisibleInstanceCount = 0;
        item->objCBIndex = allVisibleCount;
        for (const auto &instance : item->instances) {
            InstanceData objConstans;
            auto world = XMLoadFloat4x4(&instance.world);
            auto texTransform = XMLoadFloat4x4(&instance.texTransform);

            auto invWorld = XMMatrixInverse(&XMMatrixDeterminant(world), world);

            // 由观察空间到物体局部的变换矩阵
            auto viewToLocal = XMMatrixMultiply(invView, invWorld);

            // 将摄像机视锥体由观察空间变换到物体的局部空间
            BoundingFrustum localSpaceFrustum;
            mCamFrustum.Transform(localSpaceFrustum, viewToLocal);
            if (!mFrustumCullingEnabled
                || localSpaceFrustum.Contains(item->boundingBox) != DirectX::DISJOINT) {
                XMStoreFloat4x4(&objConstans.world, XMMatrixTranspose(world));
                XMStoreFloat4x4(&objConstans.texTransform, XMMatrixTranspose(texTransform));
                objConstans.materialIndex = instance.materialIndex;

                currInstanceBuffer
                    ->CopyData(item->objCBIndex + currItemVisibleInstanceCount++, objConstans);
            }
        }

        item->instanceCount = currItemVisibleInstanceCount;
        allVisibleCount += currItemVisibleInstanceCount;
    }

    std::wostringstream outs;
    outs.precision(6);
    outs << L"All instance count: " << mAllInstanceDataCount << L"; objects visible count: "
         << allVisibleCount;
    mMainWndCaption = outs.str();
    std::wcout << outs.str() << std::endl;
}

void LandAndWavesApp::UpdateMainPassCB(const GameTimer &gt)
{
    auto view = mCamera.GetView();
    auto proj = mCamera.GetProj();
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

    mMainPassCB.eyePos = mCamera.GetPosition3f();
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

void LandAndWavesApp::UpdateMaterialBuffer(const GameTimer &gt)
{
    auto currMaterialBuffer = mCurrFrameResource->materialBuffer.get();
    for (const auto &it : mMaterials) {
        auto material = it.second.get();
        if (material->NumFramesDirty > 0) {
            MaterialData mc;
            mc.diffuseAlbedo = material->DiffuseAlbedo;
            mc.fresnelR0 = material->FresnelR0;
            mc.roughness = material->Roughness;
            mc.diffuseMapIndex = material->DiffuseSrvHeapIndex;
            mc.normalMapIndex = material->NormalSrvHeapIndex;

            XMMATRIX matTransform = XMLoadFloat4x4(&material->MatTransform);
            XMStoreFloat4x4(&mc.matTransform, XMMatrixTranspose(matTransform));

            currMaterialBuffer->CopyData(material->MatCBIndex, mc);

            --material->NumFramesDirty;
        }
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
    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);
    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i) {
        auto &p = grid.Vertices[i].Position;
        vertices[i].pos = p;
        vertices[i].pos.y = GetHillsHeight(p.x, p.z);
        vertices[i].normal = GetHillsNormal(p.x, p.z);
        vertices[i].texCoord = grid.Vertices[i].TexC;

        auto pos = XMLoadFloat3(&vertices[i].pos);
        vMin = XMVectorMin(vMin, pos);
        vMax = XMVectorMax(vMax, pos);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

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
    subMesh.Bounds = bounds;

    geo->DrawArgs["grid"] = subMesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildWaveGeometryBuffers()
{
    mWaves = std::make_unique<Waves>(128, 128, 1.0f, 0.03f, 4.0f, 0.2f);

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);
    for (uint32_t i = 0; i < mWaves->VertexCount(); ++i) {
        auto pos = mWaves->Position(i);
        auto P = XMLoadFloat3(&pos);
        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }
    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

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
    submesh.Bounds = bounds;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildBoxGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(8.0f, 8.0f, 8.0f, 3);

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(box.Vertices.size());
    for (size_t i = 0; i < box.Vertices.size(); ++i) {
        auto &p = box.Vertices[i].Position;
        vertices[i].pos = p;
        vertices[i].normal = box.Vertices[i].Normal;
        vertices[i].texCoord = box.Vertices[i].TexC;

        auto pos = XMLoadFloat3(&vertices[i].pos);
        vMin = XMVectorMin(vMin, pos);
        vMax = XMVectorMax(vMax, pos);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

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
    submesh.Bounds = bounds;

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

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);
    for (uint32_t i = 0; i < vertices.size(); ++i) {
        auto pos = vertices[i].pos;
        auto P = XMLoadFloat3(&pos);
        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }
    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

    SubmeshGeometry floorSubmesh;
    floorSubmesh.IndexCount = 6;
    floorSubmesh.StartIndexLocation = 0;
    floorSubmesh.BaseVertexLocation = 0;
    floorSubmesh.Bounds = bounds;

    SubmeshGeometry wallSubmesh;
    wallSubmesh.IndexCount = 18;
    wallSubmesh.StartIndexLocation = 6;
    wallSubmesh.BaseVertexLocation = 0;
    wallSubmesh.Bounds = bounds;

    SubmeshGeometry mirrorSubmesh;
    mirrorSubmesh.IndexCount = 6;
    mirrorSubmesh.StartIndexLocation = 24;
    mirrorSubmesh.BaseVertexLocation = 0;
    mirrorSubmesh.Bounds = bounds;

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

    XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
    XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);

    XMVECTOR vMin = XMLoadFloat3(&vMinf3);
    XMVECTOR vMax = XMLoadFloat3(&vMaxf3);

    std::vector<Vertex> vertices(vcount);
    for (UINT i = 0; i < vcount; ++i) {
        fin >> vertices[i].pos.x >> vertices[i].pos.y >> vertices[i].pos.z;
        fin >> vertices[i].normal.x >> vertices[i].normal.y >> vertices[i].normal.z;

        XMVECTOR P = XMLoadFloat3(&vertices[i].pos);

        // 将点投影到单位球面上并生成球面纹理坐标
        XMFLOAT3 spherePos;
        XMStoreFloat3(&spherePos, XMVector3Normalize(P));

        float theta = atan2f(spherePos.z, spherePos.x);

        // 把角度theta限制在[0, 2pi]区间内
        if (theta < 0.0f)
            theta += XM_2PI;

        float phi = acosf(spherePos.y);

        float u = theta / (2.0f * XM_PI);
        float v = phi / XM_PI;

        vertices[i].texCoord = {u, v};

        vMin = XMVectorMin(vMin, P);
        vMax = XMVectorMax(vMax, P);
    }

    BoundingBox bounds;
    XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
    XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));

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
    submesh.Bounds = bounds;

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

void LandAndWavesApp::BuildShapeGeometry()
{
    auto ComputeBoundingBox = [](const GeometryGenerator::MeshData &meshData) -> BoundingBox {
        XMFLOAT3 vMinf3(+MathHelper::Infinity, +MathHelper::Infinity, +MathHelper::Infinity);
        XMFLOAT3 vMaxf3(-MathHelper::Infinity, -MathHelper::Infinity, -MathHelper::Infinity);
        XMVECTOR vMin = XMLoadFloat3(&vMinf3);
        XMVECTOR vMax = XMLoadFloat3(&vMaxf3);
        for (uint32_t i = 0; i < meshData.Vertices.size(); ++i) {
            auto pos = meshData.Vertices[i].Position;
            auto P = XMLoadFloat3(&pos);
            vMin = XMVectorMin(vMin, P);
            vMax = XMVectorMax(vMax, P);
        }
        BoundingBox bounds;
        XMStoreFloat3(&bounds.Center, 0.5f * (vMin + vMax));
        XMStoreFloat3(&bounds.Extents, 0.5f * (vMax - vMin));
        return bounds;
    };

    GeometryGenerator geoGen;
    GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 3);
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
    GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
    GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

    //
    // We are concatenating all the geometry into one big vertex/index buffer.  So
    // define the regions in the buffer each submesh covers.
    //

    // Cache the vertex offsets to each object in the concatenated vertex buffer.
    UINT boxVertexOffset = 0;
    UINT gridVertexOffset = (UINT) box.Vertices.size();
    UINT sphereVertexOffset = gridVertexOffset + (UINT) grid.Vertices.size();
    UINT cylinderVertexOffset = sphereVertexOffset + (UINT) sphere.Vertices.size();

    // Cache the starting index for each object in the concatenated index buffer.
    UINT boxIndexOffset = 0;
    UINT gridIndexOffset = (UINT) box.Indices32.size();
    UINT sphereIndexOffset = gridIndexOffset + (UINT) grid.Indices32.size();
    UINT cylinderIndexOffset = sphereIndexOffset + (UINT) sphere.Indices32.size();

    SubmeshGeometry boxSubmesh;
    boxSubmesh.IndexCount = (UINT) box.Indices32.size();
    boxSubmesh.StartIndexLocation = boxIndexOffset;
    boxSubmesh.BaseVertexLocation = boxVertexOffset;
    boxSubmesh.Bounds = ComputeBoundingBox(box);

    SubmeshGeometry gridSubmesh;
    gridSubmesh.IndexCount = (UINT) grid.Indices32.size();
    gridSubmesh.StartIndexLocation = gridIndexOffset;
    gridSubmesh.BaseVertexLocation = gridVertexOffset;
    gridSubmesh.Bounds = ComputeBoundingBox(grid);

    SubmeshGeometry sphereSubmesh;
    sphereSubmesh.IndexCount = (UINT) sphere.Indices32.size();
    sphereSubmesh.StartIndexLocation = sphereIndexOffset;
    sphereSubmesh.BaseVertexLocation = sphereVertexOffset;
    sphereSubmesh.Bounds = ComputeBoundingBox(sphere);

    SubmeshGeometry cylinderSubmesh;
    cylinderSubmesh.IndexCount = (UINT) cylinder.Indices32.size();
    cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
    cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;
    cylinderSubmesh.Bounds = ComputeBoundingBox(cylinder);

    //
    // Extract the vertex elements we are interested in and pack the
    // vertices of all the meshes into one vertex buffer.
    //

    auto totalVertexCount = box.Vertices.size() + grid.Vertices.size() + sphere.Vertices.size()
                            + cylinder.Vertices.size();

    std::vector<Vertex> vertices(totalVertexCount);

    UINT k = 0;
    for (size_t i = 0; i < box.Vertices.size(); ++i, ++k) {
        vertices[k].pos = box.Vertices[i].Position;
        vertices[k].normal = box.Vertices[i].Normal;
        vertices[k].texCoord = box.Vertices[i].TexC;
        vertices[k].tangent = box.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k) {
        vertices[k].pos = grid.Vertices[i].Position;
        vertices[k].normal = grid.Vertices[i].Normal;
        vertices[k].texCoord = grid.Vertices[i].TexC;
        vertices[k].tangent = grid.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k) {
        vertices[k].pos = sphere.Vertices[i].Position;
        vertices[k].normal = sphere.Vertices[i].Normal;
        vertices[k].texCoord = sphere.Vertices[i].TexC;
        vertices[k].tangent = sphere.Vertices[i].TangentU;
    }

    for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k) {
        vertices[k].pos = cylinder.Vertices[i].Position;
        vertices[k].normal = cylinder.Vertices[i].Normal;
        vertices[k].texCoord = cylinder.Vertices[i].TexC;
        vertices[k].tangent = cylinder.Vertices[i].TangentU;
    }

    std::vector<std::uint16_t> indices;
    indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
    indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
    indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
    indices.insert(
        indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

    const UINT vbByteSize = (UINT) vertices.size() * sizeof(Vertex);
    const UINT ibByteSize = (UINT) indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "shapeGeo";

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

    geo->DrawArgs["box"] = boxSubmesh;
    geo->DrawArgs["grid"] = gridSubmesh;
    geo->DrawArgs["sphere"] = sphereSubmesh;
    geo->DrawArgs["cylinder"] = cylinderSubmesh;

    mGeometries[geo->Name] = std::move(geo);
}

void LandAndWavesApp::BuildDescriptorHeaps()
{
    // 创建描述符堆
    D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
    srvHeapDesc.NumDescriptors = mTextures.size();
    srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srvHeapDesc.NodeMask = 0;
    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc, IID_PPV_ARGS(&mSRVDescriptorHeap)));

    // 使用SRV填充描述符堆
    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(mSRVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MostDetailedMip = 0;

    for (const auto it : mSRVHeapTexture) {
        if (it->Name == gSkyBoxTexName) {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;     
        } else {
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D; 
        }

        auto resource = it->Resource.Get();
        srvDesc.Format = resource->GetDesc().Format;
        srvDesc.Texture2D.MipLevels = resource->GetDesc().MipLevels;
        md3dDevice->CreateShaderResourceView(resource, &srvDesc, handle);
        handle.Offset(1, mCbvSrvUavDescriptorSize);
    }

    // 纹理数组
    /*auto treeArrayTex = mTextures["treeArray2Tex"]->Resource;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Format = treeArrayTex->GetDesc().Format;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.MipLevels = -1;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
    md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, handle);*/
}

void LandAndWavesApp::BuildRootSignature()
{
    CD3DX12_DESCRIPTOR_RANGE texTables[2];
    texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
    texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, mTextures.size() - 1, 1);

    CD3DX12_ROOT_PARAMETER slotRootParameter[5];
    // 按变更频率由高到低排列
    slotRootParameter[0].InitAsShaderResourceView(0, 1); // objectsBufferSRV
    slotRootParameter[1].InitAsShaderResourceView(1, 1); // materialsBufferSRV
    slotRootParameter[2].InitAsConstantBufferView(0);    // passCBV
    slotRootParameter[3]
        .InitAsDescriptorTable(1, &texTables[0], D3D12_SHADER_VISIBILITY_PIXEL); // textureSRV
    slotRootParameter[4]
        .InitAsDescriptorTable(1, &texTables[1], D3D12_SHADER_VISIBILITY_PIXEL); // textureSRV

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
        //{"FOG", "1"},
        {nullptr, nullptr},
    };

    const D3D_SHADER_MACRO alphaTestDefines[] = {
        //{"FOG", "1"},
        {"ALPHA_TEST", "1"},
        {nullptr, nullptr},
    };

    mShaders["standardVS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["opaquePS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", defines, "PS", "ps_5_1");
    mShaders["alphaTestedPS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/Default.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mShaders["treeSpriteVS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["treeSpriteGS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
    mShaders["treeSpritePS"] = d3dUtil::CompileShader(
        GetAppPath() + L"/Assets/Shaders/TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

    mShaders["skyVS"]
        = d3dUtil::CompileShader(GetAppPath() + L"/Assets/Shaders/Sky.hlsl", nullptr, "VS", "vs_5_1");
    mShaders["skyPS"]
        = d3dUtil::CompileShader(GetAppPath() + L"/Assets/Shaders/Sky.hlsl", nullptr, "PS", "ps_5_1");

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
            0},
           {"TANGENT",
            0,
            DXGI_FORMAT_R32G32B32_FLOAT,
            0,
            offsetof(Vertex, tangent),
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

    opaquePSODesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
    opaquePSODesc.DSVFormat = mDepthStencilFormat;

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

    // sky PSO
    auto skyPSODesc = opaquePSODesc;
    skyPSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE; // 摄像机位于天空球内，所以要关闭剔除功能
    skyPSODesc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    skyPSODesc.VS
        = {reinterpret_cast<BYTE *>(mShaders["skyVS"]->GetBufferPointer()),
           mShaders["skyVS"]->GetBufferSize()};
    skyPSODesc.PS
        = {reinterpret_cast<BYTE *>(mShaders["skyPS"]->GetBufferPointer()),
           mShaders["skyPS"]->GetBufferSize()};
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&skyPSODesc, IID_PPV_ARGS(&mPSOs["sky"])));

    // tree sprites PSO
    /*auto treeSpritePSODesc = opaquePSODesc;
    treeSpritePSODesc.VS = {
        reinterpret_cast<BYTE *>(mShaders["treeSpriteVS"]->GetBufferPointer()),
        mShaders["treeSpriteVS"]->GetBufferSize(),
    };

    treeSpritePSODesc.GS = {
        reinterpret_cast<BYTE *>(mShaders["treeSpriteGS"]->GetBufferPointer()),
        mShaders["treeSpriteGS"]->GetBufferSize(),
    };

    treeSpritePSODesc.PS = {
        reinterpret_cast<BYTE *>(mShaders["treeSpritePS"]->GetBufferPointer()),
        mShaders["treeSpritePS"]->GetBufferSize(),
    };
    treeSpritePSODesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    treeSpritePSODesc.InputLayout
        = {mTreeSpriteInputLayout.data(), (UINT) mTreeSpriteInputLayout.size()};
    treeSpritePSODesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    ThrowIfFailed(
        md3dDevice->CreateGraphicsPipelineState(&treeSpritePSODesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));*/
}

void LandAndWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i) {
        mFrameResources.emplace_back(std::make_unique<FrameResource>(
            md3dDevice.Get(),
            2,
            //static_cast<uint32_t>(mAllRenderItems.size()),
            mAllInstanceDataCount,
            static_cast<uint32_t>(mMaterials.size()),
            mWaves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    auto skyRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&skyRenderItem->world, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    skyRenderItem->texTransform = MathHelper::Identity4x4();
    skyRenderItem->objCBIndex = mAllInstanceDataCount++;
    skyRenderItem->mat = mMaterials["skyMat"].get();
    skyRenderItem->geo = mGeometries["shapeGeo"].get();
    skyRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skyRenderItem->indexCount = skyRenderItem->geo->DrawArgs["sphere"].IndexCount;
    skyRenderItem->startIndexLocation = skyRenderItem->geo->DrawArgs["sphere"].StartIndexLocation;
    skyRenderItem->baseVertexLocation = skyRenderItem->geo->DrawArgs["sphere"].BaseVertexLocation;
    skyRenderItem->boundingBox = skyRenderItem->geo->DrawArgs["sphere"].Bounds;
    skyRenderItem->instances.resize(1);
    skyRenderItem->instances[0].materialIndex = skyRenderItem->mat->MatCBIndex;
    XMStoreFloat4x4(&skyRenderItem->instances[0].world, XMMatrixScaling(5000.0f, 5000.0f, 5000.0f));
    mRenderItemLayer[(int) RenderLayer::Sky].push_back(skyRenderItem.get());
    mAllRenderItems.push_back(std::move(skyRenderItem));

    auto wavesRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&wavesRenderItem->world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    //XMStoreFloat4x4(&wavesRenderItem->texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    wavesRenderItem->objCBIndex = mAllInstanceDataCount++;
    wavesRenderItem->geo = mGeometries["waterGeo"].get();
    wavesRenderItem->mat = mMaterials["waterMat"].get();
    wavesRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRenderItem->indexCount = wavesRenderItem->geo->DrawArgs["grid"].IndexCount;
    wavesRenderItem->startIndexLocation = wavesRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    wavesRenderItem->baseVertexLocation = wavesRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;
    wavesRenderItem->boundingBox = wavesRenderItem->geo->DrawArgs["grid"].Bounds;
    wavesRenderItem->instances.resize(1);
    wavesRenderItem->instances[0].materialIndex = wavesRenderItem->mat->MatCBIndex;
    XMStoreFloat4x4(&wavesRenderItem->instances[0].world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    XMStoreFloat4x4(&wavesRenderItem->instances[0].texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    mRenderItemLayer[(int) RenderLayer::Transparent].emplace_back(wavesRenderItem.get());
    mWavesRenderItem = wavesRenderItem.get();
    mAllRenderItems.emplace_back(std::move(wavesRenderItem));

    auto gridRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&gridRenderItem->world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    //XMStoreFloat4x4(&gridRenderItem->texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    gridRenderItem->objCBIndex = mAllInstanceDataCount++;
    gridRenderItem->geo = mGeometries["landGeo"].get();
    gridRenderItem->mat = mMaterials["grassMat"].get();
    gridRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRenderItem->indexCount = gridRenderItem->geo->DrawArgs["grid"].IndexCount;
    gridRenderItem->startIndexLocation = gridRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    gridRenderItem->baseVertexLocation = gridRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;
    gridRenderItem->boundingBox = gridRenderItem->geo->DrawArgs["grid"].Bounds;
    gridRenderItem->instances.resize(1);
    gridRenderItem->instances[0].materialIndex = gridRenderItem->mat->MatCBIndex;
    XMStoreFloat4x4(&gridRenderItem->instances[0].world, XMMatrixTranslation(0.0f, -5.0f, 0.0f));
    XMStoreFloat4x4(&gridRenderItem->instances[0].texTransform, XMMatrixScaling(5.0f, 5.0f, 1.0f));
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(gridRenderItem.get());
    mAllRenderItems.emplace_back(std::move(gridRenderItem));

    auto boxRenderItem = std::make_unique<RenderItem>();
    XMStoreFloat4x4(&boxRenderItem->world, XMMatrixTranslation(6.0f, -5.0f, -15.0f));
    boxRenderItem->objCBIndex = mAllInstanceDataCount++;
    boxRenderItem->mat = mMaterials["wirefenceMat"].get();
    boxRenderItem->geo = mGeometries["boxGeo"].get();
    boxRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    boxRenderItem->indexCount = boxRenderItem->geo->DrawArgs["box"].IndexCount;
    boxRenderItem->startIndexLocation = boxRenderItem->geo->DrawArgs["box"].StartIndexLocation;
    boxRenderItem->baseVertexLocation = boxRenderItem->geo->DrawArgs["box"].BaseVertexLocation;
    boxRenderItem->boundingBox = boxRenderItem->geo->DrawArgs["box"].Bounds;
    boxRenderItem->instances.resize(1);
    boxRenderItem->instances[0].materialIndex = boxRenderItem->mat->MatCBIndex;
    XMStoreFloat4x4(&boxRenderItem->instances[0].world, XMMatrixTranslation(6.0f, -5.0f, -15.0f));
    mRenderItemLayer[(int) RenderLayer::AlphaTested].emplace_back(boxRenderItem.get());
    mAllRenderItems.emplace_back(std::move(boxRenderItem));

    auto floorRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&floorRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    floorRenderItem->objCBIndex = mAllInstanceDataCount++;
    floorRenderItem->geo = mGeometries["roomGeo"].get();
    floorRenderItem->mat = mMaterials["checkertileMat"].get();
    floorRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    floorRenderItem->indexCount = floorRenderItem->geo->DrawArgs["floor"].IndexCount;
    floorRenderItem->startIndexLocation = floorRenderItem->geo->DrawArgs["floor"].StartIndexLocation;
    floorRenderItem->baseVertexLocation = floorRenderItem->geo->DrawArgs["floor"].BaseVertexLocation;
    floorRenderItem->boundingBox = floorRenderItem->geo->DrawArgs["floor"].Bounds;
    floorRenderItem->instances.resize(1);
    floorRenderItem->instances[0].materialIndex = floorRenderItem->mat->MatCBIndex;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(floorRenderItem.get());
    mAllRenderItems.emplace_back(std::move(floorRenderItem));

    auto wallsRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&wallsRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    wallsRenderItem->objCBIndex = mAllInstanceDataCount++;
    wallsRenderItem->geo = mGeometries["roomGeo"].get();
    wallsRenderItem->mat = mMaterials["bricks3Mat"].get();
    wallsRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wallsRenderItem->indexCount = wallsRenderItem->geo->DrawArgs["wall"].IndexCount;
    wallsRenderItem->startIndexLocation = wallsRenderItem->geo->DrawArgs["wall"].StartIndexLocation;
    wallsRenderItem->baseVertexLocation = wallsRenderItem->geo->DrawArgs["wall"].BaseVertexLocation;
    wallsRenderItem->boundingBox = wallsRenderItem->geo->DrawArgs["wall"].Bounds;
    wallsRenderItem->instances.resize(1);
    wallsRenderItem->instances[0].materialIndex = wallsRenderItem->mat->MatCBIndex;
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(wallsRenderItem.get());
    mAllRenderItems.emplace_back(std::move(wallsRenderItem));

    auto skullRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&skullRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, -5.0f));
    skullRenderItem->objCBIndex = mAllInstanceDataCount++;
    skullRenderItem->geo = mGeometries["skullGeo"].get();
    skullRenderItem->mat = mMaterials["skullMat"].get();
    skullRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    skullRenderItem->indexCount = skullRenderItem->geo->DrawArgs["skull"].IndexCount;
    skullRenderItem->startIndexLocation = skullRenderItem->geo->DrawArgs["skull"].StartIndexLocation;
    skullRenderItem->baseVertexLocation = skullRenderItem->geo->DrawArgs["skull"].BaseVertexLocation;
    skullRenderItem->boundingBox = skullRenderItem->geo->DrawArgs["skull"].Bounds;
    skullRenderItem->instances.resize(1);
    skullRenderItem->instances[0].materialIndex = skullRenderItem->mat->MatCBIndex;
    XMStoreFloat4x4(&skullRenderItem->instances[0].world, XMMatrixTranslation(0.0f, 1.0f, -5.0f));
    mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(skullRenderItem.get());
    mSkullRenderItem = skullRenderItem.get();
    BuildInstanceDataForSkullRenderItem(skullRenderItem.get());
    
    auto reflectedSkullRenderItem = std::make_unique<RenderItem>();
    *reflectedSkullRenderItem = *skullRenderItem;
    reflectedSkullRenderItem->objCBIndex = mAllInstanceDataCount++;
    reflectedSkullRenderItem->instances.resize(1);
    reflectedSkullRenderItem->instances[0].materialIndex = reflectedSkullRenderItem->mat->MatCBIndex;
    mRenderItemLayer[(int) RenderLayer::Reflected].emplace_back(reflectedSkullRenderItem.get());
    mReflectedSkullRenderItem = reflectedSkullRenderItem.get();

    auto shadowSkullRenderItem = std::make_unique<RenderItem>();
    *shadowSkullRenderItem = *skullRenderItem;
    shadowSkullRenderItem->objCBIndex = mAllInstanceDataCount++;
    shadowSkullRenderItem->mat = mMaterials["shadowMat"].get();
    shadowSkullRenderItem->instances.resize(1);
    shadowSkullRenderItem->instances[0].materialIndex = shadowSkullRenderItem->mat->MatCBIndex;
    mRenderItemLayer[(int) RenderLayer::Shadow].emplace_back(shadowSkullRenderItem.get());
    mShadowedSkullRenderItem = shadowSkullRenderItem.get();

    mAllRenderItems.emplace_back(std::move(reflectedSkullRenderItem));
    mAllRenderItems.emplace_back(std::move(skullRenderItem));
    mAllRenderItems.emplace_back(std::move(shadowSkullRenderItem));

    auto mirrorRenderItem = std::make_unique<RenderItem>();
    //XMStoreFloat4x4(&mirrorRenderItem->world, XMMatrixTranslation(0.0f, 0.0f, 0.0f));
    mirrorRenderItem->objCBIndex = mAllInstanceDataCount++;
    mirrorRenderItem->geo = mGeometries["roomGeo"].get();
    mirrorRenderItem->mat = mMaterials["icemirrorMat"].get();
    mirrorRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    mirrorRenderItem->indexCount = mirrorRenderItem->geo->DrawArgs["mirror"].IndexCount;
    mirrorRenderItem->startIndexLocation
        = mirrorRenderItem->geo->DrawArgs["mirror"].StartIndexLocation;
    mirrorRenderItem->baseVertexLocation
        = mirrorRenderItem->geo->DrawArgs["mirror"].BaseVertexLocation;
    mirrorRenderItem->instances.resize(1);
    mirrorRenderItem->instances[0].materialIndex = mirrorRenderItem->mat->MatCBIndex;
    mRenderItemLayer[(int) RenderLayer::Mirrors].emplace_back(mirrorRenderItem.get());
    mRenderItemLayer[(int) RenderLayer::Transparent].emplace_back(mirrorRenderItem.get());
    mAllRenderItems.emplace_back(std::move(mirrorRenderItem));

    /*auto treeSpritesRenderItem = std::make_unique<RenderItem>();
    treeSpritesRenderItem->world = MathHelper::Identity4x4();
    treeSpritesRenderItem->objCBIndex = 9;
    treeSpritesRenderItem->mat = mMaterials["treeSpritesMat"].get();
    treeSpritesRenderItem->geo = mGeometries["treeSpritesGeo"].get();
    treeSpritesRenderItem->primitiveType = D3D10_PRIMITIVE_TOPOLOGY_POINTLIST;
    treeSpritesRenderItem->indexCount = treeSpritesRenderItem->geo->DrawArgs["points"].IndexCount;
    treeSpritesRenderItem->baseVertexLocation
        = treeSpritesRenderItem->geo->DrawArgs["points"].BaseVertexLocation;
    treeSpritesRenderItem->startIndexLocation
        = treeSpritesRenderItem->geo->DrawArgs["points"].StartIndexLocation;
    mRenderItemLayer[(int) RenderLayer::AlphaTestedTreeSprites].emplace_back(
        treeSpritesRenderItem.get());
    mAllRenderItems.emplace_back(std::move(treeSpritesRenderItem));*/

    {
        auto boxRitem = std::make_unique<RenderItem>();
        boxRitem->objCBIndex = mAllInstanceDataCount++;
        boxRitem->mat = mMaterials["bricks2Mat"].get();
        boxRitem->geo = mGeometries["shapeGeo"].get();
        boxRitem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        boxRitem->indexCount = boxRitem->geo->DrawArgs["box"].IndexCount;
        boxRitem->startIndexLocation = boxRitem->geo->DrawArgs["box"].StartIndexLocation;
        boxRitem->baseVertexLocation = boxRitem->geo->DrawArgs["box"].BaseVertexLocation;
        boxRitem->boundingBox = boxRitem->geo->DrawArgs["box"].Bounds;
        boxRitem->instances.resize(1);
        boxRitem->instances[0].materialIndex = boxRitem->mat->MatCBIndex;
        XMStoreFloat4x4(
            &boxRitem->instances[0].world,
            XMMatrixScaling(2.0f, 1.0f, 2.0f) * XMMatrixTranslation(0.0f, 15.5f, 0.0f));
        XMStoreFloat4x4(&boxRitem->instances[0].texTransform, XMMatrixScaling(1.0f, 1.0f, 1.0f));
        mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(boxRitem.get());
        mAllRenderItems.emplace_back(std::move(boxRitem));

        auto skullRitem = std::make_unique<RenderItem>();
        skullRitem->objCBIndex = mAllInstanceDataCount++;
        skullRitem->mat = mMaterials["skullMat"].get();
        skullRitem->geo = mGeometries["skullGeo"].get();
        skullRitem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        skullRitem->indexCount = skullRitem->geo->DrawArgs["skull"].IndexCount;
        skullRitem->startIndexLocation = skullRitem->geo->DrawArgs["skull"].StartIndexLocation;
        skullRitem->baseVertexLocation = skullRitem->geo->DrawArgs["skull"].BaseVertexLocation;
        skullRitem->boundingBox = skullRitem->geo->DrawArgs["skull"].Bounds;
        skullRitem->instances.resize(1);
        skullRitem->instances[0].materialIndex = skullRitem->mat->MatCBIndex;
        XMStoreFloat4x4(
            &skullRitem->instances[0].world,
            XMMatrixScaling(0.4f, 0.4f, 0.4f) * XMMatrixTranslation(0.0f, 16.0f, 0.0f));
        mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(skullRitem.get());
        mAllRenderItems.emplace_back(std::move(skullRitem));

        auto gridRitem = std::make_unique<RenderItem>();
        gridRitem->objCBIndex = mAllInstanceDataCount++;
        gridRitem->mat = mMaterials["tileMat"].get();
        gridRitem->geo = mGeometries["shapeGeo"].get();
        gridRitem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        gridRitem->indexCount = gridRitem->geo->DrawArgs["grid"].IndexCount;
        gridRitem->startIndexLocation = gridRitem->geo->DrawArgs["grid"].StartIndexLocation;
        gridRitem->baseVertexLocation = gridRitem->geo->DrawArgs["grid"].BaseVertexLocation;
        gridRitem->boundingBox = gridRitem->geo->DrawArgs["grid"].Bounds;
        gridRitem->instances.resize(1);
        gridRitem->instances[0].materialIndex = gridRitem->mat->MatCBIndex;
        XMStoreFloat4x4(&gridRitem->instances[0].world, XMMatrixTranslation(0.0f, 15.0f, 0.0f));
        XMStoreFloat4x4(&gridRitem->instances[0].texTransform, XMMatrixScaling(8.0f, 8.0f, 1.0f));
        mRenderItemLayer[(int) RenderLayer::Opaque].emplace_back(gridRitem.get());
        mAllRenderItems.emplace_back(std::move(gridRitem));

        auto cylRitem = std::make_unique<RenderItem>();
        cylRitem->geo = mGeometries["shapeGeo"].get();
        cylRitem->mat = mMaterials["bricks2Mat"].get();
        cylRitem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        cylRitem->indexCount = cylRitem->geo->DrawArgs["cylinder"].IndexCount;
        cylRitem->startIndexLocation = cylRitem->geo->DrawArgs["cylinder"].StartIndexLocation;
        cylRitem->baseVertexLocation = cylRitem->geo->DrawArgs["cylinder"].BaseVertexLocation;
        cylRitem->boundingBox = cylRitem->geo->DrawArgs["cylinder"].Bounds;
        cylRitem->instances.resize(10);
        cylRitem->objCBIndex = mAllInstanceDataCount;
        mAllInstanceDataCount += 10;

        auto sphereRitem = std::make_unique<RenderItem>();
        sphereRitem->mat = mMaterials["mirrorMat"].get();
        sphereRitem->geo = mGeometries["shapeGeo"].get();
        sphereRitem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        sphereRitem->indexCount = sphereRitem->geo->DrawArgs["sphere"].IndexCount;
        sphereRitem->startIndexLocation = sphereRitem->geo->DrawArgs["sphere"].StartIndexLocation;
        sphereRitem->baseVertexLocation = sphereRitem->geo->DrawArgs["sphere"].BaseVertexLocation;
        sphereRitem->boundingBox = sphereRitem->geo->DrawArgs["sphere"].Bounds;
        sphereRitem->instances.resize(10);
        sphereRitem->objCBIndex = mAllInstanceDataCount;
        mAllInstanceDataCount += 10;

        XMMATRIX brickTexTransform = XMMatrixScaling(1.5f, 2.0f, 1.0f);
        for (int i = 0; i < 5; ++i) {
            XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 16.5f, -10.0f + i * 5.0f);
            XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 16.5f, -10.0f + i * 5.0f);

            XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 18.5f, -10.0f + i * 5.0f);
            XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 18.5f, -10.0f + i * 5.0f);

            cylRitem->instances[i * 2].materialIndex = cylRitem->mat->MatCBIndex;
            XMStoreFloat4x4(&cylRitem->instances[i * 2].world, leftCylWorld);
            XMStoreFloat4x4(&cylRitem->instances[i * 2].texTransform, brickTexTransform);

            cylRitem->instances[i * 2 + 1].materialIndex = cylRitem->mat->MatCBIndex;
            XMStoreFloat4x4(&cylRitem->instances[i * 2 + 1].world, rightCylWorld);
            XMStoreFloat4x4(&cylRitem->instances[i * 2 + 1].texTransform, brickTexTransform);

            sphereRitem->instances[i*2].materialIndex = sphereRitem->mat->MatCBIndex;
            XMStoreFloat4x4(&sphereRitem->instances[i * 2].world, leftSphereWorld);

            sphereRitem->instances[i*2+1].materialIndex = sphereRitem->mat->MatCBIndex;
            XMStoreFloat4x4(&sphereRitem->instances[i * 2 + 1].world, rightSphereWorld);
            XMStoreFloat4x4(&sphereRitem->instances[i * 2 + 1].texTransform, brickTexTransform);
        }

        mRenderItemLayer[(int) RenderLayer::Opaque].push_back(cylRitem.get());
        mRenderItemLayer[(int) RenderLayer::Opaque].push_back(sphereRitem.get());

        mAllRenderItems.push_back(std::move(cylRitem));
        mAllRenderItems.push_back(std::move(sphereRitem));
    }
}

void LandAndWavesApp::BuildInstanceDataForSkullRenderItem(RenderItem *skullRenderItem)
{
    // Generate instance data.
    const int n = 5;
    auto instanceCount = n * n * n;
    skullRenderItem->instances.resize(instanceCount + 1);

    float width = 200.0f;
    float height = 200.0f;
    float depth = 200.0f;

    float x = -0.5f * width;
    float y = -0.5f * height;
    float z = -0.5f * depth;
    float dx = width / (n - 1);
    float dy = height / (n - 1);
    float dz = depth / (n - 1);
    for (int k = 0; k < n; ++k) {
        for (int i = 0; i < n; ++i) {
            for (int j = 0; j < n; ++j) {
                int index = k * n * n + i * n + j + 1;
                // Position instanced along a 3D grid.
                skullRenderItem->instances[index].world = XMFLOAT4X4(
                    1.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    0.0f,
                    1.0f,
                    0.0f,
                    x + j * dx,
                    y + i * dy + 5.0f,
                    z + k * dz,
                    1.0f);

                XMStoreFloat4x4(
                    &skullRenderItem->instances[index].texTransform,
                    XMMatrixScaling(2.0f, 2.0f, 1.0f));
                skullRenderItem->instances[index].materialIndex = index % (mMaterials.size()-1);

                mAllInstanceDataCount++;
            }
        }
    }
}

void LandAndWavesApp::BuildMaterial()
{
    auto matIndex = 0;
    auto grass = std::make_unique<Material>();
    grass->Name = "grassMat";
    grass->MatCBIndex = matIndex++;
    //grass->DiffuseSrvHeapIndex = 0;
    grass->DiffuseSrvHeapIndex = mDynamicTextureIndex["grassTex"];
    grass->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    grass->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    grass->Roughness = 0.125f;
    mMaterials[grass->Name] = std::move(grass);

    auto water = std::make_unique<Material>();
    water->Name = "waterMat";
    water->MatCBIndex = matIndex++;
    //water->DiffuseSrvHeapIndex = 1;
    water->DiffuseSrvHeapIndex = mDynamicTextureIndex["water1Tex"];
    water->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.5f);
    water->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    water->Roughness = 0.0f;
    mMaterials[water->Name] = std::move(water);

    auto wirefence = std::make_unique<Material>();
    wirefence->Name = "wirefenceMat";
    wirefence->MatCBIndex = matIndex++;
    //wirefence->DiffuseSrvHeapIndex = 2;
    wirefence->DiffuseSrvHeapIndex = mDynamicTextureIndex["WireFenceTex"];
    wirefence->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    wirefence->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    wirefence->Roughness = 0.25f;
    mMaterials[wirefence->Name] = std::move(wirefence);

    auto bricks3 = std::make_unique<Material>();
    bricks3->Name = "bricks3Mat";
    bricks3->MatCBIndex = matIndex++;
    //bricks3->DiffuseSrvHeapIndex = 3;
    bricks3->DiffuseSrvHeapIndex = mDynamicTextureIndex["bricks3Tex"];
    bricks3->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks3->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    bricks3->Roughness = 0.25f;
    mMaterials[bricks3->Name] = std::move(bricks3);

    auto checkertile = std::make_unique<Material>();
    checkertile->Name = "checkertileMat";
    checkertile->MatCBIndex = matIndex++;
    //checkertile->DiffuseSrvHeapIndex = 4;
    checkertile->DiffuseSrvHeapIndex = mDynamicTextureIndex["checkboardTex"];
    checkertile->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    checkertile->FresnelR0 = XMFLOAT3(0.07f, 0.07f, 0.07f);
    checkertile->Roughness = 0.3f;
    mMaterials[checkertile->Name] = std::move(checkertile);

    auto icemirror = std::make_unique<Material>();
    icemirror->Name = "icemirrorMat";
    icemirror->MatCBIndex = matIndex++;
    //icemirror->DiffuseSrvHeapIndex = 5;
    icemirror->DiffuseSrvHeapIndex = mDynamicTextureIndex["iceTex"];
    icemirror->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 0.3f);
    icemirror->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    icemirror->Roughness = 0.5f;
    mMaterials[icemirror->Name] = std::move(icemirror);

    auto skullMat = std::make_unique<Material>();
    skullMat->Name = "skullMat";
    skullMat->MatCBIndex = matIndex++;
    //skullMat->DiffuseSrvHeapIndex = 6;
    skullMat->DiffuseSrvHeapIndex = mDynamicTextureIndex["white1x1Tex"];
    skullMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skullMat->FresnelR0 = XMFLOAT3(0.05f, 0.05f, 0.05f);
    skullMat->Roughness = 0.3f;
    mMaterials[skullMat->Name] = std::move(skullMat);

    auto shadowMat = std::make_unique<Material>();
    shadowMat->Name = "shadowMat";
    shadowMat->MatCBIndex = matIndex++;
    //shadowMat->DiffuseSrvHeapIndex = 6;
    shadowMat->DiffuseSrvHeapIndex = mDynamicTextureIndex["white1x1Tex"];
    shadowMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.0f, 0.5f);
    shadowMat->FresnelR0 = XMFLOAT3(0.001f, 0.001f, 0.001f);
    shadowMat->Roughness = 0.0f;
    mMaterials[shadowMat->Name] = std::move(shadowMat);

    auto skyMat = std::make_unique<Material>();
    skyMat->Name = "skyMat";
    skyMat->MatCBIndex = matIndex++;
    skyMat->DiffuseSrvHeapIndex = 0;
    skyMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    skyMat->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    skyMat->Roughness = 1.0f;
    mMaterials[skyMat->Name] = std::move(skyMat);

    auto bricks2 = std::make_unique<Material>();
    bricks2->Name = "bricks2Mat";
    bricks2->MatCBIndex = matIndex++;
    bricks2->DiffuseSrvHeapIndex = mDynamicTextureIndex["bricks2Tex"];
    bricks2->NormalSrvHeapIndex = mDynamicTextureIndex["bricks2_nmapTex"];
    bricks2->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    bricks2->FresnelR0 = XMFLOAT3(0.1f, 0.1f, 0.1f);
    bricks2->Roughness = 0.3f;
    mMaterials[bricks2->Name] = std::move(bricks2);

    auto tile = std::make_unique<Material>();
    tile->Name = "tileMat";
    tile->MatCBIndex = matIndex++;
    tile->DiffuseSrvHeapIndex = mDynamicTextureIndex["tileTex"];
    tile->NormalSrvHeapIndex = mDynamicTextureIndex["tile_nmapTex"];
    tile->DiffuseAlbedo = XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f);
    tile->FresnelR0 = XMFLOAT3(0.2f, 0.2f, 0.2f);
    tile->Roughness = 0.1f;
    mMaterials[tile->Name] = std::move(tile);

    auto mirror = std::make_unique<Material>();
    mirror->Name = "mirrorMat";
    mirror->MatCBIndex = matIndex++;
    mirror->DiffuseSrvHeapIndex = mDynamicTextureIndex["white1x1Tex"];
    mirror->NormalSrvHeapIndex = mDynamicTextureIndex["default_nmapTex"];
    mirror->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 0.1f, 1.0f);
    mirror->FresnelR0 = XMFLOAT3(0.98f, 0.97f, 0.95f);
    mirror->Roughness = 0.1f;
    mMaterials[mirror->Name] = std::move(mirror);

    /*auto treeSprites = std::make_unique<Material>();
    treeSprites->Name = "treeSpritesMat";
    treeSprites->MatCBIndex = 8;
    treeSprites->DiffuseSrvHeapIndex = 7;
    treeSprites->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
    treeSprites->FresnelR0 = XMFLOAT3(0.01f, 0.01f, 0.01f);
    treeSprites->Roughness = 0.125f;
    mMaterials[treeSprites->Name] = std::move(treeSprites);*/
}

void LandAndWavesApp::LoadTextures()
{
    std::vector<std::pair<std::string, std::wstring>> texInfo = {
        {gSkyBoxTexName, L"/Assets/Textures/grasscube1024.dds"},
        {"bricksTex", L"/Assets/Textures/bricks.dds"},
        {"stoneTex", L"/Assets/Textures/stone.dds"},
        {"tileTex", L"/Assets/Textures/tile.dds"},
        {"WoodCrate01Tex", L"/Assets/Textures/WoodCrate01.dds"},
        {"iceTex", L"/Assets/Textures/ice.dds"},
        {"grassTex", L"/Assets/Textures/grass.dds"},
        {"white1x1Tex", L"/Assets/Textures/white1x1.dds"},
        {"water1Tex", L"/Assets/Textures/water1.dds"},
        {"WireFenceTex", L"/Assets/Textures/WireFence.dds"},
        {"bricks3Tex", L"/Assets/Textures/bricks3.dds"},
        {"checkboardTex", L"/Assets/Textures/checkboard.dds"},
        {"bricks2Tex", L"/Assets/Textures/bricks2.dds"},
        {"bricks2_nmapTex", L"/Assets/Textures/bricks2_nmap.dds"},
        {"tile_nmapTex", L"/Assets/Textures/tile_nmap.dds"},
        {"default_nmapTex", L"/Assets/Textures/default_nmap.dds"},
        //{"treeArray2Tex", L"/Assets/Textures/treeArray2.dds"},
    };

    UINT index = 0;
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

        mSRVHeapTexture.emplace_back(tex.get());
        if (tex->Name != gSkyBoxTexName) {
            mDynamicTextureIndex.insert({it.first, index++});
        }

        mTextures[it.first] = std::move(tex);
    }
}

void LandAndWavesApp::DrawRenderItems(
    ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &renderItems)
{
    //auto objCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
    auto instanceByteSize = sizeof(InstanceData);

    auto resourceObj = mCurrFrameResource->instanceBuffer->Resource();

    for (const auto &item : renderItems) {
        cmdList->IASetIndexBuffer(&item->geo->IndexBufferView());
        cmdList->IASetVertexBuffers(0, 1, &item->geo->VertexBufferView());
        cmdList->IASetPrimitiveTopology(item->primitiveType);

        auto bufferLocation = resourceObj->GetGPUVirtualAddress();
        bufferLocation += (item->objCBIndex * instanceByteSize);
        cmdList->SetGraphicsRootShaderResourceView(0, bufferLocation);

        cmdList->DrawIndexedInstanced(
            item->indexCount,
            item->instanceCount,
            item->startIndexLocation,
            item->baseVertexLocation,
            0);
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
