#include "CubismRenderer_D3D12.hpp"

#include <d3dcompiler.h>
#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Model/CubismModel.hpp"
#include "Rendering/csmBlendMode.hpp"
#include "Utils/CubismDebug.hpp"

#include "../../DirectX12/Descriptor.h"

#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

namespace Live2D { namespace Cubism { namespace Framework { namespace Rendering {

namespace {

// ----------------------------------------------------------------------------
// 定数
// ----------------------------------------------------------------------------
constexpr DXGI_FORMAT MaskFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
constexpr UINT64 ConstantBufferAlignment = D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT; // 256
constexpr UINT DefaultMaskBufferSize = 1024;

enum BlendType { Blend_Normal, Blend_Add, Blend_Mult, Blend_Count };
enum CullType { Cull_None, Cull_Back, Cull_Count };
enum MaskType { Mask_None, Mask_Normal, Mask_Inverted, Mask_Count };
enum AlphaType { Alpha_Straight, Alpha_Premultiplied, Alpha_Count };

// ルートパラメータの番号
enum RootParameter
{
	Root_Constants = 0,	// b0
	Root_MainTexture,	// t0
	Root_MaskTexture,	// t1
	Root_Count
};

struct Vertex
{
	float x, y;
	float u, v;
};

// HLSL の cbuffer と同じ並び
struct ConstantBuffer
{
	float projectMatrix[16];
	float clipMatrix[16];
	float baseColor[4];
	float multiplyColor[4];
	float screenColor[4];
	float channelFlag[4];
};

// ----------------------------------------------------------------------------
// シェーダー
// 行列は CubismMatrix44 の配列（列優先）をそのまま転送し、mul(M, v) で計算する
// ----------------------------------------------------------------------------
const char ShaderSource[] = R"(
cbuffer CubismConstants : register(b0)
{
	float4x4 projectMatrix;
	float4x4 clipMatrix;
	float4 baseColor;
	float4 multiplyColor;
	float4 screenColor;
	float4 channelFlag;
};

Texture2D mainTexture : register(t0);
Texture2D maskTexture : register(t1);
SamplerState mainSampler : register(s0);
SamplerState maskSampler : register(s1);

struct VSInput
{
	float2 pos : POSITION;
	float2 uv : TEXCOORD0;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
	float4 clipPosition : TEXCOORD1;
};

// ---- マスク生成 ----
VSOutput VSSetupMask(VSInput input)
{
	VSOutput o;
	float4 pos = mul(projectMatrix, float4(input.pos, 0.0, 1.0));
	o.position = pos;
	o.clipPosition = pos;
	o.uv = float2(input.uv.x, 1.0 - input.uv.y);
	return o;
}

float4 PSSetupMask(VSOutput input) : SV_TARGET
{
	float2 cp = input.clipPosition.xy / input.clipPosition.w;
	float isInside = step(baseColor.x, cp.x) * step(baseColor.y, cp.y)
	               * step(cp.x, baseColor.z) * step(cp.y, baseColor.w);
	return channelFlag * mainTexture.Sample(mainSampler, input.uv).a * isInside;
}

// ---- 通常描画 ----
VSOutput VSMain(VSInput input)
{
	VSOutput o;
	float4 local = float4(input.pos, 0.0, 1.0);
	o.position = mul(projectMatrix, local);
	o.clipPosition = mul(clipMatrix, local);
	o.uv = float2(input.uv.x, 1.0 - input.uv.y);
	return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	float4 texColor = mainTexture.Sample(mainSampler, input.uv);
	texColor.rgb *= multiplyColor.rgb;
#if PREMULTIPLIED_ALPHA
	texColor.rgb = texColor.rgb + screenColor.rgb * texColor.a - texColor.rgb * screenColor.rgb;
	float4 color = texColor * baseColor;
#else
	texColor.rgb = texColor.rgb + screenColor.rgb - texColor.rgb * screenColor.rgb;
	float4 color = texColor * baseColor;
	color.rgb *= color.a;
#endif

#if MASKED
	float4 clipMask = (1.0 - maskTexture.Sample(maskSampler, input.clipPosition.xy / input.clipPosition.w)) * channelFlag;
	float maskVal = clipMask.r + clipMask.g + clipMask.b + clipMask.a;
#if INVERTED
	color *= 1.0 - maskVal;
#else
	color *= maskVal;
#endif
#endif
	return color;
}
)";

// ----------------------------------------------------------------------------
// 全モデル共通のリソース
// ----------------------------------------------------------------------------
struct SharedResources
{
	ComPtr<ID3D12Device> device;
	ComPtr<ID3D12RootSignature> rootSignature;
	ComPtr<ID3D12PipelineState> maskPso[Cull_Count];
	ComPtr<ID3D12PipelineState> drawPso[Blend_Count][Cull_Count][Mask_Count][Alpha_Count];
	UINT frameCount = 2;
	bool initialized = false;
};

SharedResources g_shared;

UINT64 AlignUp(UINT64 value, UINT64 alignment)
{
	return (value + alignment - 1) & ~(alignment - 1);
}

D3D12_RESOURCE_BARRIER TransitionBarrier(ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
	D3D12_RESOURCE_BARRIER barrier = {};
	barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	barrier.Transition.pResource = resource;
	barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barrier.Transition.StateBefore = before;
	barrier.Transition.StateAfter = after;
	return barrier;
}

ComPtr<ID3DBlob> CompileShader(const char* entry, const char* target, bool masked, bool inverted, bool premultiplied)
{
	const D3D_SHADER_MACRO defines[] =
	{
		{ "MASKED", masked ? "1" : "0" },
		{ "INVERTED", inverted ? "1" : "0" },
		{ "PREMULTIPLIED_ALPHA", premultiplied ? "1" : "0" },
		{ nullptr, nullptr },
	};

	UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
#endif

	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "CubismShader_D3D12", defines, nullptr,
		entry, target, flags, 0, &blob, &error);
	if (FAILED(hr))
	{
		CubismLogError("[D3D12] Shader compile failed (%s): %s", entry,
			error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown error");
		return nullptr;
	}
	return blob;
}

D3D12_RENDER_TARGET_BLEND_DESC MakeBlend(D3D12_BLEND src, D3D12_BLEND dst, D3D12_BLEND srcAlpha, D3D12_BLEND dstAlpha)
{
	D3D12_RENDER_TARGET_BLEND_DESC desc = {};
	desc.BlendEnable = TRUE;
	desc.LogicOpEnable = FALSE;
	desc.SrcBlend = src;
	desc.DestBlend = dst;
	desc.BlendOp = D3D12_BLEND_OP_ADD;
	desc.SrcBlendAlpha = srcAlpha;
	desc.DestBlendAlpha = dstAlpha;
	desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	desc.LogicOp = D3D12_LOGIC_OP_NOOP;
	desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	return desc;
}

D3D12_RENDER_TARGET_BLEND_DESC GetBlendDesc(BlendType type)
{
	switch (type)
	{
	case Blend_Add:
		return MakeBlend(D3D12_BLEND_ONE, D3D12_BLEND_ONE, D3D12_BLEND_ZERO, D3D12_BLEND_ONE);
	case Blend_Mult:
		return MakeBlend(D3D12_BLEND_DEST_COLOR, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_ZERO, D3D12_BLEND_ONE);
	case Blend_Normal:
	default:
		return MakeBlend(D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_ONE, D3D12_BLEND_INV_SRC_ALPHA);
	}
}

bool CreateRootSignature(ID3D12Device* device)
{
	D3D12_DESCRIPTOR_RANGE ranges[2] = {};
	for (UINT i = 0; i < 2; ++i)
	{
		ranges[i].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
		ranges[i].NumDescriptors = 1;
		ranges[i].BaseShaderRegister = i; // t0, t1
		ranges[i].RegisterSpace = 0;
		ranges[i].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
	}

	D3D12_ROOT_PARAMETER params[Root_Count] = {};
	params[Root_Constants].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	params[Root_Constants].Descriptor.ShaderRegister = 0;
	params[Root_Constants].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	params[Root_MainTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[Root_MainTexture].DescriptorTable.NumDescriptorRanges = 1;
	params[Root_MainTexture].DescriptorTable.pDescriptorRanges = &ranges[0];
	params[Root_MainTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	params[Root_MaskTexture].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	params[Root_MaskTexture].DescriptorTable.NumDescriptorRanges = 1;
	params[Root_MaskTexture].DescriptorTable.pDescriptorRanges = &ranges[1];
	params[Root_MaskTexture].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_STATIC_SAMPLER_DESC samplers[2] = {};
	// s0: モデルのテクスチャ用（ミップマップあり）
	samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	samplers[0].MaxAnisotropy = 1;
	samplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	samplers[0].MinLOD = 0.0f;
	samplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	samplers[0].ShaderRegister = 0;
	samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	// s1: マスク用
	// 描画時のマスク行列は V が負になる（isRightHanded）ので、WRAP で 1-V に折り返して参照する
	samplers[1] = samplers[0];
	samplers[1].ShaderRegister = 1;

	D3D12_ROOT_SIGNATURE_DESC desc = {};
	desc.NumParameters = Root_Count;
	desc.pParameters = params;
	desc.NumStaticSamplers = 2;
	desc.pStaticSamplers = samplers;
	desc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> blob;
	ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(&desc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	if (FAILED(hr))
	{
		CubismLogError("[D3D12] Root signature serialize failed: %s",
			error ? static_cast<const char*>(error->GetBufferPointer()) : "unknown error");
		return false;
	}

	hr = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_shared.rootSignature));
	return SUCCEEDED(hr);
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC MakePsoDesc(ID3DBlob* vs, ID3DBlob* ps, CullType cull, const D3D12_RENDER_TARGET_BLEND_DESC& blend,
	DXGI_FORMAT rtvFormat, DXGI_FORMAT dsvFormat)
{
	static const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
	desc.pRootSignature = g_shared.rootSignature.Get();
	desc.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	desc.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };

	desc.BlendState.AlphaToCoverageEnable = FALSE;
	desc.BlendState.IndependentBlendEnable = FALSE;
	desc.BlendState.RenderTarget[0] = blend;
	desc.SampleMask = UINT_MAX;

	desc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	desc.RasterizerState.CullMode = (cull == Cull_Back) ? D3D12_CULL_MODE_BACK : D3D12_CULL_MODE_NONE;
	desc.RasterizerState.FrontCounterClockwise = TRUE; // Cubism は反時計回りが表面
	desc.RasterizerState.DepthClipEnable = TRUE;

	// 3D 空間に置いたとき、手前の物（床など）に隠れるように深度テストだけ行う。
	// パーツは同じ平面にあるので深度は書き込まない（書くと後から描くパーツが消える）。
	desc.DepthStencilState.DepthEnable = (dsvFormat != DXGI_FORMAT_UNKNOWN) ? TRUE : FALSE;
	desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;
	desc.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	desc.DepthStencilState.StencilEnable = FALSE;

	desc.InputLayout = { inputLayout, _countof(inputLayout) };
	desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	desc.NumRenderTargets = 1;
	desc.RTVFormats[0] = rtvFormat;
	desc.DSVFormat = dsvFormat;
	desc.SampleDesc.Count = 1;
	return desc;
}

} // namespace

// ============================================================================
// CubismMaskTarget_D3D12
// ============================================================================
CubismMaskTarget_D3D12::~CubismMaskTarget_D3D12()
{
	Release();
}

bool CubismMaskTarget_D3D12::Create(ID3D12Device* device, UINT width, UINT height)
{
	Release();

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Width = width;
	desc.Height = height;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.Format = MaskFormat;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	D3D12_CLEAR_VALUE clearValue = {};
	clearValue.Format = MaskFormat;
	clearValue.Color[0] = clearValue.Color[1] = clearValue.Color[2] = clearValue.Color[3] = 1.0f;

	_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, _state, &clearValue, IID_PPV_ARGS(&_texture))))
	{
		CubismLogError("[D3D12] Failed to create mask texture.");
		return false;
	}

	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.NumDescriptors = 1;
	if (FAILED(device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&_rtvHeap))))
	{
		CubismLogError("[D3D12] Failed to create mask RTV heap.");
		return false;
	}
	device->CreateRenderTargetView(_texture.Get(), nullptr, _rtvHeap->GetCPUDescriptorHandleForHeapStart());

	_srvIndex = Descriptor::Singleton()->Allocate();
	if (_srvIndex == UINT_MAX)
	{
		CubismLogError("[D3D12] Descriptor heap is full.");
		return false;
	}

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = MaskFormat;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Texture2D.MipLevels = 1;
	device->CreateShaderResourceView(_texture.Get(), &srvDesc, Descriptor::Singleton()->GetCpuHandle(_srvIndex));

	_width = width;
	_height = height;
	return true;
}

