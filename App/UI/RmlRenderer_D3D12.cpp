#include "RmlRenderer_D3D12.h"

#include <RmlUi/Core/Log.h>

#include <Windows.h>
#include <d3dcompiler.h>
#include <wincodec.h>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include "../../DirectX12/Descriptor.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr UINT64 InitialFrameBufferSize = 1024 * 1024;
constexpr DXGI_FORMAT TextureFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

const char ShaderSource[] = R"(
cbuffer RmlConstants : register(b0)
{
	float4x4 transform;
	float2 translation;
	float2 padding;
};

Texture2D uiTexture : register(t0);
SamplerState linearClamp : register(s0);

struct VSInput
{
	float2 position : POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float2 uv : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput o;
	o.position = mul(transform, float4(input.position + translation, 0.0, 1.0));
	o.color = input.color;
	o.uv = input.uv;
	return o;
}

float4 PSColor(VSOutput input) : SV_TARGET
{
	return input.color;
}

float4 PSTexture(VSOutput input) : SV_TARGET
{
	return input.color * uiTexture.Sample(linearClamp, input.uv);
}
)";

struct Constants
{
	float transform[16];
	float translation[2];
	float padding[2];
};

enum RootParameter
{
	Root_Constants,
	Root_Texture,
	Root_Count
};

ComPtr<ID3DBlob> CompileHlsl(const char* entry, const char* target)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "RmlRenderer", nullptr, nullptr, entry, target, flags, 0, &blob, &error)))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "[RmlRenderer] Shader compile failed: %s",
			error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown");
		return nullptr;
	}
	return blob;
}

D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	return barrier;
}

UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

std::filesystem::path Utf8ToPath(const Rml::String& text)
{
	return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}
} // namespace

RmlRenderer_D3D12::~RmlRenderer_D3D12()
{
	Release();
}

bool RmlRenderer_D3D12::Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, UINT frameCount)
{
	_device = device;

	// ---- ルートシグネチャ：b0 = 定数（行列・平行移動）、t0 = テクスチャ ----
	D3D12_DESCRIPTOR_RANGE range = {};
	range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	range.NumDescriptors = 1;

	D3D12_ROOT_PARAMETER params[Root_Count] = {};
	params[Root_Constants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[Root_Constants].Constants.Num32BitValues = sizeof(Constants) / 4;
	params[Root_Constants].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	params[Root_Texture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[Root_Texture].DescriptorTable.NumDescriptorRanges = 1;
	params[Root_Texture].DescriptorTable.pDescriptorRanges = &range;
	params[Root_Texture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC sampler = {};
	sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
	sampler.MaxLOD = D3D12_FLOAT32_MAX;
	sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = Root_Count;
	rootDesc.pParameters = params;
	rootDesc.NumStaticSamplers = 1;
	rootDesc.pStaticSamplers = &sampler;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &error)) ||
		FAILED(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature))))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "[RmlRenderer] Failed to create root signature.");
		return false;
	}

	// ---- PSO（色だけ / テクスチャ付き）----
	ComPtr<ID3DBlob> vs = CompileHlsl("VSMain", "vs_5_0");
	ComPtr<ID3DBlob> psColor = CompileHlsl("PSColor", "ps_5_0");
	ComPtr<ID3DBlob> psTexture = CompileHlsl("PSTexture", "ps_5_0");
	if (!vs || !psColor || !psTexture)
	{
		return false;
	}

	const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	static_assert(sizeof(Rml::Vertex) == 20, "Rml::Vertex layout");

	auto createPso = [&](ID3DBlob* ps, ComPtr<ID3D12PipelineState>& out) -> bool
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = _rootSignature.Get();
		desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
		blend.BlendEnable = TRUE;
		blend.SrcBlend = D3D12_BLEND_ONE;  // 乗算済みアルファ
		blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		blend.BlendOp = D3D12_BLEND_OP_ADD;
		blend.SrcBlendAlpha = D3D12_BLEND_ONE;
		blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
		blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
		blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.InputLayout = { inputLayout, _countof(inputLayout) };
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = renderTargetFormat;
		desc.SampleDesc.Count = 1;
		return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&out)));
	};
	if (!createPso(psColor.Get(), _colorPso) || !createPso(psTexture.Get(), _texturePso))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "[RmlRenderer] Failed to create pipeline state.");
		return false;
	}

	_slots.resize(frameCount + 1);
	for (FrameSlot& slot : _slots)
	{
		if (!CreateFrameBuffer(slot, InitialFrameBufferSize))
		{
			return false;
		}
	}
	return true;
}

