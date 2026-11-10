#pragma once
#include <d3d12.h>

class Scene
{
public:
	Scene() = default;
	~Scene() = default;
public:
	void Init();
	void Update();
	void Draw(ID3D12GraphicsCommandList* commandList);
	void DrawImGui();
	void UnInit();
};

