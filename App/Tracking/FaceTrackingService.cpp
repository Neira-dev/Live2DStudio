#include "FaceTrackingService.h"

#include <winrt/base.h>

#include <chrono>

#define U8(text) reinterpret_cast<const char*>(u8##text)

FaceTrackingService::FaceTrackingService(std::filesystem::path modelDirectory)
	: _modelDirectory(std::move(modelDirectory))
{
}

FaceTrackingService::~FaceTrackingService()
{
	Stop();
}

const std::vector<CameraDevice>& FaceTrackingService::RefreshDevices()
{
	_devices = CameraCapture::EnumerateDevices();
	return _devices;
}

void FaceTrackingService::Start(int deviceIndex)
{
	Stop();
	if (deviceIndex < 0 || deviceIndex >= static_cast<int>(_devices.size()))
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_error = U8("カメラが見つかりません");
		_status = TrackingStatus::Error;
		return;
	}
	_deviceIndex = deviceIndex;
	_stop = false;
	_status = TrackingStatus::Starting;
	_thread = std::thread(&FaceTrackingService::Run, this, _devices[deviceIndex]);
}

void FaceTrackingService::Stop()
{
	_stop = true;
	if (_thread.joinable())
	{
		_thread.join();
	}
	_status = TrackingStatus::Stopped;
	std::lock_guard<std::mutex> lock(_mutex);
	_latest = FaceTrackingResult();
	_error.clear();
}

std::string FaceTrackingService::GetError() const
{
	std::lock_guard<std::mutex> lock(_mutex);
	return _error;
}

bool FaceTrackingService::GetLatest(FaceTrackingResult& result, unsigned long long& frameId) const
{
	std::lock_guard<std::mutex> lock(_mutex);
	const bool updated = frameId != _frameId;
	result = _latest;
	frameId = _frameId;
	return updated;
}

void FaceTrackingService::Run(CameraDevice device)
{
	// このスレッドで WinRT（Windows ML）と Media Foundation を使う
	winrt::init_apartment(winrt::apartment_type::multi_threaded);

	auto fail = [this](const std::string& message)
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_error = message;
		_status = TrackingStatus::Error;
	};

	FaceTracker tracker;
	std::string error;
	if (!tracker.Initialize(_modelDirectory, error))
	{
		fail(std::string(U8("モデルを読み込めません（Tracking/Models）: ")) + error);
		winrt::uninit_apartment();
		return;
	}

	CameraCapture camera;
	if (!camera.Open(device, error))
	{
		fail(std::string(U8("カメラを開けません: ")) + error);
		winrt::uninit_apartment();
		return;
	}

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_error.clear();
	}
	_status = TrackingStatus::Running;

	std::vector<uint8_t> pixels;
	int width = 0, height = 0;
	auto last = std::chrono::steady_clock::now();
	while (!_stop)
	{
		if (!camera.ReadFrame(pixels, width, height))
		{
			fail(U8("カメラの映像が途切れました"));
			break;
		}
		if (pixels.empty())
		{
			continue;
		}

		const FaceTrackingResult result = tracker.Process(pixels.data(), width, height, width * 4);

		const auto now = std::chrono::steady_clock::now();
		const float dt = std::chrono::duration<float>(now - last).count();
		last = now;
		if (dt > 0.0f)
		{
			_fps = _fps.load() + (1.0f / dt - _fps.load()) * 0.1f;
		}

		std::lock_guard<std::mutex> lock(_mutex);
		_latest = result;
		++_frameId;
	}

	camera.Close();
	winrt::uninit_apartment();
}

const char* GetTrackingStatusName(TrackingStatus status)
{
	switch (status)
	{
	case TrackingStatus::Stopped: return U8("停止中");
	case TrackingStatus::Starting: return U8("準備中");
	case TrackingStatus::Running: return U8("動作中");
	case TrackingStatus::Error: return U8("エラー");
	default: return "";
	}
}