void CubismMaskTarget_D3D12::Release()
{
	if (_srvIndex != UINT_MAX)
	{
		Descriptor::Singleton()->Free(_srvIndex);
		_srvIndex = UINT_MAX;
	}
	_rtvHeap.Reset();
	_texture.Reset();
	_width = 0;
	_height = 0;
}

void CubismMaskTarget_D3D12::BeginDraw(ID3D12GraphicsCommandList* commandList)
{
	if (_state != D3D12_RESOURCE_STATE_RENDER_TARGET)
	{
		D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(_texture.Get(), _state, D3D12_RESOURCE_STATE_RENDER_TARGET);
		commandList->ResourceBarrier(1, &barrier);
		_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
	}

	D3D12_CPU_DESCRIPTOR_HANDLE rtv = _rtvHeap->GetCPUDescriptorHandleForHeapStart();
	commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

	D3D12_VIEWPORT viewport = { 0.0f, 0.0f, static_cast<float>(_width), static_cast<float>(_height), 0.0f, 1.0f };
	D3D12_RECT scissor = { 0, 0, static_cast<LONG>(_width), static_cast<LONG>(_height) };
	commandList->RSSetViewports(1, &viewport);
	commandList->RSSetScissorRects(1, &scissor);
}

void CubismMaskTarget_D3D12::EndDraw(ID3D12GraphicsCommandList* commandList)
{
	if (_state != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE)
	{
		D3D12_RESOURCE_BARRIER barrier = TransitionBarrier(_texture.Get(), _state, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
		commandList->ResourceBarrier(1, &barrier);
		_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	}
}

void CubismMaskTarget_D3D12::Clear(ID3D12GraphicsCommandList* commandList, float r, float g, float b, float a)
{
	const float color[4] = { r, g, b, a };
	commandList->ClearRenderTargetView(_rtvHeap->GetCPUDescriptorHandleForHeapStart(), color, 0, nullptr);
}

D3D12_GPU_DESCRIPTOR_HANDLE CubismMaskTarget_D3D12::GetSrv() const
{
	return Descriptor::Singleton()->GetGpuHandle(_srvIndex);
}

// ============================================================================
// CubismClippingManager_D3D12
// ============================================================================
void CubismClippingManager_D3D12::SetupClippingContext(CubismModel& model, CubismRenderer_D3D12* renderer)
{
	ID3D12GraphicsCommandList* commandList = renderer->_commandList;

	// 全てのクリッピングを用意する
	// 同じクリップ（複数の場合はまとめて１つのクリップ）を使う場合は１度だけ設定する
	csmInt32 usingClipCount = 0;
	for (csmUint32 clipIndex = 0; clipIndex < _clippingContextListForMask.GetSize(); ++clipIndex)
	{
		CubismClippingContext_D3D12* cc = _clippingContextListForMask[clipIndex];

		// このクリップを利用する描画オブジェクト群全体を囲む矩形を計算
		CalcClippedTotalBounds(model, cc, CubismRenderer::DrawableObjectType_Drawable);

		if (cc->_isUsing)
		{
			++usingClipCount;
		}
	}

	if (usingClipCount <= 0)
	{
		return;
	}

	_currentMaskBuffer = renderer->GetMaskTarget(0);
	_currentMaskBuffer->BeginDraw(commandList);

	// 各マスクのレイアウトを決定していく
	SetupLayoutBounds(usingClipCount);

	// マスクのクリアフラグを毎フレーム開始時に初期化
	if (_clearedMaskBufferFlags.GetSize() != static_cast<csmUint32>(_renderTextureCount))
	{
		_clearedMaskBufferFlags.Clear();
		for (csmInt32 i = 0; i < _renderTextureCount; ++i)
		{
			_clearedMaskBufferFlags.PushBack(false);
		}
	}
	else
	{
		for (csmInt32 i = 0; i < _renderTextureCount; ++i)
		{
			_clearedMaskBufferFlags[i] = false;
		}
	}

	// 実際にマスクを生成する
	for (csmUint32 clipIndex = 0; clipIndex < _clippingContextListForMask.GetSize(); ++clipIndex)
	{
		CubismClippingContext_D3D12* clipContext = _clippingContextListForMask[clipIndex];
		csmRectF* allClippedDrawRect = clipContext->_allClippedDrawRect; // このマスクを使う全ての描画オブジェクトの論理座標上の囲み矩形
		csmRectF* layoutBoundsOnTex01 = clipContext->_layoutBounds;      // この中にマスクを収める
		const csmFloat32 MARGIN = 0.05f;
		const csmBool isRightHanded = true;

		CubismMaskTarget_D3D12* maskBuffer = renderer->GetMaskTarget(clipContext->_bufferIndex);
		if (maskBuffer == nullptr)
		{
			continue;
		}

		// 現在のレンダーテクスチャがclipContextのものと異なる場合
		if (_currentMaskBuffer != maskBuffer)
		{
			_currentMaskBuffer->EndDraw(commandList);
			_currentMaskBuffer = maskBuffer;
			_currentMaskBuffer->BeginDraw(commandList);
		}

		// モデル座標上の矩形を、適宜マージンを付けて使う
		_tmpBoundsOnModel.SetRect(allClippedDrawRect);
		_tmpBoundsOnModel.Expand(allClippedDrawRect->Width * MARGIN, allClippedDrawRect->Height * MARGIN);

		const csmFloat32 scaleX = layoutBoundsOnTex01->Width / _tmpBoundsOnModel.Width;
		const csmFloat32 scaleY = layoutBoundsOnTex01->Height / _tmpBoundsOnModel.Height;

		// マスク生成時に使う行列を求める
		CreateMatrixForMask(isRightHanded, layoutBoundsOnTex01, scaleX, scaleY);

		clipContext->_matrixForMask.SetMatrix(_tmpMatrixForMask.GetArray());
		clipContext->_matrixForDraw.SetMatrix(_tmpMatrixForDraw.GetArray());

		const csmInt32 clipDrawCount = clipContext->_clippingIdCount;
		for (csmInt32 i = 0; i < clipDrawCount; ++i)
		{
			const csmInt32 clipDrawIndex = clipContext->_clippingIdList[i];

			// 頂点情報が更新されておらず、信頼性がない場合は描画をパスする
			if (!model.GetDrawableDynamicFlagVertexPositionsDidChange(clipDrawIndex))
			{
				continue;
			}

			renderer->IsCulling(model.GetDrawableCulling(clipDrawIndex) != 0);

			// マスクがクリアされていないなら処理する
			if (!_clearedMaskBufferFlags[clipContext->_bufferIndex])
			{
				// 1が無効（描かれない）領域、0が有効（描かれる）領域
				maskBuffer->Clear(commandList, 1.0f, 1.0f, 1.0f, 1.0f);
				_clearedMaskBufferFlags[clipContext->_bufferIndex] = true;
			}

			renderer->_clippingContextBufferForMask = clipContext;
			renderer->DrawMesh(model, clipDrawIndex);
		}
	}

	// --- 後処理 ---
	_currentMaskBuffer->EndDraw(commandList);
	renderer->_clippingContextBufferForMask = nullptr;
}

// ============================================================================
// CubismClippingContext_D3D12
// ============================================================================
CubismClippingContext_D3D12::CubismClippingContext_D3D12(ManagerType* manager, CubismModel& model, const csmInt32* clippingDrawableIndices, csmInt32 clipCount)
	: CubismClippingContext(clippingDrawableIndices, clipCount)
	, _owner(manager)
{
	_isUsing = false;
}

// ============================================================================
// CubismRenderer（D3D12 版の生成関数）
// ============================================================================
CubismRenderer* CubismRenderer::Create(csmUint32 width, csmUint32 height)
{
	return CSM_NEW CubismRenderer_D3D12(width, height);
}

void CubismRenderer::StaticRelease()
{
	CubismRenderer_D3D12::ReleaseShared();
}

// ============================================================================
// CubismRenderer_D3D12
// ============================================================================
bool CubismRenderer_D3D12::InitializeShared(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, DXGI_FORMAT depthStencilFormat, UINT frameCount)
{
	ReleaseShared();

	if (device == nullptr)
	{
		return false;
	}
	g_shared.device = device;
	g_shared.frameCount = frameCount > 0 ? frameCount : 2;

	if (!CreateRootSignature(device))
	{
		CubismLogError("[D3D12] Failed to create root signature.");
		ReleaseShared();
		return false;
	}

	// ---- シェーダー ----
	ComPtr<ID3DBlob> vsMask = CompileShader("VSSetupMask", "vs_5_0", false, false, false);
	ComPtr<ID3DBlob> psMask = CompileShader("PSSetupMask", "ps_5_0", false, false, false);
	ComPtr<ID3DBlob> vsMain = CompileShader("VSMain", "vs_5_0", false, false, false);
	ComPtr<ID3DBlob> psMain[Mask_Count][Alpha_Count];
	for (int mask = 0; mask < Mask_Count; ++mask)
	{
		for (int alpha = 0; alpha < Alpha_Count; ++alpha)
		{
			psMain[mask][alpha] = CompileShader("PSMain", "ps_5_0", mask != Mask_None, mask == Mask_Inverted, alpha == Alpha_Premultiplied);
			if (!psMain[mask][alpha])
			{
				ReleaseShared();
				return false;
			}
		}
	}
	if (!vsMask || !psMask || !vsMain)
	{
		ReleaseShared();
		return false;
	}

	// ---- マスク生成用 PSO ----
	const D3D12_RENDER_TARGET_BLEND_DESC maskBlend = MakeBlend(D3D12_BLEND_ZERO, D3D12_BLEND_INV_SRC_COLOR, D3D12_BLEND_ZERO, D3D12_BLEND_INV_SRC_ALPHA);
	for (int cull = 0; cull < Cull_Count; ++cull)
	{
		D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakePsoDesc(vsMask.Get(), psMask.Get(), static_cast<CullType>(cull), maskBlend,
			MaskFormat, DXGI_FORMAT_UNKNOWN);
		if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&g_shared.maskPso[cull]))))
		{
			CubismLogError("[D3D12] Failed to create mask PSO.");
			ReleaseShared();
			return false;
		}
	}

	// ---- 通常描画用 PSO ----
	for (int blend = 0; blend < Blend_Count; ++blend)
	{
		const D3D12_RENDER_TARGET_BLEND_DESC blendDesc = GetBlendDesc(static_cast<BlendType>(blend));
		for (int cull = 0; cull < Cull_Count; ++cull)
		{
			for (int mask = 0; mask < Mask_Count; ++mask)
			{
				for (int alpha = 0; alpha < Alpha_Count; ++alpha)
				{
					D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = MakePsoDesc(vsMain.Get(), psMain[mask][alpha].Get(), static_cast<CullType>(cull), blendDesc,
						renderTargetFormat, depthStencilFormat);
					if (FAILED(device->CreateGraphicsPipelineState(&desc, IID_PPV_ARGS(&g_shared.drawPso[blend][cull][mask][alpha]))))
					{
						CubismLogError("[D3D12] Failed to create draw PSO.");
						ReleaseShared();
						return false;
					}
				}
			}
		}
	}

	g_shared.initialized = true;
	return true;
}