void RmlRenderer_D3D12::Release()
{
	for (FrameSlot& slot : _slots)
	{
		for (Texture* texture : slot.retiredTextures)
		{
			if (texture->srvIndex != UINT_MAX)
			{
				Descriptor::Singleton()->Free(texture->srvIndex);
			}
			delete texture;
		}
	}
	_slots.clear();
	_pendingUploads.clear();
	_colorPso.Reset();
	_texturePso.Reset();
	_rootSignature.Reset();
	_device = nullptr;
}

bool RmlRenderer_D3D12::CreateFrameBuffer(FrameSlot& slot, UINT64 size)
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = size;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	ComPtr<ID3D12Resource> buffer;
	if (FAILED(_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))))
	{
		Rml::Log::Message(Rml::Log::LT_ERROR, "[RmlRenderer] Failed to create upload buffer.");
		return false;
	}
	void* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	if (FAILED(buffer->Map(0, &noRead, &mapped)))
	{
		return false;
	}
	if (slot.buffer)
	{
		slot.retired.push_back(slot.buffer);  // 今のフレームで使っている途中なので、すぐには消さない
	}
	slot.buffer = buffer;
	slot.cpu = static_cast<UINT8*>(mapped);
	slot.gpu = buffer->GetGPUVirtualAddress();
	slot.size = size;
	slot.offset = 0;
	return true;
}

bool RmlRenderer_D3D12::AllocateFrameMemory(UINT64 size, UINT64 alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu)
{
	FrameSlot& slot = _slots[_currentSlot];
	UINT64 offset = AlignUp(slot.offset, alignment);
	if (offset + size > slot.size)
	{
		if (!CreateFrameBuffer(slot, (std::max)(slot.size * 2, AlignUp(size, 65536))))
		{
			return false;
		}
		offset = 0;
	}
	*cpu = slot.cpu + offset;
	*gpu = slot.gpu + offset;
	slot.offset = offset + size;
	return true;
}

