#include "ImGuiLayer.h"

#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

// imgui_impl_win32.h では意図的にコメントアウトされているため自前で宣言する
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

ImGuiLayer::~ImGuiLayer()
{
	Shutdown();
}

HRESULT ImGuiLayer::Initialize(const ImGuiLayerDesc& desc)
{
	if (_initialized) return S_OK;
	if (!desc.hwnd || !desc.device || !desc.commandQueue) return E_INVALIDARG;

	// ImGui 専用の SRV ヒープを作成
	HRESULT hr = CreateSrvHeap(desc.device, desc.srvCapacity);
	if (FAILED(hr)) return hr;

	// コンテキストの作成
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	ImGui::StyleColorsDark();

	// Win32 バックエンド
	if (!ImGui_ImplWin32_Init(desc.hwnd))
	{
		ImGui::DestroyContext();
		_srvHeap.Reset();
		return E_FAIL;
	}

	// DX12 バックエンド
	ImGui_ImplDX12_InitInfo initInfo;
	initInfo.Device = desc.device;
	initInfo.CommandQueue = desc.commandQueue;
	initInfo.NumFramesInFlight = static_cast<int>(desc.bufferCount);
	initInfo.RTVFormat = desc.rtvFormat;
	initInfo.DSVFormat = DXGI_FORMAT_UNKNOWN;
	initInfo.SrvDescriptorHeap = _srvHeap.Get();
	initInfo.SrvDescriptorAllocFn = &ImGuiLayer::SrvAlloc;
	initInfo.SrvDescriptorFreeFn = &ImGuiLayer::SrvFree;
	initInfo.UserData = this;
	if (!ImGui_ImplDX12_Init(&initInfo))
	{
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		_srvHeap.Reset();
		return E_FAIL;
	}

	_initialized = true;
	return S_OK;
}

void ImGuiLayer::Shutdown()
{
	if (!_initialized) return;

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();

	_srvFreeList.clear();
	_srvHeap.Reset();
	_initialized = false;
}

void ImGuiLayer::BeginFrame()
{
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
}

void ImGuiLayer::EndFrame(ID3D12GraphicsCommandList* commandList)
{
	ImGui::Render();

	ID3D12DescriptorHeap* heaps[] = { _srvHeap.Get() };
	commandList->SetDescriptorHeaps(1, heaps);
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
}

bool ImGuiLayer::WndProcHandler(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& result)
{
	result = ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam);
	return result != 0;
}

HRESULT ImGuiLayer::CreateSrvHeap(ID3D12Device* device, UINT capacity)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.NumDescriptors = capacity;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

	HRESULT hr = device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&_srvHeap));
	if (FAILED(hr)) return hr;

	_srvCpuStart = _srvHeap->GetCPUDescriptorHandleForHeapStart();
	_srvGpuStart = _srvHeap->GetGPUDescriptorHandleForHeapStart();
	_srvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	// 後ろから詰めて、back() から若い番号が取れるようにする
	_srvFreeList.clear();
	_srvFreeList.reserve(capacity);
	for (UINT i = capacity; i > 0; --i)
	{
		_srvFreeList.push_back(i - 1);
	}
	return S_OK;
}

void ImGuiLayer::SrvAlloc(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE* outCpu, D3D12_GPU_DESCRIPTOR_HANDLE* outGpu)
{
	ImGuiLayer* self = static_cast<ImGuiLayer*>(info->UserData);
	IM_ASSERT(!self->_srvFreeList.empty() && "ImGui SRV heap is full");

	UINT index = self->_srvFreeList.back();
	self->_srvFreeList.pop_back();

	outCpu->ptr = self->_srvCpuStart.ptr + static_cast<SIZE_T>(index) * self->_srvDescriptorSize;
	outGpu->ptr = self->_srvGpuStart.ptr + static_cast<UINT64>(index) * self->_srvDescriptorSize;
}

void ImGuiLayer::SrvFree(ImGui_ImplDX12_InitInfo* info, D3D12_CPU_DESCRIPTOR_HANDLE cpu, D3D12_GPU_DESCRIPTOR_HANDLE)
{
	ImGuiLayer* self = static_cast<ImGuiLayer*>(info->UserData);
	UINT index = static_cast<UINT>((cpu.ptr - self->_srvCpuStart.ptr) / self->_srvDescriptorSize);
	self->_srvFreeList.push_back(index);
}
