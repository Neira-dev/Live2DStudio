#include "FloorGrid.h"

#include <Windows.h>
#include <d3dcompiler.h>
#include <cstring>
#include <string>
#include <vector>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace
{
const char ShaderSource[] = R"(
cbuffer GridConstants : register(b0)
{
	float4x4 viewProjection;
	float3 cameraPosition;
	float fadeDistance;
};

struct VSInput
{
	float3 position : POSITION;
	float4 color : COLOR;
};

struct VSOutput
{
	float4 position : SV_POSITION;
	float4 color : COLOR;
	float3 world : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
	VSOutput o;
	o.position = mul(viewProjection, float4(input.position, 1.0));
	o.color = input.color;
	o.world = input.position;
	return o;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
	// カメラから離れるほど薄くする
	float distance = length(input.world.xz - cameraPosition.xz);
	float fade = 1.0 - smoothstep(fadeDistance * 0.4, fadeDistance, distance);
	return float4(input.color.rgb, input.color.a * fade);
}
)";

struct Vertex
{
	float x, y, z;
	float r, g, b, a;
};

struct Constants
{
	float viewProjection[16];
	float cameraPosition[3];
	float fadeDistance;
};
static_assert(sizeof(Constants) == 80, "root constants size");

void LogError(const char* message, ID3DBlob* error = nullptr)
{
	std::string text = "[FloorGrid] ";
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
	if (FAILED(D3DCompile(ShaderSource, sizeof(ShaderSource) - 1, "FloorGrid", nullptr, nullptr, entry, target, flags, 0, &blob, &error)))
	{
		LogError("Shader compile failed", error.Get());
		return nullptr;
	}
	return blob;
}

void AddLine(std::vector<Vertex>& vertices, float x0, float z0, float x1, float z1, const float color[4])
{
	vertices.push_back({ x0, 0.0f, z0, color[0], color[1], color[2], color[3] });
	vertices.push_back({ x1, 0.0f, z1, color[0], color[1], color[2], color[3] });
}
} // namespace

