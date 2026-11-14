#include "StageRenderer.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <cstring>
#include <string>
#include <vector>

#include "../../DirectX12/DX12CommandQueue.h"
#include "../../DirectX12/Descriptor.h"

using Microsoft::WRL::ComPtr;

namespace
{
constexpr DXGI_FORMAT ColorFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr DXGI_FORMAT DepthFormat = DXGI_FORMAT_D32_FLOAT;

// ----------------------------------------------------------------------------
// シェーダー
// ステージ用テクスチャは乗算済みアルファ（背景は透明）
// ----------------------------------------------------------------------------
const char ShaderSource[] = R"(
cbuffer StageConstants : register(b0)
{
	float2 texelSize;       // 入力テクスチャの 1 テクセル（UV）
	float  bloomThreshold;
	float  bloomIntensity;

	float4 background;      // rgb: 背景色

	float2 shadowOffset;    // 影のずれ（UV）
	float  shadowStrength;
	float  shadowSoftness;

	float  vignette;
	float  contrast;
	float  lutStrength;
	float  blackout;

	float2 spotCenter;      // NDC
	float  spotRadius;
	float  spotAmount;

	float  aspect;
	float  lutSize;
	float2 padding;
};

Texture2D source : register(t0);
Texture2D bloomTexture : register(t1);
Texture3D lutTexture : register(t2);
SamplerState linearClamp : register(s0);

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

VSOutput VSMain(uint id : SV_VertexID)
{
	VSOutput o;
	float2 uv = float2((id << 1) & 2, id & 2);
	o.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
	o.uv = uv;
	return o;
}

// ---- ブルーム：明るい所を取り出しながら 1/4 に縮小 ----
float4 PSBright(VSOutput input) : SV_TARGET
{
	float2 o = texelSize;
	float4 c = source.SampleLevel(linearClamp, input.uv + o * float2(-1.0, -1.0), 0)
	         + source.SampleLevel(linearClamp, input.uv + o * float2( 1.0, -1.0), 0)
	         + source.SampleLevel(linearClamp, input.uv + o * float2(-1.0,  1.0), 0)
	         + source.SampleLevel(linearClamp, input.uv + o * float2( 1.0,  1.0), 0);
	c *= 0.25;
	float brightness = max(c.r, max(c.g, c.b));
	return c * smoothstep(bloomThreshold, bloomThreshold + 0.25, brightness);
}

// ---- ブルーム：ガウスぼかし（横・縦）----
float4 Blur(float2 uv, float2 direction)
{
	const float weights[5] = { 0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216 };
	float4 c = source.SampleLevel(linearClamp, uv, 0) * weights[0];
	[unroll]
	for (int i = 1; i < 5; ++i)
	{
		float2 offset = direction * (i * 1.5);
		c += source.SampleLevel(linearClamp, uv + offset, 0) * weights[i];
		c += source.SampleLevel(linearClamp, uv - offset, 0) * weights[i];
	}
	return c;
}

float4 PSBlurH(VSOutput input) : SV_TARGET { return Blur(input.uv, float2(texelSize.x, 0.0)); }
float4 PSBlurV(VSOutput input) : SV_TARGET { return Blur(input.uv, float2(0.0, texelSize.y)); }

// ---- 半透明の参加者：1体分のレイヤーを不透明度を掛けて重ねる ----
// （パーツごとに薄くすると重なった奥のパーツが透けて見えるため、まとめて薄くする）
float4 PSLayer(VSOutput input) : SV_TARGET
{
	return source.SampleLevel(linearClamp, input.uv, 0) * bloomIntensity;
}

// ---- 仕上げ ----
float4 PSComposite(VSOutput input) : SV_TARGET
{
	float2 uv = input.uv;
	float4 c = source.SampleLevel(linearClamp, uv, 0);

	// 影：光と反対側にずらした輪郭を、ぼかして下に敷く
	if (shadowStrength > 0.0)
	{
		float2 s = uv - shadowOffset;
		float2 d = float2(shadowSoftness / aspect, shadowSoftness);
		float a = source.SampleLevel(linearClamp, s, 0).a * 0.4
		        + source.SampleLevel(linearClamp, s + d * float2( 1.0,  1.0), 0).a * 0.15
		        + source.SampleLevel(linearClamp, s + d * float2(-1.0,  1.0), 0).a * 0.15
		        + source.SampleLevel(linearClamp, s + d * float2( 1.0, -1.0), 0).a * 0.15
		        + source.SampleLevel(linearClamp, s + d * float2(-1.0, -1.0), 0).a * 0.15;
		c.a += a * shadowStrength * (1.0 - c.a);
	}

	// モデルの色調（乗算済みアルファを戻してから調整する）
	if (c.a > 1e-4)
	{
		float3 rgb = c.rgb / c.a;

		rgb = saturate((rgb - 0.5) * (1.0 + contrast) + 0.5);

		if (lutStrength > 0.0)
		{
			float3 uvw = rgb * ((lutSize - 1.0) / lutSize) + 0.5 / lutSize;
			rgb = lerp(rgb, lutTexture.SampleLevel(linearClamp, uvw, 0).rgb, lutStrength);
		}

		// ビネット（画面の端ほど暗く）
		float2 p = (uv - 0.5) * float2(aspect, 1.0);
		float edge = smoothstep(0.35, 1.0, length(p) / (0.5 * sqrt(aspect * aspect + 1.0)));
		rgb *= 1.0 - vignette * edge;

		// スポットライト（円の外を暗くする）
		if (spotAmount > 0.0)
		{
			float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
			float distance = length((ndc - spotCenter) * float2(aspect, 1.0));
			float inside = 1.0 - smoothstep(spotRadius * 0.8, spotRadius * 1.2, distance);
			rgb *= lerp(1.0, lerp(0.15, 1.1, inside), spotAmount);
		}

		c.rgb = rgb * c.a;
	}

	// ブルーム（光のにじみ）
	float4 bloom = bloomTexture.SampleLevel(linearClamp, uv, 0) * bloomIntensity;
	c.rgb += bloom.rgb;
	c.a = saturate(c.a + max(bloom.r, max(bloom.g, bloom.b)) * 0.5 * (1.0 - c.a));

	float3 color = c.rgb + background.rgb * (1.0 - saturate(c.a));
	color *= 1.0 - blackout;
	return float4(saturate(color), 1.0);
}
)";

