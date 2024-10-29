#pragma once
#include "FrameResource.h"
#include "Waves.h"

#include "../Common/MathHelper.h"
#include "../Common/UploadBuffer.h"
#include "../Common/d3dApp.h"

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

struct RenderItem
{
    RenderItem() = default;

    XMFLOAT4X4 world = MathHelper::Identity4x4();

    XMFLOAT4X4 texTransform = MathHelper::Identity4x4();

    int numFramesDirty = gNumFrameResources;

    UINT objCBIndex = -1;

    MeshGeometry *geo = nullptr;
    Material *mat = nullptr;

    D3D12_PRIMITIVE_TOPOLOGY primitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT indexCount = 0;
    UINT startIndexLocation = 0;
    int baseVertexLocation = 0;
};

enum class RenderLayer : int { Opaque = 0, Count };

class LandAndWavesApp : public D3DApp
{
public:
    LandAndWavesApp(HINSTANCE hInstance);
    LandAndWavesApp(const LandAndWavesApp &rhs) = delete;
    LandAndWavesApp(const LandAndWavesApp &&rhs) = delete;
    LandAndWavesApp &operator=(const LandAndWavesApp &rhs) = delete;
    LandAndWavesApp &operator=(const LandAndWavesApp &&rhs) = delete;

    virtual bool Initialize() override;

private:
    virtual void OnResize() override;
    virtual void Update(const GameTimer &gt) override;
    virtual void Draw(const GameTimer &gt) override;

    virtual void OnMouseDown(WPARAM btnState, int x, int y) override;
    virtual void OnMouseUp(WPARAM btnState, int x, int y) override;
    virtual void OnMouseMove(WPARAM btnState, int x, int y) override;

    void OnKeyboardInput(const GameTimer &gt);
    void AnimateMaterials(const GameTimer &gt);
    void UpdateCamera(const GameTimer &gt);
    void UpdateObjectCBs(const GameTimer &gt);
    void UpdateMainPassCB(const GameTimer &gt);
    void UpdateMaterialCB(const GameTimer &gt);

    void UpdateWaves(const GameTimer &gt);

    void BuildLandGeometry();
    void BuildWaveGeometryBuffers();
    void BuildBoxGeometry();
    void BuildRenderItems();
    void BuildMaterial();
    void LoadTextures();

    void BuildDescriptorHeaps();
    void BuildRootSignature();
    void BuildShadersAndInputLayout();
    void BuildPSOs();
    void BuildFrameResources();
    void DrawRenderItems(
        ID3D12GraphicsCommandList *cmdList, const std::vector<RenderItem *> &renderItems);

    std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

    float GetHillsHeight(float x, float z) const;
    XMFLOAT3 GetHillsNormal(float x, float z) const;

private:
    std::vector<std::unique_ptr<FrameResource>> mFrameResources;
    FrameResource *mCurrFrameResource;
    int mCurrFrameResourceIndex = 0;

    ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
    ComPtr<ID3D12DescriptorHeap> mCBVDescriptorHeap = nullptr;
    ComPtr<ID3D12DescriptorHeap> mSRVDescriptorHeap = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
    std::unordered_map<std::string, std::unique_ptr<Material>> mMaterials;
    std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;

    std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
    std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;


    std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;

    std::vector<std::unique_ptr<RenderItem>> mAllRenderItems;

    std::vector<RenderItem *> mRenderItemLayer[(int) RenderLayer::Count];

    std::unique_ptr<Waves> mWaves;
    RenderItem *mWavesRenderItem = nullptr;


    PassConstants mMainPassCB;
    UINT mPassCBVOffset = 0;

    bool mIsWireframe = false;

    XMFLOAT3 mEyePos = {0.0f, 0.0f, 0.0f};
    XMFLOAT4X4 mView = MathHelper::Identity4x4();
    XMFLOAT4X4 mProj = MathHelper::Identity4x4();

    float mTheta = 1.5f * XM_PI;
    float mPhi = XM_PIDIV2 - 0.1f;
    float mRadius = 50.0f;

    float mSunTheta = 1.25f * XM_PI;
    float mSunPhi = XM_PIDIV4;

    POINT mLastMousePos;
};