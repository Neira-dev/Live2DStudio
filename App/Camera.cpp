#include "Camera.h"

#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace
{
	constexpr float MinPitch = -1.45f;  // 真上・真下を向き切らないようにする（約 83 度）
	constexpr float MaxPitch = 1.45f;
	constexpr float MinDistance = 0.2f;
	constexpr float MaxDistance = 100.0f;
}

Camera::Camera()
{
	Reset();
}

void Camera::Reset()
{
	// モデル（高さ約 1.6m）の全身が見える位置から、少し見下ろす
	_target = XMFLOAT3(0.0f, 0.8f, 0.0f);
	_yaw = 0.0f;
	_pitch = 0.15f;
	_distance = 3.5f;
	_fovY = XMConvertToRadians(45.0f);
}

void Camera::Orbit(float deltaYaw, float deltaPitch)
{
	_yaw += deltaYaw;
	_yaw = std::remainder(_yaw, XM_2PI);
	_pitch = std::clamp(_pitch + deltaPitch, MinPitch, MaxPitch);
}

void Camera::Zoom(float scale)
{
	SetDistance(_distance * scale);
}

void Camera::SetDistance(float distance)
{
	_distance = std::clamp(distance, MinDistance, MaxDistance);
}

void Camera::Pan(float right, float up)
{
	const XMMATRIX view = GetView();
	const XMMATRIX invView = XMMatrixInverse(nullptr, view);
	const XMVECTOR rightDir = invView.r[0];
	const XMVECTOR upDir = invView.r[1];
	XMVECTOR target = XMLoadFloat3(&_target);
	target += (rightDir * right + upDir * up) * _distance;
	XMStoreFloat3(&_target, target);
}

XMFLOAT3 Camera::GetPosition() const
{
	// yaw = 0 のとき -Z 側（手前）から +Z 方向を見る
	const float cosPitch = std::cos(_pitch);
	return XMFLOAT3(
		_target.x + std::sin(_yaw) * cosPitch * _distance,
		_target.y + std::sin(_pitch) * _distance,
		_target.z - std::cos(_yaw) * cosPitch * _distance);
}

XMMATRIX Camera::GetView() const
{
	const XMFLOAT3 position = GetPosition();
	return XMMatrixLookAtLH(XMLoadFloat3(&position), XMLoadFloat3(&_target), XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
}

XMMATRIX Camera::GetProjection(float aspect) const
{
	return XMMatrixPerspectiveFovLH(_fovY, aspect > 0.0f ? aspect : 1.0f, _nearZ, _farZ);
}

XMMATRIX Camera::GetViewProjection(float aspect) const
{
	return GetView() * GetProjection(aspect);
}
