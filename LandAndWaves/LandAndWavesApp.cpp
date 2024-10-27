#include <cstddef>

#include "LandAndWavesApp.h"
#include "../Common/GeometryGenerator.h"

const int gNumFrameResources = 3;

LandAndWavesApp::LandAndWavesApp(HINSTANCE hInstance) : D3DApp(hInstance)
{
}

bool LandAndWavesApp::Initialize()
{
    if (!D3DApp::Initialize())
        return false;

    // ���������б�Ϊ��ʼ����������׼������
    ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

    BuildRootSignature();
    BuildShadersAndInputLayout();
    BuildLandGeometry();
    BuildWaveGeometryBuffers();
    BuildRenderItems();
    BuildFrameResources();
    //BuildDescriptorHeaps();
    //BuildConstantBufferViews();
    BuildPSOs();

    // ִ�г�ʼ������
    ThrowIfFailed(mCommandList->Close());
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // �ȴ���ʼ�����
    FlushCommandQueue();

    return true;
}

void LandAndWavesApp::OnResize()
{
    D3DApp::OnResize();

    auto projective = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
    XMStoreFloat4x4(&mProj, projective);
}

void LandAndWavesApp::Update(const GameTimer& gt)
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

    // ����
    UpdateObjectCBs(gt);
    UpdateMainPassCB(gt);
    UpdateWaves(gt);
}

void LandAndWavesApp::Draw(const GameTimer& gt)
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

    // ������Դ����;ָʾ��״̬��ת�䣬�˴�����Դ�ӳ���״̬ת��Ϊ��ȾĿ��״̬
    auto resourceBarrierPresentToRenderTarget = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    mCommandList->ResourceBarrier(1, &resourceBarrierPresentToRenderTarget);

    // �����̨����������Ȼ�����
    mCommandList->ClearRenderTargetView(CurrentBackBufferView(),
        Colors::LightSteelBlue, 0, nullptr);
    mCommandList->ClearDepthStencilView(DepthStencilView(),
        D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
        1.0f, 0, 0, nullptr);

    // ָ��Ҫ��Ⱦ��Ŀ�껺����
    mCommandList->OMSetRenderTargets(1,
        &CurrentBackBufferView(),
        true,
        &DepthStencilView());

    //ID3D12DescriptorHeap* descriptorHeaps[] = { mCBVDescriptorHeap.Get() };
    //mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

    mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

    //auto passCBIndex = mPassCBVOffset + mCurrFrameResourceIndex;
    //auto passCBVHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetGPUDescriptorHandleForHeapStart());
    //passCBVHandle.Offset(passCBIndex, mCbvSrvUavDescriptorSize);
    //mCommandList->SetGraphicsRootDescriptorTable(1, passCBVHandle);
    auto resource = mCurrFrameResource->passCB->Resource();
    mCommandList->SetGraphicsRootConstantBufferView(1, resource->GetGPUVirtualAddress());

    DrawRenderItems(mCommandList.Get(), mRenderItemLayer[int(RenderLayer::Opaque)]);

    // ������Դ����;ָʾ��״̬��ת��, �˴�����Դ����ȾĿ��״̬ת��Ϊ����״̬
    auto resourceBarrierRenderTargetToPresent = CD3DX12_RESOURCE_BARRIER::Transition(
        CurrentBackBuffer(),
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_PRESENT);
    mCommandList->ResourceBarrier(1, &resourceBarrierRenderTargetToPresent);

    // ��������¼
    ThrowIfFailed(mCommandList->Close());

    // ���������������ִ�е������б�
    ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
    mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

    // ������̨��������ǰ̨������
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
        mRadius = MathHelper::Clamp(mRadius, 5.0f, 150.0f);
    }

    mLastMousePos.x = x;
    mLastMousePos.y = y;
}

void LandAndWavesApp::OnKeyboardInput(const GameTimer& gt)
{
    if (GetAsyncKeyState('1') & 0x8000)
        mIsWireframe = true;
    else
        mIsWireframe = false;
}

void LandAndWavesApp::UpdateCamera(const GameTimer& gt)
{
    // ��������ת��Ϊ�ѿ�������
    mEyePos.x = mRadius * sinf(mPhi) * cosf(mTheta);
    mEyePos.z = mRadius * sinf(mPhi) * sinf(mTheta);
    mEyePos.y = mRadius * cosf(mPhi);

    // �����۲����
    auto pos = XMVectorSet(mEyePos.x, mEyePos.y, mEyePos.z, 1.0f);
    auto target = XMVectorZero();
    auto up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    auto view = XMMatrixLookAtLH(pos, target, up);
    XMStoreFloat4x4(&mView, view);
}

void LandAndWavesApp::UpdateObjectCBs(const GameTimer& gt)
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

void LandAndWavesApp::UpdateMainPassCB(const GameTimer& gt)
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