void CubismRenderer_D3D12::ReleaseShared()
{
	for (auto& pso : g_shared.maskPso)
	{
		pso.Reset();
	}
	for (auto& b : g_shared.drawPso)
		for (auto& c : b)
			for (auto& m : c)
				for (auto& pso : m)
					pso.Reset();
	g_shared.rootSignature.Reset();
	g_shared.device.Reset();
	g_shared.initialized = false;
}

CubismRenderer_D3D12::CubismRenderer_D3D12(csmUint32 width, csmUint32 height)
	: CubismRenderer(width, height)
{
}

CubismRenderer_D3D12::~CubismRenderer_D3D12()
{
	if (_clippingManager != nullptr)
	{
		CSM_DELETE(_clippingManager);
	}
	_clippingManager = nullptr;

	for (CubismMaskTarget_D3D12* target : _maskTargets)
	{
		delete target;
	}
	_maskTargets.clear();

	for (UploadSlot& slot : _uploadSlots)
	{
		if (slot.buffer && slot.cpu)
		{
			slot.buffer->Unmap(0, nullptr);
		}
	}
	_uploadSlots.clear();
}

void CubismRenderer_D3D12::Initialize(CubismModel* model)
{
	Initialize(model, 1);
}

void CubismRenderer_D3D12::Initialize(CubismModel* model, csmInt32 maskBufferCount)
{
	if (!g_shared.initialized)
	{
		CubismLogError("[D3D12] CubismRenderer_D3D12::InitializeShared() has not been called.");
		return;
	}

	if (maskBufferCount < 1)
	{
		maskBufferCount = 1;
	}

	if (model->IsUsingMasking())
	{
		_clippingManager = CSM_NEW CubismClippingManager_D3D12();
		_clippingManager->Initialize(*model, maskBufferCount, DrawableObjectType_Drawable);

		// マスク用テクスチャ1枚に入るマスク数を超える場合は、必要な枚数に増やして作り直す
		// （1枚なら36個まで、2枚以上なら1枚あたり32個まで）
		const csmInt32 maskCount = static_cast<csmInt32>(_clippingManager->GetClippingContextCount());
		csmInt32 requiredCount = 1;
		if (maskCount > ClippingMaskMaxCountOnDefault)
		{
			requiredCount = (maskCount + ClippingMaskMaxCountOnMultiRenderTexture - 1) / ClippingMaskMaxCountOnMultiRenderTexture;
		}
		if (requiredCount > maskBufferCount)
		{
			CubismLogInfo("[D3D12] Mask count %d needs %d mask textures.", maskCount, requiredCount);
			CSM_DELETE(_clippingManager);
			maskBufferCount = requiredCount;
			_clippingManager = CSM_NEW CubismClippingManager_D3D12();
			_clippingManager->Initialize(*model, maskBufferCount, DrawableObjectType_Drawable);
		}

		// 既定の 256 だとマスクの縁が粗くなるため大きめにする
		_clippingManager->SetClippingMaskBufferSize(static_cast<csmFloat32>(DefaultMaskBufferSize), static_cast<csmFloat32>(DefaultMaskBufferSize));
		CreateMaskTargets();
	}

	CubismRenderer::Initialize(model, maskBufferCount);

	if (model->GetOffscreenCount() > 0 && !_warnedOffscreen)
	{
		CubismLogWarning("[D3D12] This model uses offscreens (Cubism 5.3+). They are not supported and parts are drawn directly.");
		_warnedOffscreen = true;
	}

	const csmInt32 drawableCount = model->GetDrawableCount();
	_uploadedVertices.assign(drawableCount, 0);
	_sortedDrawableIndices.assign(drawableCount + model->GetOffscreenCount(), -1);

	CreateIndexBuffer();

	// 1フレームで必要なアップロード量（頂点 + 描画回数分の定数）を見積もる
	UINT64 vertexBytes = 0;
	UINT64 drawCount = drawableCount;
	const csmInt32* maskCounts = model->GetDrawableMaskCounts();
	for (csmInt32 i = 0; i < drawableCount; ++i)
	{
		vertexBytes += AlignUp(sizeof(Vertex) * model->GetDrawableVertexCount(i), 16);
		drawCount += maskCounts[i];
	}
	const UINT64 capacity = vertexBytes + drawCount * AlignUp(sizeof(ConstantBuffer), ConstantBufferAlignment) + 64 * 1024;

	_uploadSlots.clear();
	_uploadSlots.resize(g_shared.frameCount + 1);
	for (UploadSlot& slot : _uploadSlots)
	{
		CreateUploadBuffer(slot, capacity);
	}
	_currentSlot = 0;
}

