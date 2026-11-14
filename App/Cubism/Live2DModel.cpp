#include "Live2DModel.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "CubismFramework.hpp"
#include "CubismModelSettingJson.hpp"
#include "CubismDefaultParameterId.hpp"
#include "Id/CubismIdManager.hpp"
#include "Math/CubismMatrix44.hpp"
#include "Model/CubismUserModel.hpp"
#include "Motion/CubismMotion.hpp"
#include "Motion/CubismBreathUpdater.hpp"
#include "Motion/CubismEyeBlinkUpdater.hpp"
#include "Motion/CubismPhysicsUpdater.hpp"
#include "Motion/CubismPoseUpdater.hpp"
#include "Effect/CubismBreath.hpp"
#include "Effect/CubismEyeBlink.hpp"
#include "Utils/CubismDebug.hpp"

#include "CubismRenderer_D3D12.hpp"
#include "../../DirectX12/Descriptor.h"

#pragma comment(lib, "windowscodecs.lib")

using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;

namespace
{
using namespace Live2D::Cubism::Framework;
using Live2D::Cubism::Framework::Rendering::CubismRenderer_D3D12;
using Live2D::Cubism::Framework::Rendering::CubismRenderTargetBinding_D3D12;
using Live2D::Cubism::Framework::Rendering::CubismActorLook_D3D12;

// ============================================================================
// 文字列・ファイル
// ============================================================================

// UTF-8 の文字列をパスに変換する（model3.json 内のパスは UTF-8）
fs::path Utf8ToPath(const char* utf8)
{
	return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8)));
}

// ログ出力用にパスを UTF-8 にする
std::string PathToUtf8(const fs::path& path)
{
	const std::u8string s = path.u8string();
	return std::string(s.begin(), s.end());
}

bool ReadFileBytes(const fs::path& path, std::vector<csmByte>& out)
{
	std::ifstream file(path, std::ios::binary | std::ios::ate);
	if (!file)
	{
		CubismLogError("[Live2D] Failed to open: %s", PathToUtf8(path).c_str());
		return false;
	}
	const std::streamsize size = file.tellg();
	out.resize(static_cast<size_t>(size));
	file.seekg(0, std::ios::beg);
	if (size > 0 && !file.read(reinterpret_cast<char*>(out.data()), size))
	{
		CubismLogError("[Live2D] Failed to read: %s", PathToUtf8(path).c_str());
		return false;
	}
	return true;
}

// Cubism Framework から呼ばれるファイル読み込み（パスは UTF-8）
csmByte* LoadFileForCubism(const std::string filePath, csmSizeInt* outSize)
{
	std::vector<csmByte> bytes;
	if (!ReadFileBytes(Utf8ToPath(filePath.c_str()), bytes))
	{
		*outSize = 0;
		return nullptr;
	}
	csmByte* buffer = new csmByte[bytes.size()];
	std::copy(bytes.begin(), bytes.end(), buffer);
	*outSize = static_cast<csmSizeInt>(bytes.size());
	return buffer;
}

void ReleaseBytesForCubism(csmByte* byteData)
{
	delete[] byteData;
}

// Cubism のログを Visual Studio の出力ウィンドウに出す
void LogFunction(const char* message)
{
	const int length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
	if (length <= 0)
	{
		OutputDebugStringA(message);
		return;
	}
	std::wstring wide(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, message, -1, wide.data(), length);
	OutputDebugStringW(wide.c_str());
	OutputDebugStringW(L"\n");
}

// ============================================================================
// メモリアロケータ
// ============================================================================
class Allocator : public ICubismAllocator
{
public:
	void* Allocate(const csmSizeType size) override { return malloc(size); }
	void Deallocate(void* memory) override { free(memory); }
	void* AllocateAligned(const csmSizeType size, const csmUint32 alignment) override { return _aligned_malloc(size, alignment); }
	void DeallocateAligned(void* alignedMemory) override { _aligned_free(alignedMemory); }
};

// ============================================================================
// システム全体の状態
// ============================================================================
struct SystemState
{
	Allocator allocator;
	CubismFramework::Option option = {};
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12CommandQueue> commandQueue;
	ComPtr<IWICImagingFactory> wicFactory;
	bool started = false;
	bool comInitialized = false;
};

SystemState g_system;

