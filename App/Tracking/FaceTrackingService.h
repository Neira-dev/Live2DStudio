#pragma once
/*
* FaceTrackingService
* カメラの読み込みと顔の推定を別スレッドで回し、最新の結果を渡す
*
*   メインスレッドからは Start / Stop と GetLatest だけを使う。
*/

#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "CameraCapture.h"
#include "FaceTracker.h"

enum class TrackingStatus
{
	Stopped,     // 停止中
	Starting,    // カメラとモデルを準備中
	Running,     // 動作中
	Error,       // エラー（GetError で内容）
};

class FaceTrackingService
{
public:
	explicit FaceTrackingService(std::filesystem::path modelDirectory);
	~FaceTrackingService();

	/**
	 * @brief カメラの一覧を取り直す
	 */
	const std::vector<CameraDevice>& RefreshDevices();
	const std::vector<CameraDevice>& GetDevices() const { return _devices; }

	/**
	 * @brief 指定したカメラでトラッキングを始める
	 */
	void Start(int deviceIndex);
	void Stop();

	TrackingStatus GetStatus() const { return _status.load(); }
	std::string GetError() const;
	int GetDeviceIndex() const { return _deviceIndex; }

	/**
	 * @brief 最新の結果（新しい結果が来ていれば true）
	 */
	bool GetLatest(FaceTrackingResult& result, unsigned long long& frameId) const;

	float GetCameraFps() const { return _fps.load(); }

private:
	void Run(CameraDevice device);

private:
	std::filesystem::path _modelDirectory;
	std::vector<CameraDevice> _devices;
	int _deviceIndex = -1;

	std::thread _thread;
	std::atomic<bool> _stop = false;
	std::atomic<TrackingStatus> _status = TrackingStatus::Stopped;
	std::atomic<float> _fps = 0.0f;

	mutable std::mutex _mutex;
	FaceTrackingResult _latest;
	unsigned long long _frameId = 0;
	std::string _error;
};

const char* GetTrackingStatusName(TrackingStatus status);