void CubismRenderer_D3D12::BindTexture(csmUint32 modelTextureIndex, D3D12_GPU_DESCRIPTOR_HANDLE srv)
{
	if (_textures.size() <= modelTextureIndex)
	{
		_textures.resize(modelTextureIndex + 1, D3D12_GPU_DESCRIPTOR_HANDLE{ 0 });
	}
	_textures[modelTextureIndex] = srv;
}

void CubismRenderer_D3D12::SetDrawContext(ID3D12GraphicsCommandList* commandList, const CubismRenderTargetBinding_D3D12& target)
{
	_commandList = commandList;
	_mainTarget = target;
}

void CubismRenderer_D3D12::SetClippingMaskBufferSize(csmFloat32 width, csmFloat32 height)
{
	if (_clippingManager == nullptr)
	{
		return;
	}
	_clippingManager->SetClippingMaskBufferSize(width, height);
	CreateMaskTargets();
}

bool CubismRenderer_D3D12::CreateUploadBuffer(UploadSlot& slot, UINT64 size)
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
	if (FAILED(g_shared.device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&buffer))))
	{
		CubismLogError("[D3D12] Failed to create upload buffer.");
		return false;
	}

	void* mapped = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	if (FAILED(buffer->Map(0, &readRange, &mapped)))
	{
		CubismLogError("[D3D12] Failed to map upload buffer.");
		return false;
	}

	slot.buffer = buffer;
	slot.cpu = static_cast<UINT8*>(mapped);
	slot.gpu = buffer->GetGPUVirtualAddress();
	slot.size = size;
	slot.offset = 0;
	return true;
}