struct Constants
{
	float texelSize[2];
	float bloomThreshold;
	float bloomIntensity;

	float background[4];

	float shadowOffset[2];
	float shadowStrength;
	float shadowSoftness;

	float vignette;
	float contrast;
	float lutStrength;
	float blackout;

	float spotCenter[2];
	float spotRadius;
	float spotAmount;

	float aspect;
	float lutSize;
	float padding[2];
};
static_assert(sizeof(Constants) == 96, "root constants size");

enum RootParameter
{
	Root_Constants,
	Root_Source,
	Root_Bloom,
	Root_Lut,
	Root_Count
};

void LogError(const char* message, ID3DBlob* error = nullptr)
{
	std::string text = "[StageRenderer] ";
	text += message;
	if (error != nullptr)
	{
		text += ": ";
		text += static_cast<const char*>(error->GetBufferPointer());
	}
	text += "\n";
	OutputDebugStringA(text.c_str());
}

ComPtr<ID3DBlob> CompileShader(const char* entry, const char* target)
{
	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif
	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "StageRenderer", nullptr, nullptr, entry, target, flags, 0, &blob, &error)))
	{
		LogError("Shader compile failed", error.Get());
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
} // namespace

StageRenderer::~StageRenderer()
{
	Release();
}

bool StageRenderer::Initialize(ID3D12Device* device, DX12CommandQueue* commandQueue)
{
	if (device == nullptr || commandQueue == nullptr)
	{
		return false;
	}
	_device = device;
	_commandQueue = commandQueue;

	// ---- ルートシグネチャ：b0 = 定数、t0 = 入力、t1 = ブルーム、t2 = LUT ----
	D3D12_DESCRIPTOR_RANGE ranges[3] = {};
	for (UINT i = 0; i < 3; ++i)
	{
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i;
	}
	D3D12_ROOT_PARAMETER params[Root_Count] = {};
	params[Root_Constants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	params[Root_Constants].Constants.ShaderRegister = 0;
	params[Root_Constants].Constants.Num32BitValues = sizeof(Constants) / 4;
	params[Root_Constants].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	for (UINT i = 0; i < 3; ++i)
	{
		params[Root_Source + i].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
		params[Root_Source + i].DescriptorTable.NumDescriptorRanges = 1;
		params[Root_Source + i].DescriptorTable.pDescriptorRanges = &ranges[i];
		params[Root_Source + i].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	}

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

	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &error)) ||
		FAILED(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature))))
	{
		LogError("Failed to create root signature", error.Get());
		return false;
	}

	// ---- PSO ----
	ComPtr<ID3DBlob> vs = CompileShader("VSMain", "vs_5_0");
	ComPtr<ID3DBlob> bright = CompileShader("PSBright", "ps_5_0");
	ComPtr<ID3DBlob> blurH = CompileShader("PSBlurH", "ps_5_0");
	ComPtr<ID3DBlob> blurV = CompileShader("PSBlurV", "ps_5_0");
	ComPtr<ID3DBlob> composite = CompileShader("PSComposite", "ps_5_0");
	ComPtr<ID3DBlob> layer = CompileShader("PSLayer", "ps_5_0");
	if (!vs || !bright || !blurH || !blurV || !composite || !layer)
	{
		return false;
	}

	auto createPso = [&](ID3DBlob* ps, ComPtr<ID3D12PipelineState>& out, bool premultipliedBlend = false) -> bool
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
		desc.pRootSignature = _rootSignature.Get();
		desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
		desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
		desc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
		if (premultipliedBlend)
		{
			D3D12_RENDER_TARGET_BLEND_DESC& blend = desc.BlendState.RenderTarget[0];
			blend.BlendEnable = TRUE;
			blend.SrcBlend = D3D12_BLEND_ONE;
			blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOp = D3D12_BLEND_OP_ADD;
			blend.SrcBlendAlpha = D3D12_BLEND_ONE;
			blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
			blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
			blend.LogicOp = D3D12_LOGIC_OP_NOOP;
		}
		desc.SampleMask = UINT_MAX;
		desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
		desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
		desc.RasterizerState.DepthClipEnable = TRUE;
		desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		desc.NumRenderTargets = 1;
		desc.RTVFormats[0] = ColorFormat;
		desc.SampleDesc.Count = 1;
		return SUCCEEDED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&out)));
	};
	if (!createPso(bright.Get(), _brightPso) || !createPso(blurH.Get(), _blurHPso) ||
		!createPso(blurV.Get(), _blurVPso) || !createPso(composite.Get(), _compositePso) ||
		!createPso(layer.Get(), _layerPso, true))
	{
		LogError("Failed to create pipeline state");
		return false;
	}

	// ---- 描画先 ----
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 4;
	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc = {};
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.NumDescriptors = 1;
	if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap))) ||
		FAILED(device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&_dsvHeap))))
	{
		LogError("Failed to create descriptor heaps");
		return false;
	}

	const UINT bloomWidth = Width / BloomDivisor;
	const UINT bloomHeight = Height / BloomDivisor;
	if (!CreateTarget(_stage, Width, Height, 0) || !CreateTarget(_bloomA, bloomWidth, bloomHeight, 1) ||
		!CreateTarget(_bloomB, bloomWidth, bloomHeight, 2) || !CreateTarget(_layer, Width, Height, 3))
	{
		return false;
	}

	// 深度（Live2D の PSO が深度テストを使うため、ステージと同じ大きさで用意する）
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = Width;
		desc.Height = Height;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.Format = DepthFormat;
		desc.SampleDesc.Count = 1;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_CLEAR_VALUE clear = {};
		clear.Format = DepthFormat;
		clear.DepthStencil.Depth = 1.0f;
		if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear, IID_PPV_ARGS(&_depth))))
		{
			LogError("Failed to create depth buffer");
			return false;
		}
		device->CreateDepthStencilView(_depth.Get(), nullptr, _dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// LUT（3D テクスチャ）
	{
		D3D12_HEAP_PROPERTIES heapProps = {};
		heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
		desc.Width = LutSize;
		desc.Height = LutSize;
		desc.DepthOrArraySize = LutSize;
		desc.MipLevels = 1;
		desc.Format = ColorFormat;
		desc.SampleDesc.Count = 1;
		_lutState = D3D12_RESOURCE_STATE_COPY_DEST;
		if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, _lutState, nullptr, IID_PPV_ARGS(&_lut))))
		{
			LogError("Failed to create LUT texture");
			return false;
		}

		D3D12_HEAP_PROPERTIES uploadProps = {};
		uploadProps.Type = D3D12_HEAP_TYPE_UPLOAD;
		D3D12_RESOURCE_DESC bufferDesc = {};
		bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		bufferDesc.Width = static_cast<UINT64>(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT) * LutSize * LutSize;
		bufferDesc.Height = 1;
		bufferDesc.DepthOrArraySize = 1;
		bufferDesc.MipLevels = 1;
		bufferDesc.SampleDesc.Count = 1;
		bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		if (FAILED(device->CreateCommittedResource(&uploadProps, D3D12_HEAP_FLAG_NONE, &bufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_lutUpload))))
		{
			LogError("Failed to create LUT upload buffer");
			return false;
		}

		_lutSrvIndex = Descriptor::Singleton()->Allocate();
		if (_lutSrvIndex == UINT_MAX)
		{
			LogError("Descriptor heap is full");
			return false;
		}
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = ColorFormat;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		srvDesc.Texture3D.MipLevels = 1;
		device->CreateShaderResourceView(_lut.Get(), &srvDesc, Descriptor::Singleton()->GetCpuHandle(_lutSrvIndex));
	}
	_currentLut = -1;
	return true;
}

