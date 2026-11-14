#include "PresenceStateMachine.h"

#include <algorithm>
#include <cmath>

#define U8(text) reinterpret_cast<const char*>(u8##text)

namespace
{
	// current を target へ rate（1秒あたり）で近づける
	float Approach(float current, float target, float rate, float deltaSeconds)
	{
		const float step = rate * deltaSeconds;
		if (current < target)
		{
			return (std::min)(current + step, target);
		}
		return (std::max)(current - step, target);
	}
}

void PresenceStateMachine::Update(float deltaSeconds, bool faceDetected, bool connected)
{
	_connectionLostTime = connected ? 0.0f : _connectionLostTime + deltaSeconds;
	const bool signalOk = faceDetected && connected;

	switch (_state)
	{
	case PresenceState::Present:
		if (_manualAway)
		{
			_state = PresenceState::Away;
			_foundTime = 0.0f;
		}
		else if (!signalOk)
		{
			_state = PresenceState::Unstable;
			_lostTime = 0.0f;
		}
		break;

	case PresenceState::Unstable:
		if (_manualAway)
		{
			_state = PresenceState::Away;
			_foundTime = 0.0f;
		}
		else if (signalOk)
		{
			_state = PresenceState::Present;
		}
		else
		{
			_lostTime += deltaSeconds;
			if (_lostTime >= _settings.awayDelay)
			{
				_state = PresenceState::Away;
				_foundTime = 0.0f;
			}
		}
		break;

	case PresenceState::Away:
		if (!_manualAway && signalOk)
		{
			_foundTime += deltaSeconds;
			if (_foundTime >= _settings.returnDelay)
			{
				_state = PresenceState::Returning;
				_returningTime = 0.0f;
			}
		}
		else
		{
			_foundTime = 0.0f;
		}
		break;

	case PresenceState::Returning:
		if (_manualAway)
		{
			_state = PresenceState::Away;
			_foundTime = 0.0f;
		}
		else if (!signalOk)
		{
			_state = PresenceState::Unstable;
			_lostTime = 0.0f;
		}
		else
		{
			_returningTime += deltaSeconds;
			if (_returningTime >= _settings.returnEffectTime)
			{
				_state = PresenceState::Present;
			}
		}
		break;
	}

	// 演出：離席はゆっくり消え、復帰は少し速く戻る
	const float awayTarget = (_state == PresenceState::Away) ? 1.0f : 0.0f;
	const float awayRate = (awayTarget > _awayBlend) ? 1.2f : 1.0f / (std::max)(_settings.returnEffectTime, 0.05f);
	_awayBlend = Approach(_awayBlend, awayTarget, awayRate, deltaSeconds);

	const float connectionTarget = IsConnectionLost() ? 1.0f : 0.0f;
	_connectionBlend = Approach(_connectionBlend, connectionTarget, 3.0f, deltaSeconds);
}

const char* GetPresenceStateName(PresenceState state)
{
	switch (state)
	{
	case PresenceState::Present: return U8("在席");
	case PresenceState::Unstable: return U8("不安定");
	case PresenceState::Away: return U8("離席");
	case PresenceState::Returning: return U8("復帰");
	default: return "";
	}
}