bool CubismRenderer_D3D12::AllocateUpload(UINT64 size, UINT64 alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu)
{
	if (_uploadSlots.empty())
	{
		return false;
	}

	UploadSlot& slot = _uploadSlots[_currentSlot];
	UINT64 offset = AlignUp(slot.offset, alignment);
	if (slot.buffer == nullptr || offset + size > slot.size)
	{
		// 容量不足：古いバッファはこのフレームの描画で使っているので、スロットが再利用されるまで保持する
		if (slot.buffer)
		{
			slot.retired.push_back(slot.buffer);
		}
		const UINT64 newSize = (slot.size * 2 > size + alignment) ? slot.size * 2 : AlignUp(size + alignment, 64 * 1024);
		CubismLogWarning("[D3D12] Upload buffer is too small. Growing to %llu bytes.", newSize);
		if (!CreateUploadBuffer(slot, newSize))
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

bool CubismRenderer_D3D12::CreateIndexBuffer()
{
	CubismModel* model = GetModel();
	const csmInt32 drawableCount = model->GetDrawableCount();

	_indexOffsets.assign(drawableCount, 0);
	UINT64 total = 0;
	for (csmInt32 i = 0; i < drawableCount; ++i)
	{
		_indexOffsets[i] = total;
		total += AlignUp(sizeof(csmUint16) * model->GetDrawableVertexIndexCount(i), 4);
	}
	if (total == 0)
	{
		return true;
	}

	// インデックスは変化しないのでアップロードヒープに一度だけ書き込む
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = total;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	if (FAILED(g_shared.device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_indexBuffer))))
	{
		CubismLogError("[D3D12] Failed to create index buffer.");
		return false;
	}

	UINT8* mapped = nullptr;
	D3D12_RANGE readRange = { 0, 0 };
	if (FAILED(_indexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))))
	{
		return false;
	}
	for (csmInt32 i = 0; i < drawableCount; ++i)
	{
		const csmInt32 count = model->GetDrawableVertexIndexCount(i);
		if (count > 0)
		{
			std::memcpy(mapped + _indexOffsets[i], model->GetDrawableVertexIndices(i), sizeof(csmUint16) * count);
		}
	}
	_indexBuffer->Unmap(0, nullptr);
	return true;
}