void RmlRenderer_D3D12::BeginFrame(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT width, UINT height)
{
	// 次のスロットへ（このスロットを最後に使ったフレームは GPU で終わっている）
	_currentSlot = (_currentSlot + 1) % static_cast<UINT>(_slots.size());
	FrameSlot& slot = _slots[_currentSlot];
	slot.offset = 0;
	slot.retired.clear();
	for (Texture* texture : slot.retiredTextures)
	{
		if (texture->srvIndex != UINT_MAX)
		{
			Descriptor::Singleton()->Free(texture->srvIndex);
		}
		delete texture;
	}
	slot.retiredTextures.clear();

	_commandList = commandList;
	_width = width;
	_height = height;
	_projection = Rml::Matrix4f::ProjectOrtho(0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, -10000.0f, 10000.0f);
	_transform = _projection;
	_scissorEnabled = false;

	// 描画の外で作られたテクスチャを転送する
	for (Texture* texture : _pendingUploads)
	{
		RecordUpload(*texture);
	}
	_pendingUploads.clear();

	commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
	commandList->RSSetViewports(1, &viewport);
	ApplyScissor();
	commandList->SetGraphicsRootSignature(_rootSignature.Get());
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void RmlRenderer_D3D12::EndFrame()
{
	_commandList = nullptr;
}

Rml::CompiledGeometryHandle RmlRenderer_D3D12::CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices)
{
	Geometry* geometry = new Geometry();
	geometry->vertices.assign(vertices.begin(), vertices.end());
	geometry->indices.assign(indices.begin(), indices.end());
	return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RmlRenderer_D3D12::ReleaseGeometry(Rml::CompiledGeometryHandle handle)
{
	delete reinterpret_cast<Geometry*>(handle);
}

void RmlRenderer_D3D12::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation, Rml::TextureHandle textureHandle)
{
	const Geometry* geometry = reinterpret_cast<const Geometry*>(handle);
	if (_commandList == nullptr || geometry == nullptr || geometry->indices.empty())
	{
		return;
	}

	const UINT64 vertexBytes = sizeof(Rml::Vertex) * geometry->vertices.size();
	const UINT64 indexBytes = sizeof(int) * geometry->indices.size();
	void* vertexCpu = nullptr;
	void* indexCpu = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS vertexGpu = 0;
	D3D12_GPU_VIRTUAL_ADDRESS indexGpu = 0;
	if (!AllocateFrameMemory(vertexBytes, 16, &vertexCpu, &vertexGpu) || !AllocateFrameMemory(indexBytes, 16, &indexCpu, &indexGpu))
	{
		return;
	}
	std::memcpy(vertexCpu, geometry->vertices.data(), vertexBytes);
	std::memcpy(indexCpu, geometry->indices.data(), indexBytes);

	D3D12_VERTEX_BUFFER_VIEW vbv = { vertexGpu, static_cast<UINT>(vertexBytes), sizeof(Rml::Vertex) };
	D3D12_INDEX_BUFFER_VIEW ibv = { indexGpu, static_cast<UINT>(indexBytes), DXGI_FORMAT_R32_UINT };

	Constants constants = {};
	std::memcpy(constants.transform, _transform.data(), sizeof(constants.transform));
	constants.translation[0] = translation.x;
	constants.translation[1] = translation.y;

	Texture* texture = reinterpret_cast<Texture*>(textureHandle);
	if (texture != nullptr && texture->uploaded)
	{
		_commandList->SetPipelineState(_texturePso.Get());
		_commandList->SetGraphicsRootDescriptorTable(Root_Texture, Descriptor::Singleton()->GetGpuHandle(texture->srvIndex));
	}
	else
	{
		if (texture != nullptr)
		{
			return;  // まだ転送していないテクスチャ（次のフレームで描く）
		}
		_commandList->SetPipelineState(_colorPso.Get());
	}
	_commandList->SetGraphicsRootSignature(_rootSignature.Get());
	if (texture != nullptr)
	{
		_commandList->SetGraphicsRootDescriptorTable(Root_Texture, Descriptor::Singleton()->GetGpuHandle(texture->srvIndex));
	}
	_commandList->SetGraphicsRoot32BitConstants(Root_Constants, sizeof(Constants) / 4, &constants, 0);
	_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	_commandList->IASetVertexBuffers(0, 1, &vbv);
	_commandList->IASetIndexBuffer(&ibv);
	_commandList->DrawIndexedInstanced(static_cast<UINT>(geometry->indices.size()), 1, 0, 0, 0);
}

RmlRenderer_D3D12::Texture* RmlRenderer_D3D12::CreateTexture(const Rml::byte* pixels, UINT width, UINT height)
{
	if (_device == nullptr || width == 0 || height == 0)
	{
		return nullptr;
	}

	auto texture = new Texture();
	texture->width = width;
	texture->height = height;

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = TextureFormat;
	desc.SampleDesc.Count = 1;
	if (FAILED(_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture->resource))))
	{
		delete texture;
		return nullptr;
	}

	// 転送用バッファ（行の幅は 256 バイト単位）
	const UINT rowPitch = static_cast<UINT>(AlignUp(width * 4ull, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
	D3D12_HEAP_PROPERTIES uploadProps = {};
	uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = static_cast<UINT64>(rowPitch) * height;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(_device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&texture->upload))))
	{
		delete texture;
		return nullptr;
	}
	UINT8* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	texture->upload->Map(0, &noRead, reinterpret_cast<void**>(&mapped));
	for (UINT y = 0; y < height; ++y)
	{
		std::memcpy(mapped + static_cast<size_t>(y) * rowPitch, pixels + static_cast<size_t>(y) * width * 4, width * 4);
	}
	texture->upload->Unmap(0, nullptr);

	texture->srvIndex = Descriptor::Singleton()->Allocate();
	if (texture->srvIndex == UINT_MAX)
	{
		delete texture;
		return nullptr;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = TextureFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	_device->CreateShaderResourceView(texture->resource.Get(), &srvDesc, Descriptor::Singleton()->GetCpuHandle(texture->srvIndex));

	// 描画中ならすぐ転送、そうでなければ次の描画の最初に転送する
	if (_commandList != nullptr)
	{
		RecordUpload(*texture);
	}
	else
	{
		_pendingUploads.push_back(texture);
	}
	return texture;
}

