[TOC]



## 基本流程

1. 创建窗口：CreateWindow；
2. 创建DXFGI Factory对象：CreateDXGIFactory；
3. 枚举适配器，并选择合适的适配器来创建3D设备对象：IDXGIFactory4::EnumAdapters   ---->  D3D12CreateDevice；
4. 创建D3D12对象：D3D12CreateDevice；
5.  创建Fence（用于同步）：ID3D12Device::CreateFence；
6. 创建命令队列：ID3D12Device::CreateCommonQueue；
7. 创建命令分配器：ID3D12Device::CreateCommandAllocator；
8. 创建图形命令列表：ID3D12Device::CreateCommandList；
9. 创建交换链：IDXGIFactory4::CreateSwapChain；会同时创建
10. 【可选】创建深度模板缓冲区资源：ID3D12Device::CreateCommittedResource；
11. 创建RTV（渲染目标描述符）和【可选】DSV（深度/模板缓冲区描述符）描述符堆：ID3D12Device::CreateDescriptorHeap；
12. 【可选（根据需要）】创建SRV堆（Shader Resource View Heap）等其他描述符堆：ID3D12Device::CreateDescriptorHeap；
    1. 为对应的描述符堆创建描述符：
       1. 创建SRV描述符：ID3D12Device::CreateShaderResourceView；
       2. 创建CBV描述符：ID3D12Device::CreateConstantBufferView；
       3. 创建UAV描述符：ID3D12Device::CreateUnorderedAccessView；
13. 创建根签名：根签名由一组描述绘制调用过程中着色器资源的根参数定义而成。根参数可以是描述符表、根描述符或者根常量。描述符表指定的是描述符堆中存有描述符的一块连续区域。根签名可以理解为函数声明，声明了函数的形参类型，形参数量等信息，具体的实参会以命令的方式记录到命令列表中。
    1. 定义根参数 CD3DX12_ROOT_PARAMETER：
       1. 描述符表 CD3DX12_DESCRIPTOR_RANGE：CD3DX12_DESCRIPTOR_RANGE::Init
       2. 根描述符：直接初始化
       3. 根常量：直接初始化
    2. 根参数初始化 ：对于不同类型的根参数需要使用不同的初始化接口
       1. 描述符表 ：CD3DX12_ROOT_PARAMETER::InitAsDescriptorTable
       2. 根描述符：
          1. 常量缓冲区描述符： CD3DX12_ROOT_PARAMETER::InitAsConstantBufferView
          2. 着色器资源描述符： CD3DX12_ROOT_PARAMETER::InitAsShaderResourceView
          3. 无序访问描述符： CD3DX12_ROOT_PARAMETER::InitAsUnorderedAccessView
       3. 根常量：CD3DX12_ROOT_PARAMETER::InitAsConstants
    3. 序列号根签名：D3D12SerializeRootSignature
    4. 创建根签名：ID3D12Device::CreateRootSignature
14. 编译Shader：D3DCompileFromFile  
15. 创建渲染管线对象：ID3D12Device::CreateGraphicsPipelineState
    1. 根签名
    2. 定义管线的输入装配布局

16. 获取当前后缓冲区的序号，也就是下一个将要呈送显示的缓冲区的序号：IDXGISwapChain::GetCurrentBackBufferIndex
17. 绘制：
    1. 命令列表记录命令
       1. Reset 管线：ID3D12GraphicsCommandList::Reset
       2. Viewport：ID3D12GraphicsCommandList::RSSetViewports
       3. 裁剪矩形：ID3D12GraphicsCommandList::RSSetScissorRects
       4. 转换资源状态：此次需要将资源从呈现状态转换为渲染目标状态：ID3D12GraphicsCommandList::ResourceBarrier
       5. 清空后台缓冲区：ID3D12GraphicsCommandList::ClearRenderTargetView
       6. 清空深度缓冲区：ID3D12GraphicsCommandList::ClearDepthStencilView
       7. 指定要渲染的模板缓冲区：ID3D12GraphicsCommandList::OMSetRenderTargets
       8. 设置根签名：ID3D12GraphicsCommandList::SetGraphicsRootSignature
       9. 设置描述符堆：ID3D12GraphicsCommandList::SetDescriptorHeaps
       10. 绑定根签名参数：
           1. 绑定描述符表：ID3D12GraphicsCommandList::SetGraphicsRootDescriptorTable
           2. 绑定根描述符：
              1. 常量缓冲区描述符：ID3D12GraphicsCommandList::SetGraphicsRootConstantBufferView
              2. 着色器资源描述符：ID3D12GraphicsCommandList::SetGraphicsRootShaderResourceView
              3. 无序访问描述符：ID3D12GraphicsCommandList::SetGraphicsRootUnorderedAccessView
           3. 绑定根常量：ID3D12GraphicsCommandList::SetGraphicsRoot32BitConstant
       11. 设置索引缓冲区：ID3D12GraphicsCommandList::
       12. 设置顶点缓冲区：ID3D12GraphicsCommandList::
       13. 设置图元类型：ID3D12GraphicsCommandList::
       14. 绘制方式：
           1. ID3D12GraphicsCommandList::DrawIndexedInstanced
           2. ID3D12GraphicsCommandList::DrawInstanced
    2. 完成记录命令：ID3D12GraphicsCommandList::Close
    3. 将命令列表提交到命令队列中：ID3D12CommandQueue::ExecuteCommandLists
    4. 交换后台缓冲区
    5. 发送同步Fence：ID3D12CommandQueue::Signal