bool CubismRenderer_D3D12::CreateMaskTargets()
{
	if (_clippingManager == nullptr)
	{
		return true;
	}

	const UINT width = static_cast<UINT>(_clippingManager->GetClippingMaskBufferSize().X);
	const UINT height = static_cast<UINT>(_clippingManager->GetClippingMaskBufferSize().Y);
	const csmInt32 count = _clippingManager->GetRenderTextureCount();

	while (static_cast<csmInt32>(_maskTargets.size()) < count)
	{
		_maskTargets.push_back(new CubismMaskTarget_D3D12());
	}

	bool result = true;
	for (csmInt32 i = 0; i < count; ++i)
	{
		CubismMaskTarget_D3D12* target = _maskTargets[i];
		if (target->GetWidth() != width || target->GetHeight() != height)
		{
			result &= target->Create(g_shared.device.Get(), width, height);
		}
	}
	return result;
}

CubismMaskTarget_D3D12* CubismRenderer_D3D12::GetMaskTarget(csmInt32 index)
{
	if (index < 0 || index >= static_cast<csmInt32>(_maskTargets.size()))
	{
		return nullptr;
	}
	return _maskTargets[index];
}

void CubismRenderer_D3D12::RestoreMainTarget()
{
	_commandList->OMSetRenderTargets(1, &_mainTarget.rtv, FALSE, _mainTarget.hasDsv ? &_mainTarget.dsv : nullptr);
	_commandList->RSSetViewports(1, &_mainTarget.viewport);
	_commandList->RSSetScissorRects(1, &_mainTarget.scissor);
}

