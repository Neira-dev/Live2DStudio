#pragma once
/*
* RmlUiLayer
* RmlUi（HTML/CSS 風の UI ライブラリ）の初期化・入力・描画をまとめる
*
*   UI の見た目は App/UI/*.rml と *.rcss に書く（作業ディレクトリ = App フォルダー）。
*   Win32 のメッセージは WndProcHandler で受け取り、RmlUi の入力に変換する。
*/

#include <d3d12.h>
#include <memory>
#include <Windows.h>

namespace Rml
{
	class Context;
}
class RmlRenderer_D3D12;
class RmlSystem_Win32;

class RmlUiLayer
{
public:
	struct Desc
	{
		HWND hwnd = nullptr;
		ID3D12Device* device = nullptr;
		DXGI_FORMAT renderTargetFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
		UINT frameCount = 2;
		UINT width = 1280;
		UINT height = 720;
	};

public:
	RmlUiLayer();
	~RmlUiLayer();
	RmlUiLayer(const RmlUiLayer&) = delete;
	RmlUiLayer& operator=(const RmlUiLayer&) = delete;

	bool Initialize(const Desc& desc);

	/**
	 * @brief 終了処理（GPU の処理が終わってから呼ぶ）
	 */
	void Shutdown();

	Rml::Context* GetContext() const { return _context; }

	/**
	 * @brief レイアウトとアニメーションを更新する（毎フレーム、描画の前）
	 */
	void Update();

	/**
	 * @brief 描画する（バックバッファに描いたものの上に重ねる）
	 */
	void Render(ID3D12GraphicsCommandList* commandList, D3D12_CPU_DESCRIPTOR_HANDLE rtv);

	/**
	 * @brief Win32 のメッセージを RmlUi に渡す（Window::AddWndProc で登録する）
	 */
	bool HandleMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result);

private:
	std::unique_ptr<RmlRenderer_D3D12> _renderer;
	std::unique_ptr<RmlSystem_Win32> _system;
	Rml::Context* _context = nullptr;
	HWND _hwnd = nullptr;
	UINT _width = 0;
	UINT _height = 0;
	bool _initialized = false;
};
