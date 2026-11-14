#include "PerformanceMonitor.h"

#include <Windows.h>
#include <algorithm>

namespace
{
	ULONGLONG FileTimeToTicks(const FILETIME& time)
	{
		return (static_cast<ULONGLONG>(time.dwHighDateTime) << 32) | time.dwLowDateTime;
	}

	// プロセスの CPU 時間（ユーザー＋カーネル、100ns 単位）
	ULONGLONG GetProcessCpuTicks()
	{
		FILETIME creation, exit, kernel, user;
		if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
		{
			return 0;
		}
		return FileTimeToTicks(kernel) + FileTimeToTicks(user);
	}

	ULONGLONG GetWallTicks()
	{
		FILETIME now;
		GetSystemTimeAsFileTime(&now);
		return FileTimeToTicks(now);
	}
}

bool PerformanceMonitor::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue)
{
	if (device == nullptr || commandQueue == nullptr || FAILED(commandQueue->GetTimestampFrequency(&_frequency)))
	{
		return false;
	}

	D3D12_QUERY_HEAP_DESC heapDesc = {};
	heapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
	heapDesc.Count = SlotCount * 2;
	if (FAILED(device->CreateQueryHeap(&heapDesc, IID_PPV_ARGS(&_queryHeap))))
	{
		return false;
	}

	D3D12_HEAP_PROPERTIES heapProps = {};
	heapProps.Type = D3D12_HEAP_TYPE_READBACK;
	D3D12_RESOURCE_DESC desc = {};
	desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	desc.Width = sizeof(UINT64) * SlotCount * 2;
	desc.Height = 1;
	desc.DepthOrArraySize = 1;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	if (FAILED(device->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&_readback))))
	{
		return false;
	}

	_lastProcessTime = GetProcessCpuTicks();
	_lastWallTime = GetWallTicks();
	return true;
}

void PerformanceMonitor::Release()
{
	_queryHeap.Reset();
	_readback.Reset();
}

void PerformanceMonitor::BeginGpu(ID3D12GraphicsCommandList* commandList)
{
	if (!_queryHeap)
	{
		return;
	}
	_slot = static_cast<UINT>(_frameCount % SlotCount);

	// このスロットを前に使ったフレーム（SlotCount フレーム前）の結果はもう出ている
	if (_frameCount >= SlotCount)
	{
		UINT64* data = nullptr;
		const D3D12_RANGE range = { sizeof(UINT64) * _slot * 2, sizeof(UINT64) * (_slot * 2 + 2) };
		if (SUCCEEDED(_readback->Map(0, &range, reinterpret_cast<void**>(&data))))
		{
			const UINT64 begin = data[_slot * 2];
			const UINT64 end = data[_slot * 2 + 1];
			if (end > begin && _frequency > 0)
			{
				const float milliseconds = static_cast<float>(static_cast<double>(end - begin) * 1000.0 / static_cast<double>(_frequency));
				_gpuMilliseconds += (milliseconds - _gpuMilliseconds) * 0.1f;  // ならす
			}
			const D3D12_RANGE noWrite = { 0, 0 };
			_readback->Unmap(0, &noWrite);
		}
	}

	commandList->EndQuery(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * 2);
	_began = true;
}

void PerformanceMonitor::EndGpu(ID3D12GraphicsCommandList* commandList)
{
	if (!_queryHeap || !_began)
	{
		return;
	}
	commandList->EndQuery(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * 2 + 1);
	commandList->ResolveQueryData(_queryHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, _slot * 2, 2, _readback.Get(), sizeof(UINT64) * _slot * 2);
	_began = false;
	++_frameCount;
}

void PerformanceMonitor::Update(float deltaSeconds)
{
	// ---- CPU（0.5 秒ごと）----
	_cpuTimer += deltaSeconds;
	if (_cpuTimer >= 0.5f)
	{
		_cpuTimer = 0.0f;
		const ULONGLONG process = GetProcessCpuTicks();
		const ULONGLONG wall = GetWallTicks();
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		if (wall > _lastWallTime && info.dwNumberOfProcessors > 0)
		{
			const double usage = static_cast<double>(process - _lastProcessTime) / static_cast<double>(wall - _lastWallTime) / info.dwNumberOfProcessors;
			_cpuUsage = static_cast<float>(std::clamp(usage, 0.0, 1.0));
		}
		_lastProcessTime = process;
		_lastWallTime = wall;
	}

	// ---- フレーム（直近 10 秒）----
	if (deltaSeconds > 0.0f)
	{
		_frameTimes.push_back(deltaSeconds);
	}
	float total = 0.0f;
	for (float t : _frameTimes)
	{
		total += t;
	}
	while (total > 10.0f && !_frameTimes.empty())
	{
		total -= _frameTimes.front();
		_frameTimes.pop_front();
	}
	if (!_frameTimes.empty() && total > 0.0f)
	{
		_fps = static_cast<float>(_frameTimes.size()) / total;
		const float dropThreshold = 1.5f / 60.0f;
		const size_t drops = std::count_if(_frameTimes.begin(), _frameTimes.end(), [dropThreshold](float t) { return t > dropThreshold; });
		_dropRate = static_cast<float>(drops) / static_cast<float>(_frameTimes.size());
	}
}