bool StageRenderer::CreateTarget(Target& target, UINT width, UINT height, UINT rtvSlot)
{
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = ColorFormat;
	desc.SampleDesc.Count = 1;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	D3D12_CLEAR_VALUE clear = {};
	clear.Format = ColorFormat;

	target.state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (FAILED(_device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, target.state, &clear, IID_PPV_ARGS(&target.texture))))
	{
		LogError("Failed to create render target");
		return false;
	}

	const UINT rtvSize = _device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	target.rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	target.rtv.ptr += static_cast<SIZE_T>(rtvSlot) * rtvSize;
	_device->CreateRenderTargetView(target.texture.Get(), nullptr, target.rtv);

	target.srvIndex = Descriptor::Singleton()->Allocate();
	if (target.srvIndex == UINT_MAX)
	{
		LogError("Descriptor heap is full");
		return false;
	}
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = ColorFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	_device->CreateShaderResourceView(target.texture.Get(), &srvDesc, Descriptor::Singleton()->GetCpuHandle(target.srvIndex));

	target.width = width;
	target.height = height;
	return true;
}

void StageRenderer::ReleaseTarget(Target& target)
{
	if (target.srvIndex != UINT_MAX)
	{
		Descriptor::Singleton()->Free(target.srvIndex);
		target.srvIndex = UINT_MAX;
	}
	target.texture.Reset();
}