bool FloorGrid::Initialize(const Desc& desc)
{
	ID3D12Device* device = desc.device;
	if (device == nullptr || desc.halfLineCount <= 0 || desc.spacing <= 0.0f)
	{
		return false;
	}
	_fadeDistance = desc.fadeDistance;

	// ---- ルートシグネチャ：b0 = 定数（ビュー × プロジェクション、カメラ位置） ----
	D3D12_ROOT_PARAMETER param = {};
	param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	param.Constants.ShaderRegister = 0;
	param.Constants.Num32BitValues = sizeof(Constants) / 4;
	param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_ROOT_SIGNATURE_DESC rootDesc = {};
	rootDesc.NumParameters = 1;
	rootDesc.pParameters = &param;
	rootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	ComPtr<ID3DBlob> rootBlob;
	ComPtr<ID3DBlob> error;
	if (FAILED(D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &error)))
	{
		LogError("Failed to serialize root signature", error.Get());
		return false;
	}
	if (FAILED(device->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(), IID_PPV_ARGS(&_rootSignature))))
	{
		LogError("Failed to create root signature");
		return false;
	}

	// ---- PSO ----
	ComPtr<ID3DBlob> vs = CompileShader("VSMain", "vs_5_0");
	ComPtr<ID3DBlob> ps = CompileShader("PSMain", "ps_5_0");
	if (!vs || !ps)
	{
		return false;
	}

	const D3D12_INPUT_ELEMENT_DESC inputLayout[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
	pso.pRootSignature = _rootSignature.Get();
	pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
	pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
	D3D12_RENDER_TARGET_BLEND_DESC& blend = pso.BlendState.RenderTarget[0];
	blend.BlendEnable = TRUE;
	blend.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	blend.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	blend.BlendOp = D3D12_BLEND_OP_ADD;
	blend.SrcBlendAlpha = D3D12_BLEND_ONE;
	blend.DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
	blend.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	blend.LogicOp = D3D12_LOGIC_OP_NOOP;
	blend.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	pso.SampleMask = UINT_MAX;
	pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	pso.RasterizerState.DepthClipEnable = TRUE;
	pso.RasterizerState.AntialiasedLineEnable = TRUE;
	pso.DepthStencilState.DepthEnable = TRUE;
	pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
	pso.InputLayout = { inputLayout, _countof(inputLayout) };
	pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	pso.NumRenderTargets = 1;
	pso.RTVFormats[0] = desc.renderTargetFormat;
	pso.DSVFormat = desc.depthStencilFormat;
	pso.SampleDesc.Count = 1;
	if (FAILED(device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&_pso))))
	{
		LogError("Failed to create pipeline state");
		return false;
	}

	// ---- 線の頂点 ----
	const float minorColor[4] = { 0.75f, 0.80f, 0.90f, 0.25f };
	const float majorColor[4] = { 0.85f, 0.90f, 1.00f, 0.50f };
	const float axisXColor[4] = { 0.95f, 0.30f, 0.30f, 0.90f };
	const float axisZColor[4] = { 0.30f, 0.50f, 0.95f, 0.90f };

	std::vector<Vertex> vertices;
	const int n = desc.halfLineCount;
	const float extent = n * desc.spacing;
	for (int i = -n; i <= n; ++i)
	{
		const float p = i * desc.spacing;
		const float* color = (i % 5 == 0) ? majorColor : minorColor;
		AddLine(vertices, p, -extent, p, extent, i == 0 ? axisZColor : color);  // Z 方向の線（X = p）
		AddLine(vertices, -extent, p, extent, p, i == 0 ? axisXColor : color);  // X 方向の線（Z = p）
	}
	_vertexCount = static_cast<UINT>(vertices.size());

	// 一度書くだけなのでアップロードヒープに置いたまま使う
	const UINT bufferSize = static_cast<UINT>(sizeof(Vertex) * vertices.size());
	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
	D3D12_RESOURCE_DESC bufferDesc = {};
	bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferDesc.Width = bufferSize;
	bufferDesc.Height = 1;
	bufferDesc.DepthOrArraySize = 1;
	bufferDesc.MipLevels = 1;
	bufferDesc.SampleDesc.Count = 1;
	bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &bufferDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&_vertexBuffer))))
	{
		LogError("Failed to create vertex buffer");
		return false;
	}
	void* mapped = nullptr;
	const D3D12_RANGE noRead = { 0, 0 };
	if (FAILED(_vertexBuffer->Map(0, &noRead, &mapped)))
	{
		LogError("Failed to map vertex buffer");
		return false;
	}
	std::memcpy(mapped, vertices.data(), bufferSize);
	_vertexBuffer->Unmap(0, nullptr);

	_vertexBufferView.BufferLocation = _vertexBuffer->GetGPUVirtualAddress();
	_vertexBufferView.SizeInBytes = bufferSize;
	_vertexBufferView.StrideInBytes = sizeof(Vertex);
	return true;
}

void FloorGrid::Draw(ID3D12GraphicsCommandList* commandList, const XMMATRIX& viewProjection, const XMFLOAT3& cameraPosition)
{
	if (commandList == nullptr || !_pso)
	{
		return;
	}

	// DirectXMath の行列（行ベクトル用）をそのまま並べると、HLSL の column_major + mul(M, v) で正しく計算される
	Constants constants = {};
	XMFLOAT4X4 matrix;
	XMStoreFloat4x4(&matrix, viewProjection);
	std::memcpy(constants.viewProjection, &matrix, sizeof(constants.viewProjection));
	constants.cameraPosition[0] = cameraPosition.x;
	constants.cameraPosition[1] = cameraPosition.y;
	constants.cameraPosition[2] = cameraPosition.z;
	constants.fadeDistance = _fadeDistance;

	commandList->SetGraphicsRootSignature(_rootSignature.Get());
	commandList->SetPipelineState(_pso.Get());
	commandList->SetGraphicsRoot32BitConstants(0, sizeof(Constants) / 4, &constants, 0);
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	commandList->IASetVertexBuffers(0, 1, &_vertexBufferView);
	commandList->DrawInstanced(_vertexCount, 1, 0, 0);
}

void FloorGrid::Release()
{
	_vertexBuffer.Reset();
	_pso.Reset();
	_rootSignature.Reset();
	_vertexCount = 0;
}