// ============================================================================
// GPU 処理の待機
// ============================================================================
void WaitForGpu()
{
	if (!g_system.device || !g_system.commandQueue)
	{
		return;
	}
	ComPtr<ID3D12Fence> fence;
	if (FAILED(g_system.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))))
	{
		return;
	}
	HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	if (event == nullptr)
	{
		return;
	}
	if (SUCCEEDED(g_system.commandQueue->Signal(fence.Get(), 1)) && fence->GetCompletedValue() < 1)
	{
		fence->SetEventOnCompletion(1, event);
		WaitForSingleObject(event, INFINITE);
	}
	CloseHandle(event);
}

// ============================================================================
// テクスチャ
// ============================================================================
struct Image
{
	UINT width = 0;
	UINT height = 0;
	std::vector<UINT8> pixels; // RGBA8（乗算済みアルファ）
};

// WIC で画像を読み込み、乗算済みアルファの RGBA8 に変換する
bool LoadImageFile(const fs::path& path, Image& image)
{
	if (!g_system.wicFactory)
	{
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&g_system.wicFactory))))
		{
			CubismLogError("[Live2D] Failed to create WIC factory.");
			return false;
		}
	}

	ComPtr<IWICBitmapDecoder> decoder;
	if (FAILED(g_system.wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)))
	{
		CubismLogError("[Live2D] Failed to open image: %s", PathToUtf8(path).c_str());
		return false;
	}

	ComPtr<IWICBitmapFrameDecode> frame;
	if (FAILED(decoder->GetFrame(0, &frame)))
	{
		return false;
	}

	ComPtr<IWICFormatConverter> converter;
	if (FAILED(g_system.wicFactory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
	{
		CubismLogError("[Live2D] Failed to convert image: %s", PathToUtf8(path).c_str());
		return false;
	}

	converter->GetSize(&image.width, &image.height);
	const UINT stride = image.width * 4;
	image.pixels.resize(static_cast<size_t>(stride) * image.height);
	if (FAILED(converter->CopyPixels(nullptr, stride, static_cast<UINT>(image.pixels.size()), image.pixels.data())))
	{
		return false;
	}
	return true;
}

// 2x2 平均で1段小さいミップを作る（乗算済みアルファなのでそのまま平均してよい）
Image Downsample(const Image& src)
{
	Image dst;
	dst.width = (std::max)(1u, src.width / 2);
	dst.height = (std::max)(1u, src.height / 2);
	dst.pixels.resize(static_cast<size_t>(dst.width) * dst.height * 4);

	for (UINT y = 0; y < dst.height; ++y)
	{
		const UINT y0 = (std::min)(y * 2, src.height - 1);
		const UINT y1 = (std::min)(y * 2 + 1, src.height - 1);
		for (UINT x = 0; x < dst.width; ++x)
		{
			const UINT x0 = (std::min)(x * 2, src.width - 1);
			const UINT x1 = (std::min)(x * 2 + 1, src.width - 1);
			const UINT8* p00 = &src.pixels[(static_cast<size_t>(y0) * src.width + x0) * 4];
			const UINT8* p01 = &src.pixels[(static_cast<size_t>(y0) * src.width + x1) * 4];
			const UINT8* p10 = &src.pixels[(static_cast<size_t>(y1) * src.width + x0) * 4];
			const UINT8* p11 = &src.pixels[(static_cast<size_t>(y1) * src.width + x1) * 4];
			UINT8* out = &dst.pixels[(static_cast<size_t>(y) * dst.width + x) * 4];
			for (int c = 0; c < 4; ++c)
			{
				out[c] = static_cast<UINT8>((p00[c] + p01[c] + p10[c] + p11[c] + 2) / 4);
			}
		}
	}
	return dst;
}

struct Texture
{
	ComPtr<ID3D12Resource> resource;
	UINT srvIndex = UINT_MAX;
};

/**
 * @brief テクスチャ転送用のコマンドリストをまとめて実行する
 */
class TextureUploader
{
public:
	bool Begin()
	{
		ID3D12Device* device = g_system.device.Get();
		if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&_allocator))) ||
			FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, _allocator.Get(), nullptr, IID_PPV_ARGS(&_commandList))))
		{
			CubismLogError("[Live2D] Failed to create upload command list.");
			return false;
		}
		return true;
	}

	bool CreateTexture(const Image& baseImage, Texture& texture)
	{
		ID3D12Device* device = g_system.device.Get();

		// ミップマップを作る（縮小表示時のちらつき防止）
		std::vector<Image> mips;
		mips.push_back(baseImage);
		while (mips.back().width > 1 || mips.back().height > 1)
		{
			mips.push_back(Downsample(mips.back()));
		}
		const UINT mipCount = static_cast<UINT>(mips.size());

		D3D12_RESOURCE_DESC texDesc = {};
		texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		texDesc.Width = baseImage.width;
		texDesc.Height = baseImage.height;
		texDesc.DepthOrArraySize = 1;
		texDesc.MipLevels = static_cast<UINT16>(mipCount);
		texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		texDesc.SampleDesc.Count = 1;
		texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

		D3D12_HEAP_PROPERTIES defaultHeap = {};
		defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
		if (FAILED(device->CreateCommittedResource(&defaultHeap, D3D12_HEAP_FLAG_NONE, &texDesc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture.resource))))
		{
			CubismLogError("[Live2D] Failed to create texture.");
			return false;
		}

		std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints(mipCount);
		std::vector<UINT> rowCounts(mipCount);
		std::vector<UINT64> rowSizes(mipCount);
		UINT64 totalSize = 0;
		device->GetCopyableFootprints(&texDesc, 0, mipCount, 0, footprints.data(), rowCounts.data(), rowSizes.data(), &totalSize);

		D3D12_HEAP_PROPERTIES uploadHeap = {};
		uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width = totalSize;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

		ComPtr<ID3D12Resource> staging;
		if (FAILED(device->CreateCommittedResource(&uploadHeap, D3D12_HEAP_FLAG_NONE, &bufferDesc,
			D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&staging))))
		{
			CubismLogError("[Live2D] Failed to create texture upload buffer.");
			return false;
		}

		UINT8* mapped = nullptr;
		D3D12_RANGE readRange = { 0, 0 };
		if (FAILED(staging->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
		{
			return false;
		}
		for (UINT mip = 0; mip < mipCount; ++mip)
		{
			const Image& image = mips[mip];
			const size_t srcPitch = static_cast<size_t>(image.width) * 4;
			for (UINT row = 0; row < rowCounts[mip]; ++row)
			{
				std::memcpy(mapped + footprints[mip].Offset + static_cast<size_t>(row) * footprints[mip].Footprint.RowPitch,
					image.pixels.data() + row * srcPitch, srcPitch);
			}
		}
		staging->Unmap(0, nullptr);

		for (UINT mip = 0; mip < mipCount; ++mip)
		{
			D3D12_TEXTURE_COPY_LOCATION dst = {};
			dst.pResource = texture.resource.Get();
			dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
			dst.SubresourceIndex = mip;

			D3D12_TEXTURE_COPY_LOCATION src = {};
			src.pResource = staging.Get();
			src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
			src.PlacedFootprint = footprints[mip];

			_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
		}

		D3D12_RESOURCE_BARRIER barrier = {};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = texture.resource.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
		_commandList->ResourceBarrier(1, &barrier);

		_staging.push_back(staging);

		// SRV
		texture.srvIndex = Descriptor::Singleton()->Allocate();
		if (texture.srvIndex == UINT_MAX)
		{
			CubismLogError("[Live2D] Descriptor heap is full.");
			return false;
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = texDesc.Format;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture2D.MipLevels = mipCount;
		device->CreateShaderResourceView(texture.resource.Get(), &srvDesc, Descriptor::Singleton()->GetCpuHandle(texture.srvIndex));
		return true;
	}

	// 積んだコピーを実行し、完了まで待つ
	bool ExecuteAndWait()
	{
		if (FAILED(_commandList->Close()))
		{
			return false;
		}
		ID3D12CommandList* lists[] = { _commandList.Get() };
		g_system.commandQueue->ExecuteCommandLists(1, lists);
		WaitForGpu();
		_staging.clear();
		return true;
	}

private:
	ComPtr<ID3D12CommandAllocator> _allocator;
	ComPtr<ID3D12GraphicsCommandList> _commandList;
	std::vector<ComPtr<ID3D12Resource>> _staging;
};

// ============================================================================
// モデル
// ============================================================================
class Live2DModelImpl final : public Live2DModel, public CubismUserModel
{
public:
	Live2DModelImpl() = default;

	~Live2DModelImpl() override
	{
		// GPU が使い終わるまで待ってからリソースを解放する
		WaitForGpu();

		if (_motionManager != nullptr)
		{
			_motionManager->StopAllMotions();
		}
		for (auto& group : _motions)
		{
			for (ACubismMotion* motion : group.second)
			{
				ACubismMotion::Delete(motion);
			}
		}
		_motions.clear();
		for (auto& entry : _externalMotions)
		{
			ACubismMotion::Delete(entry.second);
		}
		_externalMotions.clear();

		for (Texture& texture : _textures)
		{
			if (texture.srvIndex != UINT_MAX)
			{
				Descriptor::Singleton()->Free(texture.srvIndex);
			}
		}
		_textures.clear();

		if (_setting != nullptr)
		{
			CSM_DELETE(_setting);
			_setting = nullptr;
		}
		// モデル本体・物理演算・レンダラーなどは CubismUserModel のデストラクタで解放される
	}

	bool Load(const fs::path& path) override
	{
		if (!Live2DSystem::IsStarted())
		{
			CubismLogError("[Live2D] Live2D::StartUp() has not been called.");
			return false;
		}
		if (_model != nullptr)
		{
			CubismLogError("[Live2D] A model is already loaded. Create another Live2D instance to load a new model.");
			return false;
		}

		// ---- model3.json の場所を決める ----
		fs::path jsonPath = path;
		std::error_code ec;
		if (fs::is_directory(path, ec))
		{
			jsonPath.clear();
			for (const auto& entry : fs::directory_iterator(path, ec))
			{
				const std::wstring name = entry.path().filename().wstring();
				const std::wstring suffix = L".model3.json";
				if (name.size() > suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
				{
					jsonPath = entry.path();
					break;
				}
			}
			if (jsonPath.empty())
			{
				CubismLogError("[Live2D] No .model3.json found in: %s", PathToUtf8(path).c_str());
				return false;
			}
		}
		_homeDir = jsonPath.parent_path();
		CubismLogInfo("[Live2D] Loading: %s", PathToUtf8(jsonPath).c_str());

		std::vector<csmByte> bytes;
		if (!ReadFileBytes(jsonPath, bytes))
		{
			return false;
		}
		_setting = CSM_NEW CubismModelSettingJson(bytes.data(), static_cast<csmSizeInt>(bytes.size()));

		// ---- moc3 ----
		if (std::strlen(_setting->GetModelFileName()) == 0 || !ReadFileBytes(_homeDir / Utf8ToPath(_setting->GetModelFileName()), bytes))
		{
			CubismLogError("[Live2D] Failed to read moc3.");
			return false;
		}
		LoadModel(bytes.data(), static_cast<csmSizeInt>(bytes.size()), true);
		if (_model == nullptr)
		{
			CubismLogError("[Live2D] Failed to create model. The moc3 version may be newer than the SDK.");
			return false;
		}
		CubismLogInfo("[Live2D] Drawables: %d, Parameters: %d", _model->GetDrawableCount(), _model->GetParameterCount());

		// ---- 物理演算 ----
		if (std::strlen(_setting->GetPhysicsFileName()) > 0 && ReadFileBytes(_homeDir / Utf8ToPath(_setting->GetPhysicsFileName()), bytes))
		{
			LoadPhysics(bytes.data(), static_cast<csmSizeInt>(bytes.size()));
			if (_physics != nullptr)
			{
				_updateScheduler.AddUpdatableList(CSM_NEW CubismPhysicsUpdater(*_physics));
			}
		}

		// ---- ポーズ ----
		if (std::strlen(_setting->GetPoseFileName()) > 0 && ReadFileBytes(_homeDir / Utf8ToPath(_setting->GetPoseFileName()), bytes))
		{
			LoadPose(bytes.data(), static_cast<csmSizeInt>(bytes.size()));
			if (_pose != nullptr)
			{
				_updateScheduler.AddUpdatableList(CSM_NEW CubismPoseUpdater(*_pose));
			}
		}

		// ---- 目パチ ----
		if (_setting->GetEyeBlinkParameterCount() > 0)
		{
			_eyeBlink = CubismEyeBlink::Create(_setting);
			_updateScheduler.AddUpdatableList(CSM_NEW CubismEyeBlinkUpdater(_motionUpdated, *_eyeBlink));
		}
		for (csmInt32 i = 0; i < _setting->GetEyeBlinkParameterCount(); ++i)
		{
			_eyeBlinkIds.PushBack(_setting->GetEyeBlinkParameterId(i));
		}
		for (csmInt32 i = 0; i < _setting->GetLipSyncParameterCount(); ++i)
		{
			_lipSyncIds.PushBack(_setting->GetLipSyncParameterId(i));
		}

		// ---- 呼吸 ----
		{
			CubismIdManager* ids = CubismFramework::GetIdManager();
			csmVector<CubismBreath::BreathParameterData> breathParameters;
			breathParameters.PushBack(CubismBreath::BreathParameterData(ids->GetId(DefaultParameterId::ParamAngleX), 0.0f, 15.0f, 6.5345f, 0.5f));
			breathParameters.PushBack(CubismBreath::BreathParameterData(ids->GetId(DefaultParameterId::ParamAngleY), 0.0f, 8.0f, 3.5345f, 0.5f));
			breathParameters.PushBack(CubismBreath::BreathParameterData(ids->GetId(DefaultParameterId::ParamAngleZ), 0.0f, 10.0f, 5.5345f, 0.5f));
			breathParameters.PushBack(CubismBreath::BreathParameterData(ids->GetId(DefaultParameterId::ParamBodyAngleX), 0.0f, 4.0f, 15.5345f, 0.5f));
			breathParameters.PushBack(CubismBreath::BreathParameterData(ids->GetId(DefaultParameterId::ParamBreath), 0.5f, 0.5f, 3.2345f, 1.0f));
			_breath = CubismBreath::Create();
			_breath->SetParameters(breathParameters);
			_updateScheduler.AddUpdatableList(CSM_NEW CubismBreathUpdater(*_breath));
		}

		// ---- レイアウト ----
		{
			csmMap<csmString, csmFloat32> layout;
			_setting->GetLayoutMap(layout);
			_modelMatrix->SetupFromLayout(layout);
		}

		_model->SaveParameters();

		// ---- 3D 表示用：読み込み時のポーズで全パーツを囲む範囲 ----
		_model->Update();
		ComputeBounds();

		// ---- model3.json に登録されたモーション ----
		for (csmInt32 g = 0; g < _setting->GetMotionGroupCount(); ++g)
		{
			const csmChar* group = _setting->GetMotionGroupName(g);
			for (csmInt32 i = 0; i < _setting->GetMotionCount(group); ++i)
			{
				ACubismMotion* motion = LoadMotionFile(_homeDir / Utf8ToPath(_setting->GetMotionFileName(group, i)));
				if (motion == nullptr)
				{
					continue;
				}
				const csmFloat32 fadeIn = _setting->GetMotionFadeInTimeValue(group, i);
				const csmFloat32 fadeOut = _setting->GetMotionFadeOutTimeValue(group, i);
				if (fadeIn >= 0.0f)
				{
					motion->SetFadeInTime(fadeIn);
				}
				if (fadeOut >= 0.0f)
				{
					motion->SetFadeOutTime(fadeOut);
				}
				_motions[group].push_back(motion);
			}
		}

		// ---- レンダラー ----
		CreateRenderer(static_cast<csmUint32>(_model->GetCanvasWidthPixel()), static_cast<csmUint32>(_model->GetCanvasHeightPixel()));
		CubismRenderer_D3D12* renderer = GetRenderer<CubismRenderer_D3D12>();
		if (renderer == nullptr)
		{
			CubismLogError("[Live2D] Failed to create renderer.");
			return false;
		}
		renderer->IsPremultipliedAlpha(true); // テクスチャは乗算済みアルファで読み込む

		// ---- テクスチャ ----
		if (!LoadTextures(*renderer))
		{
			return false;
		}

		_loaded = true;
		CubismLogInfo("[Live2D] Load completed.");
		return true;
	}

	bool PlayMotion(const fs::path& path, bool loop) override
	{
		if (_model == nullptr)
		{
			return false;
		}

		std::error_code ec;
		const std::wstring key = fs::absolute(path, ec).lexically_normal().wstring();
		ACubismMotion* motion = nullptr;
		auto found = _externalMotions.find(key);
		if (found != _externalMotions.end())
		{
			motion = found->second;
		}
		else
		{
			motion = LoadMotionFile(path);
			if (motion == nullptr)
			{
				return false;
			}
			_externalMotions[key] = motion;
		}

		motion->SetLoop(loop);
		_motionManager->StartMotionPriority(motion, false, PriorityForce);
		return true;
	}

	void StopMotion() override
	{
		if (_motionManager) { _motionManager->StopAllMotions(); }
	}

	void Update(float deltaSeconds) override
	{
		if (!_loaded)
		{
			return;
		}

		_model->LoadParameters();

		// モーションが無ければ待機モーションを再生する
		if (_motionManager->IsFinished())
		{
			StartIdleMotion();
		}
		_motionUpdated = _motionManager->UpdateMotion(_model, deltaSeconds);

		_model->SaveParameters();
		_opacity = _model->GetModelOpacity();

		// フェイストラッキング（SaveParameters の後なので次のフレームに持ち越さない）
		// 物理演算が頭の動きに追従するよう、先に全て反映する
		ApplyTracking(false);

		// 目パチ・呼吸・物理演算・ポーズ
		_updateScheduler.OnLateUpdate(_model, deltaSeconds);

		// 自動の目パチで上書きされた目・口・眉を、トラッキングの値に戻す
		ApplyTracking(true);

		_model->Update();
	}

	void SetTrackingParameters(const std::vector<std::pair<std::string, float>>& values) override
	{
		_tracking.clear();
		CubismIdManager* ids = CubismFramework::GetIdManager();
		for (const auto& [name, value] : values)
		{
			auto it = _trackingIds.find(name);
			if (it == _trackingIds.end())
			{
				it = _trackingIds.emplace(name, ids->GetId(name.c_str())).first;
			}
			// 顔のパーツ（目・口・眉）は目パチの後にもう一度かける
			const bool face = name.find("Eye") != std::string::npos || name.find("Mouth") != std::string::npos ||
				name.find("Brow") != std::string::npos;
			_tracking.push_back({ it->second, value, face });
		}
	}

	void Draw(ID3D12GraphicsCommandList* commandList, const Live2DRenderTarget& target) override
	{
		if (!_loaded || commandList == nullptr)
		{
			return;
		}
		CubismRenderer_D3D12* renderer = GetRenderer<CubismRenderer_D3D12>();
		if (renderer == nullptr)
		{
			return;
		}

		const float width = target.viewport.Width;
		const float height = target.viewport.Height;
		if (width <= 0.0f || height <= 0.0f)
		{
			return;
		}

		CubismMatrix44 projection;
		if (_use3D)
		{
			// 3D 表示：ワールド × ビュー × プロジェクションをそのまま使う
			projection.SetMatrix(_worldViewProjection);
		}
		else
		{
			// 画面のアスペクト比に合わせる
			if (_model->GetCanvasWidth() > 1.0f && width < height)
			{
				_modelMatrix->SetWidth(2.0f);
				projection.Scale(1.0f, width / height);
			}
			else
			{
				projection.Scale(height / width, 1.0f);
			}

			// 表示位置・拡大率
			csmFloat32 view[16] =
			{
				_scale, 0.0f,   0.0f, 0.0f,
				0.0f,   _scale, 0.0f, 0.0f,
				0.0f,   0.0f,   1.0f, 0.0f,
				_posX,  _posY,  0.0f, 1.0f,
			};
			CubismMatrix44 viewMatrix;
			viewMatrix.SetMatrix(view);
			projection.MultiplyByMatrix(&viewMatrix);
			projection.MultiplyByMatrix(_modelMatrix);
		}
		renderer->SetMvpMatrix(&projection);

		// モデルを囲む範囲を画面（NDC）に変換する（擬似ライトの基準と、ドラッグの当たり判定に使う）
		{
			const csmFloat32* m = projection.GetArray();
			const float corners[4][2] =
			{
				{ _bounds[0], _bounds[2] }, { _bounds[1], _bounds[2] },
				{ _bounds[0], _bounds[3] }, { _bounds[1], _bounds[3] },
			};
			float minX = FLT_MAX, maxX = -FLT_MAX, minY = FLT_MAX, maxY = -FLT_MAX;
			for (const auto& c : corners)
			{
				const float x = m[0] * c[0] + m[4] * c[1] + m[12];
				const float y = m[1] * c[0] + m[5] * c[1] + m[13];
				const float w = m[3] * c[0] + m[7] * c[1] + m[15];
				const float invW = (std::fabs(w) > 1e-6f) ? 1.0f / w : 1.0f;
				minX = (std::min)(minX, x * invW);
				maxX = (std::max)(maxX, x * invW);
				minY = (std::min)(minY, y * invW);
				maxY = (std::max)(maxY, y * invW);
			}
			_screenBounds[0] = minX;
			_screenBounds[1] = maxX;
			_screenBounds[2] = minY;
			_screenBounds[3] = maxY;
			_hasScreenBounds = true;
		}

		CubismActorLook_D3D12 look;
		look.tint[0] = _look.tint[0];
		look.tint[1] = _look.tint[1];
		look.tint[2] = _look.tint[2];
		look.tint[3] = _look.saturation;
		look.light[0] = _look.lightDirection[0];
		look.light[1] = _look.lightDirection[1];
		look.light[2] = _look.lightStrength;
		look.light[3] = _look.opacity;
		look.rim[0] = _look.rimColor[0];
		look.rim[1] = _look.rimColor[1];
		look.rim[2] = _look.rimColor[2];
		look.rim[3] = _look.rimStrength;
		look.extra[0] = _look.rimWidth;
		look.bounds[0] = (_screenBounds[0] + _screenBounds[1]) * 0.5f;
		look.bounds[1] = (_screenBounds[2] + _screenBounds[3]) * 0.5f;
		look.bounds[2] = (_screenBounds[1] - _screenBounds[0]) * 0.5f;
		look.bounds[3] = (_screenBounds[3] - _screenBounds[2]) * 0.5f;
		renderer->SetActorLook(look);

		CubismRenderTargetBinding_D3D12 binding;
		binding.rtv = target.rtv;
		binding.dsv = target.dsv;
		binding.hasDsv = target.hasDsv;
		binding.viewport = target.viewport;
		binding.scissor = target.scissor;
		renderer->SetDrawContext(commandList, binding);
		renderer->DrawModel();
	}

	void SetPosition(float x, float y) override
	{
		_posX = x;
		_posY = y;
	}

	void SetScale(float scale) override
	{
		_scale = scale;
	}

	void SetWorldViewProjection(const float* matrix) override
	{
		_use3D = (matrix != nullptr);
		if (_use3D)
		{
			std::memcpy(_worldViewProjection, matrix, sizeof(_worldViewProjection));
		}
	}

	void GetModelBounds(float& left, float& right, float& bottom, float& top) const override
	{
		left = _bounds[0];
		right = _bounds[1];
		bottom = _bounds[2];
		top = _bounds[3];
	}

	void SetActorLook(const Live2DActorLook& look) override
	{
		_look = look;
	}

	bool GetScreenBounds(float& left, float& right, float& bottom, float& top) const override
	{
		left = _screenBounds[0];
		right = _screenBounds[1];
		bottom = _screenBounds[2];
		top = _screenBounds[3];
		return _hasScreenBounds;
	}

	void GetAverageColor(float rgb[3]) const override
	{
		rgb[0] = _averageColor[0];
		rgb[1] = _averageColor[1];
		rgb[2] = _averageColor[2];
	}

	bool IsLoaded() const override
	{
		return _loaded;
	}

private:
	static constexpr csmInt32 PriorityIdle = 1;
	static constexpr csmInt32 PriorityForce = 3;

	ACubismMotion* LoadMotionFile(const fs::path& path)
	{
		std::vector<csmByte> bytes;
		if (!ReadFileBytes(path, bytes))
		{
			return nullptr;
		}
		CubismMotion* motion = CubismMotion::Create(bytes.data(), static_cast<csmSizeInt>(bytes.size()));
		if (motion == nullptr)
		{
			CubismLogError("[Live2D] Failed to load motion: %s", PathToUtf8(path).c_str());
			return nullptr;
		}
		motion->SetEffectIds(_eyeBlinkIds, _lipSyncIds);
		return motion;
	}

	void ApplyTracking(bool faceOnly)
	{
		for (const TrackingValue& value : _tracking)
		{
			if (!faceOnly || value.face)
			{
				const csmInt32 index = _model->GetParameterIndex(value.id);
				if (index >= 0)
				{
					_model->SetParameterValue(index, value.value);
				}
			}
		}
	}

	void ComputeBounds()
	{
		float left = FLT_MAX, right = -FLT_MAX, bottom = FLT_MAX, top = -FLT_MAX;
		const csmInt32 drawableCount = _model->GetDrawableCount();
		for (csmInt32 d = 0; d < drawableCount; ++d)
		{
			if (_model->GetDrawableOpacity(d) <= 0.0f)
			{
				continue;  // 非表示のパーツ（差分など）は含めない
			}
			const csmInt32 count = _model->GetDrawableVertexCount(d);
			const csmFloat32* vertices = _model->GetDrawableVertices(d);
			for (csmInt32 i = 0; i < count; ++i)
			{
				left = (std::min)(left, vertices[i * 2 + 0]);
				right = (std::max)(right, vertices[i * 2 + 0]);
				bottom = (std::min)(bottom, vertices[i * 2 + 1]);
				top = (std::max)(top, vertices[i * 2 + 1]);
			}
		}
		if (left < right && bottom < top)
		{
			_bounds[0] = left;
			_bounds[1] = right;
			_bounds[2] = bottom;
			_bounds[3] = top;
		}
	}

	void StartIdleMotion()
	{
		auto found = _motions.find("Idle");
		if (found == _motions.end() || found->second.empty())
		{
			return;
		}
		std::uniform_int_distribution<size_t> dist(0, found->second.size() - 1);
		_motionManager->StartMotionPriority(found->second[dist(_random)], false, PriorityIdle);
	}

	bool LoadTextures(CubismRenderer_D3D12& renderer)
	{
		const csmInt32 textureCount = _setting->GetTextureCount();
		if (textureCount <= 0)
		{
			return true;
		}

		TextureUploader uploader;
		if (!uploader.Begin())
		{
			return false;
		}

		_textures.resize(textureCount);
		double sum[4] = {};  // 平均色（乗算済みアルファのまま足し、最後に a で割る）
		for (csmInt32 i = 0; i < textureCount; ++i)
		{
			const csmChar* fileName = _setting->GetTextureFileName(i);
			if (std::strlen(fileName) == 0)
			{
				continue;
			}

			Image image;
			if (!LoadImageFile(_homeDir / Utf8ToPath(fileName), image))
			{
				continue;
			}

			// 4 ピクセルおきに見れば十分
			for (size_t p = 0; p + 3 < image.pixels.size(); p += 16)
			{
				sum[0] += image.pixels[p + 0];
				sum[1] += image.pixels[p + 1];
				sum[2] += image.pixels[p + 2];
				sum[3] += image.pixels[p + 3];
			}
			if (sum[3] > 0.0)
			{
				for (int c = 0; c < 3; ++c)
				{
					_averageColor[c] = static_cast<float>(sum[c] / sum[3]);
				}
			}

			if (!uploader.CreateTexture(image, _textures[i]))
			{
				continue;
			}
			renderer.BindTexture(i, Descriptor::Singleton()->GetGpuHandle(_textures[i].srvIndex));
		}

		return uploader.ExecuteAndWait();
	}

	fs::path _homeDir;
	ICubismModelSetting* _setting = nullptr;
	csmVector<CubismIdHandle> _eyeBlinkIds;
	csmVector<CubismIdHandle> _lipSyncIds;
	std::map<std::string, std::vector<ACubismMotion*>> _motions;
	std::map<std::wstring, ACubismMotion*> _externalMotions;
	std::vector<Texture> _textures;
	std::mt19937 _random{ std::random_device{}() };

	csmBool _motionUpdated = false;
	bool _loaded = false;
	float _posX = 0.0f;
	float _posY = 0.0f;
	float _scale = 1.0f;

	// 3D 表示
	bool _use3D = false;
	csmFloat32 _worldViewProjection[16] = {};
	float _bounds[4] = { -1.0f, 1.0f, -1.0f, 1.0f };  // left, right, bottom, top（モデルの座標）

	// ライティング
	Live2DActorLook _look;
	float _screenBounds[4] = { -1.0f, 1.0f, -1.0f, 1.0f };  // 最後に描いたときの範囲（NDC）
	bool _hasScreenBounds = false;
	float _averageColor[3] = { 0.5f, 0.5f, 0.5f };

	// フェイストラッキング
	struct TrackingValue
	{
		CubismIdHandle id;
		float value;
		bool face;
	};
	std::vector<TrackingValue> _tracking;
	std::map<std::string, CubismIdHandle> _trackingIds;
};

} // namespace

// ============================================================================
// Live2DSystem
// ============================================================================
bool Live2DSystem::StartUp(const Desc& desc)
{
	if (g_system.started)
	{
		return true;
	}
	if (desc.device == nullptr || desc.commandQueue == nullptr)
	{
		return false;
	}

	// WIC を使うため COM を初期化する（既に初期化済みでも問題ない）
	const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	g_system.comInitialized = SUCCEEDED(hr);

	g_system.device = desc.device;
	g_system.commandQueue = desc.commandQueue;

	g_system.option.LogFunction = LogFunction;
#ifdef _DEBUG
	g_system.option.LoggingLevel = CubismFramework::Option::LogLevel_Verbose;
#else
	g_system.option.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
#endif
	g_system.option.LoadFileFunction = LoadFileForCubism;
	g_system.option.ReleaseBytesFunction = ReleaseBytesForCubism;

	if (!CubismFramework::StartUp(&g_system.allocator, &g_system.option))
	{
		return false;
	}
	CubismFramework::Initialize();

	if (!CubismRenderer_D3D12::InitializeShared(desc.device, desc.renderTargetFormat, desc.depthStencilFormat, desc.frameCount))
	{
		CubismFramework::Dispose();
		return false;
	}

	g_system.started = true;
	return true;
}

void Live2DSystem::Shutdown()
{
	if (!g_system.started)
	{
		return;
	}

	WaitForGpu();
	CubismFramework::Dispose();
	CubismRenderer_D3D12::ReleaseShared();

	g_system.wicFactory.Reset();
	g_system.commandQueue.Reset();
	g_system.device.Reset();

	if (g_system.comInitialized)
	{
		CoUninitialize();
		g_system.comInitialized = false;
	}
	g_system.started = false;
}

bool Live2DSystem::IsStarted()
{
	return g_system.started;
}

std::unique_ptr<Live2DModel> Live2DModel::Create()
{
	return std::make_unique<Live2DModelImpl>();
}
