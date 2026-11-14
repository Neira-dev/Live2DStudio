#include "CameraCapture.h"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <cstring>
#include <filesystem>

#pragma comment(lib, "mfplat")
#pragma comment(lib, "mf")
#pragma comment(lib, "mfreadwrite")
#pragma comment(lib, "mfuuid")

using Microsoft::WRL::ComPtr;

namespace
{
	std::string ToUtf8(const wchar_t* text)
	{
		const std::u8string utf8 = std::filesystem::path(text != nullptr ? text : L"").u8string();
		return std::string(reinterpret_cast<const char*>(utf8.data()), utf8.size());
	}

	// Media Foundation の初期化（プロセスで 1 回）
	bool StartMediaFoundation()
	{
		static const bool started = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
		return started;
	}
}

struct CameraCapture::Impl
{
	ComPtr<IMFMediaSource> source;
	ComPtr<IMFSourceReader> reader;
	UINT32 width = 0;
	UINT32 height = 0;
	LONG defaultStride = 0;
};

CameraCapture::CameraCapture() = default;

CameraCapture::~CameraCapture()
{
	Close();
}

std::vector<CameraDevice> CameraCapture::EnumerateDevices()
{
	std::vector<CameraDevice> devices;
	if (!StartMediaFoundation())
	{
		return devices;
	}

	ComPtr<IMFAttributes> attributes;
	if (FAILED(MFCreateAttributes(&attributes, 1)) ||
		FAILED(attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
	{
		return devices;
	}

	IMFActivate** activates = nullptr;
	UINT32 count = 0;
	if (FAILED(MFEnumDeviceSources(attributes.Get(), &activates, &count)))
	{
		return devices;
	}
	for (UINT32 i = 0; i < count; ++i)
	{
		WCHAR* name = nullptr;
		WCHAR* link = nullptr;
		UINT32 length = 0;
		CameraDevice device;
		if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length)))
		{
			device.name = ToUtf8(name);
			CoTaskMemFree(name);
		}
		if (SUCCEEDED(activates[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &link, &length)))
		{
			device.symbolicLink = link;
			CoTaskMemFree(link);
		}
		activates[i]->Release();
		if (!device.symbolicLink.empty())
		{
			devices.push_back(std::move(device));
		}
	}
	CoTaskMemFree(activates);
	return devices;
}

bool CameraCapture::Open(const CameraDevice& device, std::string& error)
{
	Close();
	if (!StartMediaFoundation())
	{
		error = "Media Foundation could not be started.";
		return false;
	}

	auto impl = std::make_unique<Impl>();
	ComPtr<IMFAttributes> attributes;
	MFCreateAttributes(&attributes, 2);
	attributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
	attributes->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, device.symbolicLink.c_str());
	HRESULT hr = MFCreateDeviceSource(attributes.Get(), &impl->source);
	if (FAILED(hr))
	{
		error = "The camera could not be opened (it may be in use by another app).";
		return false;
	}

	// 色変換（YUY2 / NV12 / MJPG → RGB32）を Media Foundation に任せる。遅延は小さく
	ComPtr<IMFAttributes> readerAttributes;
	MFCreateAttributes(&readerAttributes, 2);
	readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
	readerAttributes->SetUINT32(MF_LOW_LATENCY, TRUE);
	hr = MFCreateSourceReaderFromMediaSource(impl->source.Get(), readerAttributes.Get(), &impl->reader);
	if (FAILED(hr))
	{
		error = "The camera reader could not be created.";
		return false;
	}

	// 640x480 に近い解像度を選ぶ（トラッキングにはこれで十分で、負荷が小さい）
	DWORD bestIndex = MAXDWORD;
	UINT64 bestScore = MAXUINT64;
	for (DWORD i = 0;; ++i)
	{
		ComPtr<IMFMediaType> type;
		if (FAILED(impl->reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), i, &type)))
		{
			break;
		}
		UINT32 w = 0, h = 0;
		MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
		UINT32 numerator = 0, denominator = 1;
		MFGetAttributeRatio(type.Get(), MF_MT_FRAME_RATE, &numerator, &denominator);
		const double fps = denominator ? static_cast<double>(numerator) / denominator : 0.0;
		const UINT64 score = static_cast<UINT64>(std::llabs(static_cast<long long>(w) * h - 640ll * 480)) + (fps < 25.0 ? 10000000ull : 0ull);
		if (score < bestScore)
		{
			bestScore = score;
			bestIndex = i;
		}
	}
	if (bestIndex != MAXDWORD)
	{
		ComPtr<IMFMediaType> native;
		impl->reader->GetNativeMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), bestIndex, &native);
		impl->reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, native.Get());
	}

	ComPtr<IMFMediaType> output;
	MFCreateMediaType(&output);
	output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
	hr = impl->reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, output.Get());
	if (FAILED(hr))
	{
		error = "The camera does not support RGB output.";
		return false;
	}

	ComPtr<IMFMediaType> current;
	impl->reader->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current);
	MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &impl->width, &impl->height);
	UINT32 stride = 0;
	if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)))
	{
		impl->defaultStride = static_cast<LONG>(stride);
	}
	else
	{
		impl->defaultStride = static_cast<LONG>(impl->width * 4);
	}

	_impl = std::move(impl);
	return true;
}