void RmlRenderer_D3D12::RecordUpload(Texture& texture)
{
	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = texture.resource.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = texture.upload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Footprint.Format = TextureFormat;
	src.PlacedFootprint.Footprint.Width = texture.width;
	src.PlacedFootprint.Footprint.Height = texture.height;
	src.PlacedFootprint.Footprint.Depth = 1;
	src.PlacedFootprint.Footprint.RowPitch = static_cast<UINT>(AlignUp(texture.width * 4ull, D3D12_TEXTURE_DATA_PITCH_ALIGNMENT));
	_commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(texture.resource.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_commandList->ResourceBarrier(1, &barrier);
	texture.uploaded = true;

	// 転送用バッファはこのフレームが終わるまで残す
	_slots[_currentSlot].retired.push_back(texture.upload);
	texture.upload.Reset();
}

Rml::TextureHandle RmlRenderer_D3D12::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions)
{
	return reinterpret_cast<Rml::TextureHandle>(CreateTexture(source.data(), static_cast<UINT>(sourceDimensions.x), static_cast<UINT>(sourceDimensions.y)));
}

Rml::TextureHandle RmlRenderer_D3D12::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source)
{
	// WIC で読み込み、乗算済みアルファの RGBA にする
	ComPtr<IWICImagingFactory> factory;
	if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
	{
		return 0;
	}
	const std::wstring path = Utf8ToPath(source).wstring();
	ComPtr<IWICBitmapDecoder> decoder;
	ComPtr<IWICBitmapFrameDecode> frame;
	ComPtr<IWICFormatConverter> converter;
	if (FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
		FAILED(decoder->GetFrame(0, &frame)) ||
		FAILED(factory->CreateFormatConverter(&converter)) ||
		FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPRGBA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "[RmlRenderer] Failed to load image: %s", source.c_str());
		return 0;
	}
	UINT width = 0, height = 0;
	converter->GetSize(&width, &height);
	std::vector<Rml::byte> pixels(static_cast<size_t>(width) * height * 4);
	if (FAILED(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(pixels.size()), pixels.data())))
	{
		return 0;
	}
	textureDimensions = Rml::Vector2i(static_cast<int>(width), static_cast<int>(height));
	return reinterpret_cast<Rml::TextureHandle>(CreateTexture(pixels.data(), width, height));
}

void RmlRenderer_D3D12::ReleaseTexture(Rml::TextureHandle handle)
{
	Texture* texture = reinterpret_cast<Texture*>(handle);
	if (texture == nullptr)
	{
		return;
	}
	_pendingUploads.erase(std::remove(_pendingUploads.begin(), _pendingUploads.end(), texture), _pendingUploads.end());
	// GPU が使い終わるまで待ってから解放する（このスロットが次に使われるとき）
	if (_slots.empty())
	{
		delete texture;
		return;
	}
	_slots[_currentSlot].retiredTextures.push_back(texture);
}

void RmlRenderer_D3D12::EnableScissorRegion(bool enable)
{
	_scissorEnabled = enable;
	ApplyScissor();
}

void RmlRenderer_D3D12::SetScissorRegion(Rml::Rectanglei region)
{
	_scissor = region;
	ApplyScissor();
}

void RmlRenderer_D3D12::ApplyScissor()
{
	if (_commandList == nullptr)
	{
		return;
	}
	D3D12_RECT rect = { 0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height) };
	if (_scissorEnabled)
	{
		rect.left = (std::max)(0L, static_cast<LONG>(_scissor.Left()));
		rect.top = (std::max)(0L, static_cast<LONG>(_scissor.Top()));
		rect.right = (std::max)(rect.left, static_cast<LONG>(_scissor.Right()));
		rect.bottom = (std::max)(rect.top, static_cast<LONG>(_scissor.Bottom()));
	}
	_commandList->RSSetScissorRects(1, &rect);
}

void RmlRenderer_D3D12::SetTransform(const Rml::Matrix4f* transform)
{
	_transform = (transform != nullptr) ? _projection * (*transform) : _projection;
}