void StageRenderer::Release()
{
	ReleaseTarget(_stage);
	ReleaseTarget(_bloomA);
	ReleaseTarget(_bloomB);
	ReleaseTarget(_layer);
	_layerPso.Reset();
	if (_lutSrvIndex != UINT_MAX)
	{
		Descriptor::Singleton()->Free(_lutSrvIndex);
		_lutSrvIndex = UINT_MAX;
	}
	_lut.Reset();
	_lutUpload.Reset();
	_depth.Reset();
	_rtvHeap.Reset();
	_dsvHeap.Reset();
	_brightPso.Reset();
	_blurHPso.Reset();
	_blurVPso.Reset();
	_compositePso.Reset();
	_rootSignature.Reset();
	_device = nullptr;
}

void StageRenderer::Transition(ID3D12GraphicsCommandList* commandList, ID3D12Resource* resource, D3D12_RESOURCE_STATES& state, D3D12_RESOURCE_STATES after)
{
	if (state == after)
	{
		return;
	}
	D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(resource, state, after);
	commandList->ResourceBarrier(1, &barrier);
	state = after;
}

void StageRenderer::UpdateLut(ID3D12GraphicsCommandList* commandList, StageLut lut)
{
	if (!_lut || static_cast<int>(lut) == _currentLut)
	{
		return;
	}

	// アップロード用バッファを前のフレームのコピーが使い終わるまで待つ（LUT を変えたときだけ）
	if (_currentLut >= 0)
	{
		_commandQueue->Flush();
	}

	const std::vector<uint8_t> data = BuildStageLut(lut, LutSize);
	const UINT rowPitch = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT;
	const UINT slicePitch = rowPitch * LutSize;
	UINT8* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	if (FAILED(_lutUpload->Map(0, &noRead, reinterpret_cast<void**>(&mapped))))
	{
		return;
	}
	for (UINT z = 0; z < LutSize; ++z)
	{
		for (UINT y = 0; y < LutSize; ++y)
		{
			std::memcpy(mapped + z * slicePitch + y * rowPitch, &data[(static_cast<size_t>(z) * LutSize + y) * LutSize * 4], LutSize * 4);
		}
	}
	_lutUpload->Unmap(0, nullptr);

	Transition(commandList, _lut.Get(), _lutState, D3D12_RESOURCE_STATE_COPY_DEST);

	D3D12_TEXTURE_COPY_LOCATION dst = {};
	dst.pResource = _lut.Get();
	dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dst.SubresourceIndex = 0;
	D3D12_TEXTURE_COPY_LOCATION src = {};
	src.pResource = _lutUpload.Get();
	src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	src.PlacedFootprint.Offset = 0;
	src.PlacedFootprint.Footprint.Format = ColorFormat;
	src.PlacedFootprint.Footprint.Width = LutSize;
	src.PlacedFootprint.Footprint.Height = LutSize;
	src.PlacedFootprint.Footprint.Depth = LutSize;
	src.PlacedFootprint.Footprint.RowPitch = rowPitch;
	commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

	Transition(commandList, _lut.Get(), _lutState, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	_currentLut = static_cast<int>(lut);
}

Live2DRenderTarget StageRenderer::BeginActors(ID3D12GraphicsCommandList* commandList)
{
	Transition(commandList, _stage.texture.Get(), _stage.state, D3D12_RESOURCE_STATE_RENDER_TARGET);

	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	const D3D12_CPU_DESCRIPTOR_HANDLE dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
	commandList->ClearRenderTargetView(_stage.rtv, clear, 0, nullptr);
	commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	Live2DRenderTarget target;
	target.rtv = _stage.rtv;
	target.dsv = dsv;
	target.hasDsv = true;
	target.viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
	target.scissor = { 0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height) };
	return target;
}

