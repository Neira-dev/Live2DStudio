#pragma once
/*
* RmlRenderer_D3D12
* RmlUi の描画を DirectX 12 で行う RenderInterface
*
*   ・ジオメトリは CPU 側に持ち、描くたびにフレームごとのアップロード領域へ書き込む
*     （文字などは頻繁に作り直されるため、GPU のバッファを個別に作らない）
*   ・テクスチャは乗算済みアルファ（RmlUi の仕様）
*   ・クリップマスク・レイヤー・フィルターは未対応（RCSS で使わない）
*/

#include <RmlUi/Core/RenderInterface.h>

#include <climits>
#include <d3d12.h>
#include <vector>
#include <wrl/client.h>

class RmlRenderer_D3D12 : public Rml::RenderInterface
{
public:
	RmlRenderer_D3D12() = default;
	~RmlRenderer_D3D12() override;

	bool Initialize(ID3D12Device* device, DXGI_FORMAT renderTargetFormat, UINT frameCount);
	void Release();

	/**
	 * @brief 描画の開始（Context::Render の直前に呼ぶ）
	 */
	void BeginFrame(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv, UINT width, UINT height);

	/**
	 * @brief 描画の終了（Context::Render の直後に呼ぶ）
	 */
	void EndFrame();

	// ---- Rml::RenderInterface ----
	Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
	void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
	void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

	Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
	Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) override;
	void ReleaseTexture(Rml::TextureHandle texture) override;

	void EnableScissorRegion(bool enable) override;
	void SetScissorRegion(Rml::Rectanglei region) override;
	void SetTransform(const Rml::Matrix4f* transform) override;

private:
	struct Geometry
	{
		std::vector<Rml::Vertex> vertices;
		std::vector<int> indices;
	};

	struct Texture
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12Resource> upload;  // 転送が終わるまで保持
		UINT srvIndex = UINT_MAX;
		UINT width = 0;
		UINT height = 0;
		bool uploaded = false;
	};

	// フレームごとのアップロード領域（GPU が使い終わるまで再利用しない）
	struct FrameSlot
	{
		Microsoft::WRL::ComPtr<ID3D12Resource> buffer;
		UINT8* cpu = nullptr;
		D3D12_GPU_VIRTUAL_ADDRESS gpu = 0;
		UINT64 size = 0;
		UINT64 offset = 0;
		std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> retired;  // このスロットが再利用されるときに解放する
		std::vector<Texture*> retiredTextures;
	};

	bool AllocateFrameMemory(UINT64 size, UINT64 alignment, void** cpu, D3D12_GPU_VIRTUAL_ADDRESS* gpu);
	bool CreateFrameBuffer(FrameSlot& slot, UINT64 size);
	Texture* CreateTexture(const Rml::byte* pixels, UINT width, UINT height);
	void RecordUpload(Texture& texture);
	void ApplyScissor();

private:
	ID3D12Device* _device = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> _rootSignature;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _colorPso;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> _texturePso;

	std::vector<FrameSlot> _slots;
	UINT _currentSlot = 0;
	std::vector<Texture*> _pendingUploads;  // 描画の外で作られ、まだ転送していないテクスチャ

	ID3D12GraphicsCommandList* _commandList = nullptr;
	UINT _width = 0;
	UINT _height = 0;
	bool _scissorEnabled = false;
	Rml::Rectanglei _scissor;
	Rml::Matrix4f _projection;
	Rml::Matrix4f _transform;
};