void CubismRenderer_D3D12::DoDrawModel()
{
	if (_commandList == nullptr || !g_shared.initialized || _uploadSlots.empty())
	{
		return;
	}

	CubismModel* model = GetModel();

	// ---- フレーム開始：アップロード用スロットを進める ----
	_currentSlot = (_currentSlot + 1) % static_cast<UINT>(_uploadSlots.size());
	_uploadSlots[_currentSlot].offset = 0;
	_uploadSlots[_currentSlot].retired.clear();
	std::fill(_uploadedVertices.begin(), _uploadedVertices.end(), 0);

	_commandList->SetGraphicsRootSignature(g_shared.rootSignature.Get());
	_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// マスクが無いモデルでも、指定された描画先（オフスクリーンなど）に描く
	RestoreMainTarget();

	// ---- クリッピングマスクの生成 ----
	if (_clippingManager != nullptr)
	{
		CreateMaskTargets();

		if (IsUsingHighPrecisionMask())
		{
			// 高精細マスク：マスクは描画するパーツごとに生成する
			_clippingManager->SetupMatrixForHighPrecision(*model, true, DrawableObjectType_Drawable);
		}
		else
		{
			_clippingManager->SetupClippingContext(*model, this);
			RestoreMainTarget();
		}
	}

	// ---- 描画順に並べる（オフスクリーンは未対応なので Drawable のみ） ----
	const csmInt32 drawableCount = model->GetDrawableCount();
	const csmInt32* renderOrder = model->GetRenderOrders();
	std::fill(_sortedDrawableIndices.begin(), _sortedDrawableIndices.end(), -1);
	for (csmInt32 i = 0; i < drawableCount; ++i)
	{
		const csmInt32 order = renderOrder[i];
		if (order >= 0 && order < static_cast<csmInt32>(_sortedDrawableIndices.size()))
		{
			_sortedDrawableIndices[order] = i;
		}
	}

	for (csmInt32 drawableIndex : _sortedDrawableIndices)
	{
		if (drawableIndex >= 0)
		{
			DrawDrawable(*model, drawableIndex);
		}
	}

	_commandList = nullptr;
}

void CubismRenderer_D3D12::DrawDrawable(CubismModel& model, csmInt32 drawableIndex)
{
	if (!model.GetDrawableDynamicFlagIsVisible(drawableIndex))
	{
		return;
	}

	CubismClippingContext_D3D12* clipContext = (_clippingManager != nullptr)
		? (*_clippingManager->GetClippingContextListForDraw())[drawableIndex]
		: nullptr;

	// 高精細マスク：このパーツ用のマスクをここで生成する
	if (clipContext != nullptr && IsUsingHighPrecisionMask() && clipContext->_isUsing)
	{
		CubismMaskTarget_D3D12* maskTarget = GetMaskTarget(clipContext->_bufferIndex);
		if (maskTarget != nullptr)
		{
			maskTarget->BeginDraw(_commandList);
			maskTarget->Clear(_commandList, 1.0f, 1.0f, 1.0f, 1.0f);

			for (csmInt32 i = 0; i < clipContext->_clippingIdCount; ++i)
			{
				const csmInt32 clipDrawIndex = clipContext->_clippingIdList[i];
				if (!model.GetDrawableDynamicFlagVertexPositionsDidChange(clipDrawIndex))
				{
					continue;
				}
				IsCulling(model.GetDrawableCulling(clipDrawIndex) != 0);
				_clippingContextBufferForMask = clipContext;
				DrawMesh(model, clipDrawIndex);
			}

			maskTarget->EndDraw(_commandList);
			_clippingContextBufferForMask = nullptr;
			RestoreMainTarget();
		}
	}

	_clippingContextBufferForDraw = clipContext;
	IsCulling(model.GetDrawableCulling(drawableIndex) != 0);
	DrawMesh(model, drawableIndex);
}

D3D12_GPU_DESCRIPTOR_HANDLE CubismRenderer_D3D12::GetTextureSrv(const CubismModel& model, csmInt32 drawableIndex) const
{
	const csmInt32 textureIndex = model.GetDrawableTextureIndex(drawableIndex);
	if (textureIndex < 0 || textureIndex >= static_cast<csmInt32>(_textures.size()))
	{
		return D3D12_GPU_DESCRIPTOR_HANDLE{ 0 };
	}
	return _textures[textureIndex];
}

bool CubismRenderer_D3D12::UploadVertices(const CubismModel& model, csmInt32 drawableIndex, D3D12_VERTEX_BUFFER_VIEW* view)
{
	const csmInt32 vertexCount = model.GetDrawableVertexCount(drawableIndex);
	if (vertexCount <= 0)
	{
		return false;
	}
	const UINT size = static_cast<UINT>(sizeof(Vertex) * vertexCount);

	// マスクと本描画で同じ頂点を使うので、1フレームに1回だけ転送する
	if (_uploadedVertices[drawableIndex] == 0)
	{
		void* cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
		if (!AllocateUpload(size, 16, &cpu, &gpu))
		{
			return false;
		}

		const csmFloat32* positions = model.GetDrawableVertices(drawableIndex);
		const Core::csmVector2* uvs = model.GetDrawableVertexUvs(drawableIndex);
		Vertex* dst = static_cast<Vertex*>(cpu);
		for (csmInt32 i = 0; i < vertexCount; ++i)
		{
			dst[i].x = positions[i * 2 + 0];
			dst[i].y = positions[i * 2 + 1];
			dst[i].u = uvs[i].X;
			dst[i].v = uvs[i].Y;
		}
		_uploadedVertices[drawableIndex] = gpu;
	}

	view->BufferLocation = _uploadedVertices[drawableIndex];
	view->SizeInBytes = size;
	view->StrideInBytes = sizeof(Vertex);
	return true;
}

