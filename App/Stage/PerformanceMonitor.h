#pragma once
/*
* PerformanceMonitor
* 配信前チェック用の負荷の計測
*
*   ・GPU：ステージの描画にかかった GPU 時間（タイムスタンプクエリ）。1 フレーム（60fps）に占める割合を使用率とみなす
*   ・CPU：このプロセスの CPU 使用率（全コアに対する割合）
*   ・フレーム：直近 10 秒の fps と、フレーム落ち（1.5 フレーム分以上かかったフレーム）の割合
*/

#include <d3d12.h>
#include <deque>
#include <wrl/client.h>

class PerformanceMonitor
{
public:
	bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
	void Release();

	/**
	 * @brief 計測したい描画の前後で呼ぶ（同じコマンドリスト）
	 */
	void BeginGpu(ID3D12GraphicsCommandList* commandList);
	void EndGpu(ID3D12GraphicsCommandList* commandList);

	/**
	 * @brief 毎フレーム呼ぶ（CPU 使用率とフレーム時間を更新する）
	 */
	void Update(float deltaSeconds);

	float GetGpuMilliseconds() const { return _gpuMilliseconds; }
	float GetGpuUsage() const { return _gpuMilliseconds / (1000.0f / 60.0f); }  // 60fps の 1 フレームに対する割合
	float GetCpuUsage() const { return _cpuUsage; }
	float GetFps() const { return _fps; }
	float GetDropRate() const { return _dropRate; }

private:
	static constexpr UINT SlotCount = 4;  // 同時に処理中になり得るフレーム数より多くする

	Microsoft::WRL::ComPtr<ID3D12QueryHeap> _queryHeap;
	Microsoft::WRL::ComPtr<ID3D12Resource> _readback;
	UINT64 _frequency = 0;
	UINT _slot = 0;
	UINT64 _frameCount = 0;
	bool _began = false;
	float _gpuMilliseconds = 0.0f;

	// CPU
	ULONGLONG _lastProcessTime = 0;
	ULONGLONG _lastWallTime = 0;
	float _cpuUsage = 0.0f;
	float _cpuTimer = 0.0f;

	// フレーム
	std::deque<float> _frameTimes;
	float _fps = 0.0f;
	float _dropRate = 0.0f;
};
