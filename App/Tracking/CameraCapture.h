#pragma once
/*
* CameraCapture
* Media Foundation で Web カメラの映像を取り込む（32bit BGRA、上から下）
*
*   映像はこの PC の中だけで使い、外へは送らない（企画書のプライバシー要件）。
*   ReadFrame はブロックするので、トラッキング用のスレッドから呼ぶ。
*/

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct CameraDevice
{
	std::string name;           // 表示名（UTF-8）
	std::wstring symbolicLink;  // デバイスを開くための識別子
};

class CameraCapture
{
public:
	CameraCapture();
	~CameraCapture();

	/**
	 * @brief 接続されているカメラの一覧
	 */
	static std::vector<CameraDevice> EnumerateDevices();

	/**
	 * @brief カメラを開く（640x480 前後を優先）
	 */
	bool Open(const CameraDevice& device, std::string& error);
	void Close();
	bool IsOpen() const;

	/**
	 * @brief 次のフレームを読む
	 * @param pixels BGRA（stride = width * 4）
	 */
	bool ReadFrame(std::vector<uint8_t>& pixels, int& width, int& height);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
