1. 创建窗口：CreateWindow
2. 创建DXFGI Factory对象：CreateDXGIFactory
3. 枚举适配器，并选择合适的适配器来创建3D设备对象：pIDXGIFactory->EnumAdapters   ---->  D3D12CreateDevice
4. 创建D3D12对象：D3D12CreateDevice
5. 创建命令队列：pID3DDevice->CreateCommonQueue
6. 创建交换链：pIDXGIFactory->CreateSwapchainForHwnd
7. 获取当前后缓冲区的序号，也就是下一个将要呈送显示的缓冲区的序号：pISwapchain->GetCurrentBackBufferIndex
8. 创建RTV（渲染目标视图）描述符堆：pID3DDevice->CreateDescriptorHeap
9. 创建RTV描述符：pID3DDevice->CreateRenderTargetView
10. 创建SRV堆（Shader Resource View Heap）：pID3DDevice->CreateDescriptorHeap
11. 创建根签名：
12. 编译Shader并创建渲染管线对象：D3DCompileFromFile  ----> pID3DDevice->CreateGraphicsPipelineState
13. 创建命令分配器：pID3DDevice->CreateCommandAllocator
14. 创建图形命令列表：pID3DDevice->CreateCommandList



## 描述符堆
DescriptorHeap描述符堆是描述符(也可叫理解为视图)的集合；
描述符分为：

- 渲染目标描述符（RTV: Render Target View）

- 深度/模板缓冲区描述符（DSV: Depath Stencil View）

- 常量缓冲区描述符（CBV: Constant Buffer View）

- 着色器资源描述符(SRV: Shader Resource View)

- 无序访问描述符（UAV: Unordered Access View）

- 顶点缓冲区描述符（VBV: Vertex Buffer View）

- 索引缓冲区描述（IBV: Index Buffer View）

除了顶点和索引缓冲区不需要创建对应的描述符堆外，其他类型的描述符需要存放在对应类型的描述符堆里。
RTV存放在以D3D12_DESCRIPTOR_HEAP_TYPE_RTV类型创建的描述符堆里；
DSV存放在以D3D12_DESCRIPTOR_HEAP_TYPE_DSV类型创建的描述符堆里；
Sampler存放在以D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER类型创建的描述符堆里；
其中CBV、SRV、UAV混合存放在以D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV类型创建的描述符堆里。
描述符中每项描述符的类型由描述符堆的type决定（`type可以是 D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV | D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER |  D3D12_DESCRIPTOR_HEAP_TYPE_RTV | D3D12_DESCRIPTOR_HEAP_TYPE_DSV`）；每个描述符的大小由设备决定，并通过设备的接口来查询；

不同类型的描述符创建：
RTV：ID3D12Device::CreateRenderTargetView
DSV：ID3D12Device::CreateDepthStencilView
SRV：ID3D12Device::CreateShaderResourceView
CBV：ID3D12Device::CreateConstantBufferView
UAV：ID3D12Device::CreateUnorderedAccessView
Sampler：ID3D12Device::CreateSampler


## 根签名





## 混合

混合是一种将当前待光栅化的像素（即源像素）与之前已光栅化并存至后台缓冲区的像素（即目标像素）相融合的技术。