void LandAndWavesApp::UpdateWaves(const GameTimer& gt)
{
    static float t_base = 0.0f;
    if ((mTimer.TotalTime() - t_base) >= 0.25f)
    {
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
    for (int i = 0; i < mWaves->VertexCount(); ++i)
    {
        Vertex v;

        v.pos = mWaves->Position(i);
        v.color = XMFLOAT4(DirectX::Colors::Blue);

        currWavesVB->CopyData(i, v);
    }

    mWavesRenderItem->geo->VertexBufferGPU = currWavesVB->Resource();
}

void LandAndWavesApp::BuildLandGeometry()
{
    GeometryGenerator geoGen;
    GeometryGenerator::MeshData grid = geoGen.CreateGrid(160.0f, 160.0f, 50, 50);
    std::vector<Vertex> vertices(grid.Vertices.size());
    for (size_t i = 0; i < grid.Vertices.size(); ++i)
    {
        auto& p = grid.Vertices[i].Position;
        vertices[i].pos = p;
        vertices[i].pos.y = GetHillsHeight(p.x, p.z);

        // Color the vertex based on its height.
        if (vertices[i].pos.y < -10.0f)
        {
            // Sandy beach color.
            vertices[i].color = XMFLOAT4(1.0f, 0.96f, 0.62f, 1.0f);
        }
        else if (vertices[i].pos.y < 5.0f)
        {
            // Light yellow-green.
            vertices[i].color = XMFLOAT4(0.48f, 0.77f, 0.46f, 1.0f);
        }
        else if (vertices[i].pos.y < 12.0f)
        {
            // Dark yellow-green.
            vertices[i].color = XMFLOAT4(0.1f, 0.48f, 0.19f, 1.0f);
        }
        else if (vertices[i].pos.y < 20.0f)
        {
            // Dark brown.
            vertices[i].color = XMFLOAT4(0.45f, 0.39f, 0.34f, 1.0f);
        }
        else
        {
            // White snow.
            vertices[i].color = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
        }
    }

    // �ȴ���������������壬�����ݿ������ϴ��ѣ��ٴ��ϴ����ύ��Ĭ�϶ѣ��˲�ʹ�������б���¼���ύ�����������ִ�У�
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

    geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        geo->VertexBufferCPU->GetBufferPointer(), vbByteSize, geo->VertexBufferUploader);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(), mCommandList.Get(),
        geo->IndexBufferCPU->GetBufferPointer(), ibByteSize, geo->IndexBufferUploader);

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
    for (int i = 0; i < m - 1; ++i)
    {
        for (int j = 0; j < n - 1; ++j)
        {
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
    UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

    auto geo = std::make_unique<MeshGeometry>();
    geo->Name = "waterGeo";
    geo->VertexByteStride = sizeof(Vertex);
    geo->VertexBufferByteSize = vbByteSize;
    geo->IndexFormat = DXGI_FORMAT_R16_UINT;
    geo->IndexBufferByteSize = ibByteSize;

    // �˵Ķ����Ƕ�̬���ݣ����Խ�����������Ĭ�϶���
    geo->VertexBufferCPU = nullptr;
    geo->VertexBufferGPU = nullptr;

    ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
    CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

    geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
        mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

    SubmeshGeometry submesh;
    submesh.IndexCount = (UINT)indices.size();
    submesh.StartIndexLocation = 0;
    submesh.BaseVertexLocation = 0;

    geo->DrawArgs["grid"] = submesh;

    mGeometries[geo->Name] = std::move(geo);
}

//void LandAndWavesApp::BuildDescriptorHeaps()
//{
//    auto objCount = (UINT)mOpaqueRenderItems.size();
//    auto numDescriptor = (objCount + 1) * gNumFrameResources;
//
//    // ������Ⱦ����CBV����ʼƫ�������ڴ˳����������3��������
//    mPassCBVOffset = objCount * gNumFrameResources;
//
//    D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
//    cbvHeapDesc.NumDescriptors = numDescriptor;
//    cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//    cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//    cbvHeapDesc.NodeMask = 0;
//    ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&mCBVDescriptorHeap)));
//}

//void LandAndWavesApp::BuildConstantBufferViews()
//{
//    // ÿ��֡��Դ��ÿ�����嶼��Ҫһ����Ӧ��CBV������
//    {
//        UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
//        auto objCount = (UINT)mOpaqueRenderItems.size();
//
//        for (uint32_t i = 0; i < gNumFrameResources; ++i) {
//            auto resource = mFrameResources[i]->objectCB->Resource();
//            for (uint32_t j = 0; j < objCount; ++j) {
//                auto address = resource->GetGPUVirtualAddress();
//                address += j * objCBByteSize;   // ƫ�Ƶ��������е�j������ĳ�����������ַ
//
//                D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
//                cbvDesc.BufferLocation = address;
//                cbvDesc.SizeInBytes = objCBByteSize;
//
//                auto heapIndex = i * objCount + j;
//                auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
//                handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);
//
//                md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
//            }
//        }
//    }
//
//    // ���3����������ÿ֡��Դ����Ⱦ����CBV
//    {
//        UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));
//
//        for (auto i = 0; i < gNumFrameResources; ++i) {
//            auto resource = mFrameResources[i]->passCB->Resource();
//            // ÿ��֡��Դ����Ⱦ���̻�����ֻ����һ������������
//            auto address = resource->GetGPUVirtualAddress();
//
//            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
//            cbvDesc.BufferLocation = address;
//            cbvDesc.SizeInBytes = passCBByteSize;
//
//            auto heapIndex = mPassCBVOffset + i;
//            auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCBVDescriptorHeap->GetCPUDescriptorHandleForHeapStart());
//            handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);
//
//            md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
//
//        }
//    }
//}

void LandAndWavesApp::BuildRootSignature()
{
    // ������������
    //CD3DX12_DESCRIPTOR_RANGE cbvTable0;
    //cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

    //CD3DX12_DESCRIPTOR_RANGE cbvTable1;
    //cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

    //// �������������������������������������
    //CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    //slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
    //slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);


    CD3DX12_ROOT_PARAMETER slotRootParameter[2];
    slotRootParameter[0].InitAsConstantBufferView(0);
    slotRootParameter[1].InitAsConstantBufferView(1);


    CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(2, slotRootParameter, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    // ������ǩ��
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

void LandAndWavesApp::BuildShadersAndInputLayout()
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

void LandAndWavesApp::BuildPSOs()
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

void LandAndWavesApp::BuildFrameResources()
{
    for (int i = 0; i < gNumFrameResources; ++i)
    {
        mFrameResources.emplace_back(std::make_unique<FrameResource>(
            md3dDevice.Get(), 1, static_cast<uint32_t>(mAllRenderItems.size()), mWaves->VertexCount()));
    }
}

void LandAndWavesApp::BuildRenderItems()
{
    auto wavesRenderItem = std::make_unique<RenderItem>();
    wavesRenderItem->world = MathHelper::Identity4x4();
    wavesRenderItem->objCBIndex = 0;
    wavesRenderItem->geo = mGeometries["waterGeo"].get();
    wavesRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    wavesRenderItem->indexCount = wavesRenderItem->geo->DrawArgs["grid"].IndexCount;
    wavesRenderItem->startIndexLocation = wavesRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    wavesRenderItem->baseVertexLocation = wavesRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;
    
    mWavesRenderItem = wavesRenderItem.get();

    mRenderItemLayer[(int)RenderLayer::Opaque].emplace_back(mWavesRenderItem);

    auto gridRenderItem = std::make_unique<RenderItem>();
    gridRenderItem->world = MathHelper::Identity4x4();
    gridRenderItem->objCBIndex = 0;
    gridRenderItem->geo = mGeometries["landGeo"].get();
    gridRenderItem->primitiveType = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    gridRenderItem->indexCount = gridRenderItem->geo->DrawArgs["grid"].IndexCount;
    gridRenderItem->startIndexLocation = gridRenderItem->geo->DrawArgs["grid"].StartIndexLocation;
    gridRenderItem->baseVertexLocation = gridRenderItem->geo->DrawArgs["grid"].BaseVertexLocation;

    mRenderItemLayer[(int)RenderLayer::Opaque].emplace_back(gridRenderItem.get());

    mAllRenderItems.emplace_back(std::move(wavesRenderItem));
    mAllRenderItems.emplace_back(std::move(gridRenderItem));
}

void LandAndWavesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& renderItems)
{
    auto objCbByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

    auto resource = mCurrFrameResource->objectCB->Resource();

    for (const auto& item : renderItems) {
        cmdList->IASetIndexBuffer(&item->geo->IndexBufferView());
        cmdList->IASetVertexBuffers(0, 1, &item->geo->VertexBufferView());
        cmdList->IASetPrimitiveTopology(item->primitiveType);

        auto objCBAddress = resource->GetGPUVirtualAddress();
        objCBAddress += item->objCBIndex * objCbByteSize;
        cmdList->SetGraphicsRootConstantBufferView(0, objCBAddress);

        cmdList->DrawIndexedInstanced(item->indexCount, 1, item->startIndexLocation, item->baseVertexLocation, 0);
    }
}

float LandAndWavesApp::GetHillsHeight(float x, float z)const
{
    return 0.3f * (z * sinf(0.1f * x) + x * cosf(0.1f * z));
}

XMFLOAT3 LandAndWavesApp::GetHillsNormal(float x, float z)const
{
    XMFLOAT3 n(
        -0.03f * z * cosf(0.1f * x) - 0.3f * cosf(0.1f * z),
        1.0f,
        -0.3f * sinf(0.1f * x) + 0.03f * x * sinf(0.1f * z));

    XMVECTOR unitNormal = XMVector3Normalize(XMLoadFloat3(&n));
    XMStoreFloat3(&n, unitNormal);

    return n;
}