void CameraCapture::Close()
{
	if (_impl)
	{
		_impl->reader.Reset();
		if (_impl->source)
		{
			_impl->source->Shutdown();
		}
		_impl.reset();
	}
}

bool CameraCapture::IsOpen() const
{
	return _impl != nullptr;
}

bool CameraCapture::ReadFrame(std::vector<uint8_t>& pixels, int& width, int& height)
{
	if (!_impl)
	{
		return false;
	}

	DWORD streamIndex = 0, flags = 0;
	LONGLONG timestamp = 0;
	ComPtr<IMFSample> sample;
	const HRESULT hr = _impl->reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &streamIndex, &flags, &timestamp, &sample);
	if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR) || (flags & MF_SOURCE_READERF_ENDOFSTREAM))
	{
		return false;
	}
	if (!sample)
	{
		pixels.clear();
		return true;  // まだフレームが無い（ストリームの切り替わりなど）
	}

	ComPtr<IMFMediaBuffer> buffer;
	if (FAILED(sample->ConvertToContiguousBuffer(&buffer)))
	{
		return false;
	}

	width = static_cast<int>(_impl->width);
	height = static_cast<int>(_impl->height);
	pixels.resize(static_cast<size_t>(width) * height * 4);

	// 2D バッファなら行の向きと幅を正しく取れる
	ComPtr<IMF2DBuffer> buffer2d;
	BYTE* scanline = nullptr;
	LONG pitch = 0;
	if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline, &pitch)))
	{
		for (int y = 0; y < height; ++y)
		{
			std::memcpy(&pixels[static_cast<size_t>(y) * width * 4], scanline + static_cast<ptrdiff_t>(y) * pitch, static_cast<size_t>(width) * 4);
		}
		buffer2d->Unlock2D();
	}
	else
	{
		BYTE* data = nullptr;
		DWORD length = 0;
		if (FAILED(buffer->Lock(&data, nullptr, &length)))
		{
			return false;
		}
		const LONG stride = _impl->defaultStride;
		const LONG rowBytes = static_cast<LONG>(width) * 4;
		for (int y = 0; y < height; ++y)
		{
			// stride が負なら下から上に並んでいる
			const BYTE* row = (stride < 0) ? data + static_cast<size_t>(height - 1 - y) * rowBytes : data + static_cast<size_t>(y) * stride;
			if (row + rowBytes <= data + length)
			{
				std::memcpy(&pixels[static_cast<size_t>(y) * rowBytes], row, rowBytes);
			}
		}
		buffer->Unlock();
	}
	return true;
}
