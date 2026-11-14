#pragma once
/*
* PresenceStateMachine
* 離席検知の状態機械
*
*   在席 ─(顔ロスト or 回線断)→ 不安定 ─(3秒続く)→ 離席 ─(0.5秒戻る)→ 復帰 → 在席
*                                    └(すぐ戻る)→ 在席
*   手動離席はいつでも離席にし、解除すると復帰する。
*
* 入力は「顔が見えているか」と「回線がつながっているか」の2つ。
* （今はトラッキングと通信が未実装なので、UI のテスト用スイッチから入れる）
*/

enum class PresenceState
{
	Present,    // 在席
	Unstable,   // 不安定（顔ロスト・回線断の直後）
	Away,       // 離席
	Returning,  // 復帰（演出中）
};

class PresenceStateMachine
{
public:
	struct Settings
	{
		float awayDelay = 3.0f;            // 顔ロストが何秒続いたら離席にするか
		float returnDelay = 0.5f;          // 顔が何秒戻ったら復帰にするか
		float connectionLossDelay = 0.5f;  // 回線断が何秒続いたら「接続不安定」にするか
		float returnEffectTime = 0.6f;     // 復帰演出の長さ
	};

public:
	/**
	 * @brief 状態を進める
	 * @param faceDetected 顔が見えているか
	 * @param connected 回線がつながっているか
	 */
	void Update(float deltaSeconds, bool faceDetected, bool connected);

	void SetManualAway(bool away) { _manualAway = away; }
	bool IsManualAway() const { return _manualAway; }

	Settings& GetSettings() { return _settings; }
	const Settings& GetSettings() const { return _settings; }

	PresenceState GetState() const { return _state; }

	/**
	 * @brief 回線断が connectionLossDelay を超えているか（映像を止めて「接続不安定」を出す）
	 */
	bool IsConnectionLost() const { return _connectionLostTime >= _settings.connectionLossDelay; }

	/**
	 * @brief 離席演出の進み具合（0 = 在席、1 = 離席）。なめらかに変化する
	 */
	float GetAwayBlend() const { return _awayBlend; }

	/**
	 * @brief 回線断の演出の進み具合（0 = 正常、1 = 半透明）
	 */
	float GetConnectionBlend() const { return _connectionBlend; }

private:
	Settings _settings;
	PresenceState _state = PresenceState::Present;
	bool _manualAway = false;
	float _lostTime = 0.0f;
	float _foundTime = 0.0f;
	float _returningTime = 0.0f;
	float _connectionLostTime = 0.0f;
	float _awayBlend = 0.0f;
	float _connectionBlend = 0.0f;
};

const char* GetPresenceStateName(PresenceState state);
