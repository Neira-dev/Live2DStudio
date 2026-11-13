#pragma once
/*
* Camera
* 注視点の周りを回るカメラ（オービットカメラ）
*
* 座標系は左手系（+X 右、+Y 上、+Z 奥）。単位は 1 = 1m を想定。
*   ドラッグで回転 … Orbit
*   ホイールでズーム … Zoom
*   右ドラッグで平行移動 … Pan
*/

#include <DirectXMath.h>

class Camera
{
public:
	Camera();

	/**
	 * @brief 初期位置に戻す
	 */
	void Reset();

	/**
	 * @brief 注視点の周りを回る
	 * @param deltaYaw 左右の回転（ラジアン）
	 * @param deltaPitch 上下の回転（ラジアン）
	 */
	void Orbit(float deltaYaw, float deltaPitch);

	/**
	 * @brief 注視点に近づく・離れる
	 * @param scale 距離に掛ける倍率（1 未満で近づく）
	 */
	void Zoom(float scale);

	/**
	 * @brief 画面の向きに沿って注視点ごと平行移動する
	 * @param right 右方向の移動量（距離 1 あたり）
	 * @param up 上方向の移動量（距離 1 あたり）
	 */
	void Pan(float right, float up);

	void SetTarget(const DirectX::XMFLOAT3& target) { _target = target; }
	void SetDistance(float distance);
	void SetFovY(float radians) { _fovY = radians; }

	const DirectX::XMFLOAT3& GetTarget() const { return _target; }
	float GetDistance() const { return _distance; }
	float GetYaw() const { return _yaw; }
	float GetPitch() const { return _pitch; }
	float GetFovY() const { return _fovY; }

	/**
	 * @brief カメラの位置
	 */
	DirectX::XMFLOAT3 GetPosition() const;

	DirectX::XMMATRIX GetView() const;
	DirectX::XMMATRIX GetProjection(float aspect) const;

	/**
	 * @brief ビュー × プロジェクション
	 * @param aspect 画面の幅 / 高さ
	 */
	DirectX::XMMATRIX GetViewProjection(float aspect) const;

private:
	DirectX::XMFLOAT3 _target;
	float _yaw = 0.0f;
	float _pitch = 0.0f;
	float _distance = 1.0f;
	float _fovY = 0.0f;
	float _nearZ = 0.05f;
	float _farZ = 200.0f;
};
