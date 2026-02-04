#pragma once

class GraphicsCore
{
public:
	GraphicsCore() {}
	~GraphicsCore() {}
	GraphicsCore(const GraphicsCore&) = delete;
	GraphicsCore& operator=(const GraphicsCore&) = delete;

	void Initialize(HWND hWnd, float width, float height);
	void Resize(float width, float height);

	ID3D12GraphicsCommandList* BeginFrame();
	void EndFrame();
	void MoveToNextFrame();
	void WaitForGpu();

	ID3D12Device* GetDevice() const { return m_Device.Get(); }
	ID3D12CommandQueue* GetCommandQueue() const { return m_CommandQueue.Get(); }
	ID3D12GraphicsCommandList* GetCommandList() const { return m_CommandList.Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDepthStencilView() const { return m_DsvHeap->GetCPUDescriptorHandleForHeapStart(); }

	static constexpr UINT FrameCount = 3;
private:
	HWND m_Hwnd = nullptr;
	float m_AspectRatio = 0;
	float m_Width = 0;
	float m_Height = 0;

	float m_ClearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f, };

	// Core Components
	ComPtr<ID3D12Device> m_Device;
	ComPtr<ID3D12CommandQueue> m_CommandQueue;
	ComPtr<IDXGISwapChain3> m_SwapChain;

	ComPtr<ID3D12CommandAllocator> m_CommandAllocators[FrameCount];
	ComPtr<ID3D12GraphicsCommandList> m_CommandList;

	// Heaps & Descriptors
	ComPtr<ID3D12Resource> m_RenderTargets[FrameCount];
	ComPtr<ID3D12DescriptorHeap> m_RtvHeap;
	UINT m_RtvDescriptorSize = 0;

	ComPtr<ID3D12Resource> m_DsBuffer;
	ComPtr<ID3D12DescriptorHeap> m_DsvHeap;
	UINT m_DsvDescriptorSize = 0;

	// Synchronization
	ComPtr<ID3D12Fence> m_Fence;
	UINT64 m_FenceValues[FrameCount] = { 0, };
	UINT64 m_CurrentFenceValue = 1;
	HANDLE m_FenceEvent = nullptr;

	bool m_UseWarpDevice = false;

	UINT m_FrameIndex = 0;

	void CreateDX12Core();
	void GetHardwareAdapter(
		IDXGIFactory1* pFactory,
		IDXGIAdapter1** ppAdapter,
		bool requestHighPerformanceAdapter = false);
	void CreateDescriptorHeaps();
	void CreateFence();
	void CreateCommandObjects();
};