18. 【可选】窗口Resize
    1. 需要等待之前的命令执行完毕
    2. 重置命令列表：ID3D12GraphicsCommandList::Reset
    3. 重置交换链资源缓冲区：ID3D12Resource::Reset
    4. 【可选】重置深度/模板缓冲区：ID3D12Resource::Reset
    5. 重置交换链：IDXGISwapChain::ResizeBuffers
    6. 为对应的描述符堆创建描述符：
       1. 创建RTV描述符：ID3D12Device::CreateRenderTargetView
       2. 【可选】创建DSV描述符：ID3D12Device::CreateDepthStencilView
    7. 如有命令提交到命令队列（比如深度/模板缓冲区状态的转换）就等待命令执行完毕，以防对后续操作产生影响。



## 资源

### 堆的类型



### 资源的创建方式



## 渲染流水线

输入装配阶段

​	顶点

​	图元拓扑

​	索引

顶点着色器阶段

​	局部空间和世界空间

​	观察空间

​	投影和齐次裁剪空间

曲面细分阶段

几何着色器阶段

裁剪

光栅化阶段

​	视口变换

​	背面剔除

​	顶点属性插值

像素着色器阶段

输出合并阶段



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

根签名：根签名由一组描述绘制调用过程中着色器资源的根参数定义而成。根参数可以是描述符表、根描述符或者根常量。描述符表指定的是描述符堆中存有描述符的一块连续区域。根签名可以理解为函数声明，声明了函数的形参类型，形参数量等信息，具体的实参会以命令的方式记录到命令列表中。\

**描述符表**：描述符引用的是描述符堆中的一块连续范围，用于确定要绑定的资源。每个描述符表占用1DWORD（4字节）。

**根描述符**：通过直接设置描述符即可指示要绑定的资源，而且无需将它存于描述符堆中。但是只有常量缓冲区的CBV，以及缓冲区的SRV/UAV才可以根据描述符的身份进行绑定。而纹理的SRV并不能作为根描述符来实现资源绑定。每个根描述符（64位的GPU虚拟地址）占用2DWORD（8字节）。

**根常量**：借助根常量可直接绑定一系列32位的常量。每个常量32位，占用一个DWORD。

1. 定义根参数 CD3DX12_ROOT_PARAMETER：
   1. 描述符表 CD3DX12_DESCRIPTOR_RANGE：CD3DX12_DESCRIPTOR_RANGE::Init
   2. 根描述符：直接初始化
   3. 根常量：直接初始化
2. 根参数初始化 ：对于不同类型的根参数需要使用不同的初始化接口
   1. 描述符表 ：CD3DX12_ROOT_PARAMETER::InitAsDescriptorTable
   2. 根描述符：
      1. 常量缓冲区描述符： CD3DX12_ROOT_PARAMETER::InitAsConstantBufferView
      2. 着色器资源描述符： CD3DX12_ROOT_PARAMETER::InitAsShaderResourceView
      3. 无序访问描述符： CD3DX12_ROOT_PARAMETER::InitAsUnorderedAccessView
   3. 根常量：CD3DX12_ROOT_PARAMETER::InitAsConstants
3. 序列号根签名：D3D12SerializeRootSignature
4. 创建根签名：ID3D12Device::CreateRootSignature



## 混合

混合是一种将当前待光栅化的像素（即源像素）与之前已光栅化并存至后台缓冲区的像素（即目标像素）相融合的技术。

混合中的RGB分量与alpha分量的混合运算是各自单独计算的。



## 模板

模板缓冲区是一种离屏缓冲区，我可以通过它来阻止特定像素片段向后台缓冲区的渲染操作。



## 几何着色器

顶点着色器（VS） ----> 外壳着色器（HS）----> 镶嵌器着色器 ----> 域着色器（DS）----> 几何着色器（GS）----> 像素着色器（PS）

如果不启用几何曲面细分（即处于中间的三个渲染步骤）这一环节，几何着色器便位于顶点和像素着色器之间。

几何着色器的一般格式：

```hlsl
[maxvertexcount(N)]	// 设置几何着色器单次调用所输出的顶点最大数量
void shaderName(
	PrimitiveType InputVertexType inputName[NumElement], 
	inout StreamOutputObject<OutputVertexType> outputName	// 输出参数
){
	// 几何着色器的具体实现
}
```

PrimitiveType可以是一下类型之一：

- point：输入的图元为点
- line：输入的图元为线列表或线条带
- triangle：输入的图元为三角形列表或三角形带
- lineadj：输入的图元为线列表及其邻接图元，或线条带及其邻接图元
- triangleadj：输入的图元为三角形列表及其邻接图元，或三角形带及其邻接图元

几何着色器的输出参数一定要有inout修饰符，而且必须是一种流类型。流类型有如下三种：

- PointStream<OutputVertexType>: 一系列顶点所定义的点带
- LineStream<OutputVertexType>: 一系列顶点所定义的线带
- TriangleStream<OutputVertexType>: 一系列顶点所定义的三角形带

[unroll]

`[unroll]`是一个着色器指令，用于指示编译器尽可能地展开循环，以减少循环迭代时的开销。这通常用于性能优化，特别是在处理固定次数的迭代时，例如在处理矩阵运算或向量操作时。`[unroll]`指令可以应用于`for`循环，以确保循环体被复制多次，而不是使用循环结构，这有助于提高执行效率，尤其是在GPU编程中。



## 计算着色器

