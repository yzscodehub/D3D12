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

