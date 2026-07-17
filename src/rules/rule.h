#pragma once

#include <QJsonObject>
#include <QList>
#include <QString>

enum class TriggerType {
	JoinedVoice,
	LeftVoice,
	SelfMuted,
	SelfUnmuted,
	SelfDeafened,
	SelfUndeafened,
	ParticipantJoined,
	ParticipantLeft,
	SpeakingStarted,
	SpeakingStopped,
	AnySpeakingStarted,
	AnySpeakingStopped,
};

enum class ActionType {
	ShowSource,
	HideSource,
	MuteSource,
	UnmuteSource,
	SetVolume,
};

struct RuleAction {
	ActionType type = ActionType::ShowSource;
	// Scene containing the item for Show/Hide. Empty = apply in all scenes.
	QString sceneName;
	QString sourceName;
	// SetVolume only: 100 = 0 dB. Range 0-200.
	double volumePercent = 100.0;

	QJsonObject toJson() const;
	static RuleAction fromJson(const QJsonObject &obj);
	QString describe() const;
};

struct Rule {
	QString name;
	bool enabled = true;
	TriggerType trigger = TriggerType::JoinedVoice;
	// For participant/speaking triggers: match against user id or display
	// name (case-insensitive substring). Empty matches anyone.
	QString userFilter;
	QList<RuleAction> actions;

	QJsonObject toJson() const;
	static Rule fromJson(const QJsonObject &obj);
};

const char *triggerDisplayName(TriggerType type);
const char *actionDisplayName(ActionType type);
bool triggerHasUserFilter(TriggerType type);