void CubismRenderer_D3D12::DrawMesh(CubismModel& model, csmInt32 drawableIndex)
{
	const csmInt32 indexCount = model.GetDrawableVertexIndexCount(drawableIndex);
	const bool generatingMask = IsGeneratingMask();

	auto finish = [this]()
	{
		_clippingContextBufferForDraw = nullptr;
		_clippingContextBufferForMask = nullptr;
	};

	// 描画物無し / 透明 / テクスチャ未設定 ならスキップ
	if (indexCount == 0 || _indexBuffer == nullptr)
	{
		finish();
		return;
	}
	if (model.GetDrawableOpacity(drawableIndex) <= 0.0f && !generatingMask)
	{
		finish();
		return;
	}
	const D3D12_GPU_DESCRIPTOR_HANDLE textureSrv = GetTextureSrv(model, drawableIndex);
	if (textureSrv.ptr == 0)
	{
		finish();
		return;
	}

	// ---- 頂点 ----
	D3D12_VERTEX_BUFFER_VIEW vbv = {};
	if (!UploadVertices(model, drawableIndex, &vbv))
	{
		finish();
		return;
	}

	D3D12_INDEX_BUFFER_VIEW ibv = {};
	ibv.BufferLocation = _indexBuffer->GetGPUVirtualAddress() + _indexOffsets[drawableIndex];
	ibv.SizeInBytes = static_cast<UINT>(sizeof(csmUint16) * indexCount);
	ibv.Format = DXGI_FORMAT_R16_UINT;

	// ---- 定数 ----
	void* cbCpu = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS cbGpu = 0;
	if (!AllocateUpload(sizeof(ConstantBuffer), ConstantBufferAlignment, &cbCpu, &cbGpu))
	{
		finish();
		return;
	}
	ConstantBuffer cb = {};

	const CullType cull = IsCulling() ? Cull_Back : Cull_None;
	ID3D12PipelineState* pso = nullptr;
	D3D12_GPU_DESCRIPTOR_HANDLE maskSrv = textureSrv; // 未使用時もテーブルは設定しておく

	if (generatingMask)
	{
		// マスク生成
		CubismClippingContext_D3D12* clip = _clippingContextBufferForMask;
		const csmInt32 channelIndex = clip->_layoutChannelIndex;
		CubismTextureColor* channel = clip->GetClippingManager()->GetChannelFlagAsColor(channelIndex);
		cb.channelFlag[0] = channel->R;
		cb.channelFlag[1] = channel->G;
		cb.channelFlag[2] = channel->B;
		cb.channelFlag[3] = channel->A;

		// マスクを書き込む範囲（NDC）
		const csmRectF* rect = clip->_layoutBounds;
		cb.baseColor[0] = rect->X * 2.0f - 1.0f;
		cb.baseColor[1] = rect->Y * 2.0f - 1.0f;
		cb.baseColor[2] = rect->GetRight() * 2.0f - 1.0f;
		cb.baseColor[3] = rect->GetBottom() * 2.0f - 1.0f;

		std::memcpy(cb.projectMatrix, clip->_matrixForMask.GetArray(), sizeof(cb.projectMatrix));

		pso = g_shared.maskPso[cull].Get();
	}
	else
	{
		// 通常描画
		CubismClippingContext_D3D12* clip = _clippingContextBufferForDraw;
		MaskType maskType = Mask_None;
		if (clip != nullptr)
		{
			CubismMaskTarget_D3D12* maskTarget = GetMaskTarget(clip->_bufferIndex);
			if (maskTarget != nullptr)
			{
				maskType = model.GetDrawableInvertedMask(drawableIndex) ? Mask_Inverted : Mask_Normal;
				maskSrv = maskTarget->GetSrv();

				std::memcpy(cb.clipMatrix, clip->_matrixForDraw.GetArray(), sizeof(cb.clipMatrix));

				CubismTextureColor* channel = clip->GetClippingManager()->GetChannelFlagAsColor(clip->_layoutChannelIndex);
				cb.channelFlag[0] = channel->R;
				cb.channelFlag[1] = channel->G;
				cb.channelFlag[2] = channel->B;
				cb.channelFlag[3] = channel->A;
			}
		}

		const CubismTextureColor baseColor = GetModelColorWithOpacity(model.GetDrawableOpacity(drawableIndex));
		const CubismModelMultiplyAndScreenColor& colors = model.GetOverrideMultiplyAndScreenColor();
		const CubismTextureColor multiplyColor = colors.GetDrawableMultiplyColor(drawableIndex);
		const CubismTextureColor screenColor = colors.GetDrawableScreenColor(drawableIndex);
		cb.baseColor[0] = baseColor.R;     cb.baseColor[1] = baseColor.G;     cb.baseColor[2] = baseColor.B;     cb.baseColor[3] = baseColor.A;
		cb.multiplyColor[0] = multiplyColor.R; cb.multiplyColor[1] = multiplyColor.G; cb.multiplyColor[2] = multiplyColor.B; cb.multiplyColor[3] = multiplyColor.A;
		cb.screenColor[0] = screenColor.R; cb.screenColor[1] = screenColor.G; cb.screenColor[2] = screenColor.B; cb.screenColor[3] = screenColor.A;

		CubismMatrix44 mvp = GetMvpMatrix();
		std::memcpy(cb.projectMatrix, mvp.GetArray(), sizeof(cb.projectMatrix));

		// ブレンドモード（5.3 の拡張ブレンドは近いものに置き換える）
		BlendType blend = Blend_Normal;
		switch (model.GetDrawableBlendModeType(drawableIndex).GetColorBlendType())
		{
		case Core::csmColorBlendType_AddCompatible:
		case Core::csmColorBlendType_Add:
			blend = Blend_Add;
			break;
		case Core::csmColorBlendType_MultiplyCompatible:
		case Core::csmColorBlendType_Multiply:
			blend = Blend_Mult;
			break;
		default:
			blend = Blend_Normal;
			break;
		}

		const AlphaType alpha = IsPremultipliedAlpha() ? Alpha_Premultiplied : Alpha_Straight;
		pso = g_shared.drawPso[blend][cull][maskType][alpha].Get();
	}

	std::memcpy(cbCpu, &cb, sizeof(cb));

	// ---- 描画 ----
	_commandList->SetPipelineState(pso);
	_commandList->SetGraphicsRootConstantBufferView(Root_Constants, cbGpu);
	_commandList->SetGraphicsRootDescriptorTable(Root_MainTexture, textureSrv);
	_commandList->SetGraphicsRootDescriptorTable(Root_MaskTexture, maskSrv);
	_commandList->IASetVertexBuffers(0, 1, &vbv);
	_commandList->IASetIndexBuffer(&ibv);
	_commandList->DrawIndexedInstanced(indexCount, 1, 0, 0, 0);

	finish();
}

}}}}