Live2DRenderTarget StageRenderer::BeginLayer(ID3D12GraphicsCommandList* commandList)
{
	Transition(commandList, _layer.texture.Get(), _layer.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
	const float clear[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	commandList->ClearRenderTargetView(_layer.rtv, clear, 0, nullptr);

	Live2DRenderTarget target;
	target.rtv = _layer.rtv;
	target.dsv = _dsvHeap->GetCPUDescriptorHandleForHeapStart();
	target.hasDsv = true;
	target.viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
	target.scissor = { 0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height) };
	return target;
}

void StageRenderer::EndLayer(ID3D12GraphicsCommandList* commandList, float opacity)
{
	Transition(commandList, _layer.texture.Get(), _layer.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	commandList->OMSetRenderTargets(1, &_stage.rtv, FALSE, nullptr);
	const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(Width), static_cast<float>(Height), 0.0f, 1.0f };
	const D3D12_RECT scissor = { 0, 0, static_cast<LONG>(Width), static_cast<LONG>(Height) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	Constants constants = {};
	constants.bloomIntensity = opacity;  // PSLayer では不透明度として使う
	Descriptor* descriptor = Descriptor::Singleton();
	commandList->SetGraphicsRootSignature(_rootSignature.Get());
	commandList->SetPipelineState(_layerPso.Get());
	commandList->SetGraphicsRoot32BitConstants(Root_Constants, sizeof(Constants) / 4, &constants, 0);
	commandList->SetGraphicsRootDescriptorTable(Root_Source, descriptor->GetGpuHandle(_layer.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Bloom, descriptor->GetGpuHandle(_layer.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Lut, descriptor->GetGpuHandle(_lutSrvIndex));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}

void StageRenderer::DrawPass(ID3D12GraphicsCommandList* commandList, ID3D12PipelineState* pso, Target& output,
	const Target& source, const void* constants)
{
	Transition(commandList, output.texture.Get(), output.state, D3D12_RESOURCE_STATE_RENDER_TARGET);
	commandList->OMSetRenderTargets(1, &output.rtv, FALSE, nullptr);
	const D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(output.width), static_cast<float>(output.height), 0.0f, 1.0f };
	const D3D12_RECT scissor = { 0, 0, static_cast<LONG>(output.width), static_cast<LONG>(output.height) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	Descriptor* descriptor = Descriptor::Singleton();
	commandList->SetGraphicsRootSignature(_rootSignature.Get());
	commandList->SetPipelineState(pso);
	commandList->SetGraphicsRoot32BitConstants(Root_Constants, sizeof(Constants) / 4, constants, 0);
	commandList->SetGraphicsRootDescriptorTable(Root_Source, descriptor->GetGpuHandle(source.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Bloom, descriptor->GetGpuHandle(source.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Lut, descriptor->GetGpuHandle(_lutSrvIndex));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);

	Transition(commandList, output.texture.Get(), output.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
}

void StageRenderer::EndActors(ID3D12GraphicsCommandList* commandList, float bloomThreshold)
{
	Transition(commandList, _stage.texture.Get(), _stage.state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);

	Constants constants = {};
	constants.texelSize[0] = 1.0f / Width;
	constants.texelSize[1] = 1.0f / Height;
	constants.bloomThreshold = bloomThreshold;
	DrawPass(commandList, _brightPso.Get(), _bloomA, _stage, &constants);

	constants.texelSize[0] = 1.0f / _bloomA.width;
	constants.texelSize[1] = 1.0f / _bloomA.height;
	DrawPass(commandList, _blurHPso.Get(), _bloomB, _bloomA, &constants);
	DrawPass(commandList, _blurVPso.Get(), _bloomA, _bloomB, &constants);
}

void StageRenderer::Composite(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv,
	const D3D12_VIEWPORT& viewport, const D3D12_RECT& scissor, const StageCompositeParams& params)
{
	commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);

	Constants constants = {};
	constants.texelSize[0] = 1.0f / Width;
	constants.texelSize[1] = 1.0f / Height;
	constants.bloomIntensity = params.bloomIntensity;
	constants.background[0] = params.background[0];
	constants.background[1] = params.background[1];
	constants.background[2] = params.background[2];
	constants.background[3] = 1.0f;
	constants.shadowOffset[0] = params.shadowOffset[0];
	constants.shadowOffset[1] = params.shadowOffset[1];
	constants.shadowStrength = params.shadowStrength;
	constants.shadowSoftness = params.shadowSoftness;
	constants.vignette = params.vignette;
	constants.contrast = params.contrast;
	constants.lutStrength = (_currentLut >= 0) ? params.lutStrength : 0.0f;
	constants.blackout = params.blackout;
	constants.spotCenter[0] = params.spotCenter[0];
	constants.spotCenter[1] = params.spotCenter[1];
	constants.spotRadius = params.spotRadius;
	constants.spotAmount = params.spotAmount;
	constants.aspect = static_cast<float>(Width) / Height;
	constants.lutSize = static_cast<float>(LutSize);

	Descriptor* descriptor = Descriptor::Singleton();
	commandList->SetGraphicsRootSignature(_rootSignature.Get());
	commandList->SetPipelineState(_compositePso.Get());
	commandList->SetGraphicsRoot32BitConstants(Root_Constants, sizeof(Constants) / 4, &constants, 0);
	commandList->SetGraphicsRootDescriptorTable(Root_Source, descriptor->GetGpuHandle(_stage.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Bloom, descriptor->GetGpuHandle(_bloomA.srvIndex));
	commandList->SetGraphicsRootDescriptorTable(Root_Lut, descriptor->GetGpuHandle(_lutSrvIndex));
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	commandList->DrawInstanced(3, 1, 0, 0);
}
