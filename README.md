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

顶点着色器阶段（VS）

​	局部空间和世界空间

​	观察空间

​	投影和齐次裁剪空间

曲面细分阶段

​	外壳着色器（HS）

​	镶嵌器着色器（该阶段的操作全权交由硬件完成，程序员无法对其进行任何控制）

​	域着色器（DS）

几何着色器阶段（GS）

裁剪

光栅化阶段

​	视口变换

​	背面剔除

​	顶点属性插值

像素着色器阶段（PS）

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

- `ID3D12GraphicsCommandList::Dispatch`可分派一个线程组网格，每个线程组都是线程构成的3D网格，线程组中的线程数由计算着色器里的`[numthreads(x,y,x)]`属性来指定。出于性能的考量，线程的总数应为warp大小（NVIDIA）/wavefront尺寸（AMD）的整数倍。（warp：32; wavefront: 64）；

- 为了保证处理器的并发性，应至少为每个多处理器分派两个线程组。所以，若硬件具有16个多处理器，则应当至少调度32个线程组。

- 结构化缓冲区是一种由相同类型元素构成的缓冲区，这与数组有些相似。其元素类型可以是用户自定义的结构体。

  - 只读结构化缓冲区的HLSL定义：

    ```hlsl
    StructuredBuffer<DataType> gInputA;
    ```

  - 读写结构化缓冲区的HLSL定义：

    ```hlsl
    RWStructuredBuffer<DataType> gInputA;
    ```

​	要让计算着色器访问只读缓冲区资源，只需为它创建对应的SRV，再将其绑定计算着色器上。

​	要让计算着色器访问可读写缓冲区资源，需将其创建可读写的UAV，再将其绑定计算着色器上。

- 各种类型的线程ID可通过系统值传入计算着色器中。这些ID则通常会作资源与共享内存的索引。

- 消费结构化缓冲区与追加结构化缓冲区在HLSL的定义：

  ```hlsl
  ConsumeStructuredBuffer<DataType> gInput;
  AppendStructuredBuffer<DataType> gOutput;
  ```

  如果数据元素的处理顺序与最终写入输出缓冲区中的顺序是无关紧要的，那么这两种结构缓冲区将是不错的选择，因为它们能使我们绕开繁琐的索引语法。要注意的是，追加缓冲区的空间并不能自动按需增长，但是它们一定有足够空间来容下我们先其追加的所有数据元素。

- 所有的线程组都有一块被称为共享内存或线程本地存储器的空间。该共享内存的访问速度极快，可以与硬件缓存比肩。而且，此共享内存对于性能优化或实现特定算法极为有益。在计算着色器的代码中，共享内存的声明如下：

  ```hlsl
  groupshared float4 gCache[N];
  ```

  N是用户所需的数组大小，但是要注意，线程组共享内存的上限是32kb。假设一个多处理器最多支持32kb的共享内存，考虑到性能，一个线程组所用的共享内存应不多于16kb，否则一个多处理器将无法替代运行两个这样的线程组，从而难保证其持续运行。

- 尽量避免在计算处理与渲染过程之间进行切换，因为这会产生开销。一般来说，我们在每帧中应先尝试完成所有的计算任务，而后再执行后续的所有渲染工作。



## 动态索引

动态索引是着色器模型5.1引入的新技术，它使我们可以对具有不同大小及格式纹理的数组进行动态索。此技术的一项应用是：在我们绘制每一帧画面时，可一次性绑定所有的纹理描述符，随后，在像素着色器中对纹理数组进行动态索引来为像素找到它对应的纹理。



## 实例化

实例化技术可用于将场景中的同一对象绘制多次，以不同的位置、朝向、缩放（大小）、材质与纹理等绘制多次。为了节约内存资源，我们可以仅创建一个网格，再利用不同的世界矩阵、材质以及纹理向Direct3D提交多个绘制调用。为了避免资源变动和多次绘制调用所带来的API开销，我们可以给存有全部实例数据的结构化缓冲区绑定一个SRV，并利用`SV_InstanceID`系统值在顶点着色器中对其进行索引。另外我们还可以通过动态索引来索引纹理数组。单次绘制调用中要渲染的实例个数由`ID3D12GraphicsCommandList::DrawIndexedInstanced`方法的第二参数`InstanceCount`指定。



## 视锥体剔除

GPU会在裁剪阶段自动抛弃位于视锥体之外的三角形。但是，那些终将被裁剪的三角形仍要先通过绘制调用（会产生API开销）提交至渲染流水线，并经过顶点着色器的处理，还极有可能传到曲面细分阶段与几何着色器中内，直到在裁剪阶段中才会被丢弃。为了改善这种无效率的处理流程。我们可以采用视锥体剔除技术。此方法的思路是构建一个包围体，包围球、包围盒都可以，使它们分别包围场景中的每一个物体。如果包围体与视锥体没有交集，则无须将物体交给Direct3D绘制。此方法通过开销较小的CPU测试大大节省了GPU资源的浪费，从而使它不必为看不到的几何图形“买单”。



## 立方体贴图

- 立方体图由6个纹理组成，我们把它们分别视为立方体的每一个面。在Direct3D12中，可以通过ID3D12Resource接口将立方体图表示为具有6个元素的纹理数组。而在HLSL中，立方体图由TextureCube类型表示。我们使用3D纹理坐标来指定立方体图上的纹素，它定义了一个以立方体图中心为起点的3D查找向量v。该向量与立方体图相交处的纹素即为v的3D坐标所对应的纹素。
- 环境图即为在某点处（以不同视角）对周围环境截取的6张图像，而这些图像最终会存与一个立方体图之中。通过环境图我们就能方便地渲染天空或模拟反射。

### 动态立方体图

- 预先烘焙的立方体图既不能截取场景中的移动对象，也无法采集在它生成时还不曾存在的物体。为了克服这种限制，我们需要在运行时动态得构建立方体图。也就是说，我们在每一帧都要将摄像机架设在场景中某处，以作为立方体图得原点，并沿着每个坐标轴方向将场景分6次渲染至每个立方体图的面上。因为每一帧都要重新构建立方体，所以就能截取到动态对象以及环境中的没一样物体。动态立方体图的开销极大，因此应当谨慎地将其用与关键物品的渲染。
- 使用几何着色器实现对立方体图6个面的同时渲染。我们可以将数组纹理的渲染目标视图绑定至渲染流水线的OM阶段，也能对纹理数组中的每个数组切片同时进行渲染。利用系统值`SV_RenderTargetArrayIndex`便可以把三角形赋予特定的给渲染目标数组切片。假设现有一个纹理数组的渲染目标视图，我们利用`SV_RenderTargetArrayIndex`系统值即可一次性渲染整个场景来动态地生成立方体图，而不必再对每个面一一进行绘制（共6次）。但是，这个策略并非在任何情况下都优于利用视锥体剔除共需渲染场景6次地方法。



## 法线贴图



## 阴影贴图



## 环境光遮蔽

### 屏幕空间环境光遮蔽



## 动画







