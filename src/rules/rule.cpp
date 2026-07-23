#include "rule.h"

#include <QJsonArray>

namespace {

const char *kTriggerKeys[] = {
	"joined_voice",     "left_voice",       "self_muted",           "self_unmuted",
	"self_deafened",    "self_undeafened",  "participant_joined",   "participant_left",
	"speaking_started", "speaking_stopped", "any_speaking_started", "any_speaking_stopped",
};

const char *kActionKeys[] = {
	"show_source", "hide_source", "mute_source", "unmute_source", "set_volume",
};

template<typename E, size_t N> E keyToEnum(const char *const (&keys)[N], const QString &key, E fallback)
{
	for (size_t i = 0; i < N; i++) {
		if (key == QLatin1String(keys[i]))
			return static_cast<E>(i);
	}
	return fallback;
}

} // namespace

QJsonObject RuleAction::toJson() const
{
	QJsonObject obj;
	obj["type"] = QLatin1String(kActionKeys[static_cast<int>(type)]);
	obj["scene"] = sceneName;
	obj["source"] = sourceName;
	obj["volume_percent"] = volumePercent;
	return obj;
}

RuleAction RuleAction::fromJson(const QJsonObject &obj)
{
	RuleAction action;
	action.type = keyToEnum(kActionKeys, obj.value("type").toString(), ActionType::ShowSource);
	action.sceneName = obj.value("scene").toString();
	action.sourceName = obj.value("source").toString();
	action.volumePercent = obj.value("volume_percent").toDouble(100.0);
	return action;
}

QString RuleAction::describe() const
{
	QString desc = QString::fromUtf8(actionDisplayName(type)) + QStringLiteral(": ") + sourceName;
	if (type == ActionType::ShowSource || type == ActionType::HideSource) {
		desc += sceneName.isEmpty() ? QStringLiteral(" (all scenes)")
					    : QStringLiteral(" (scene '%1')").arg(sceneName);
	} else if (type == ActionType::SetVolume) {
		desc += QStringLiteral(" -> %1%").arg(volumePercent);
	}
	return desc;
}

QJsonObject Rule::toJson() const
{
	QJsonObject obj;
	obj["name"] = name;
	obj["enabled"] = enabled;
	obj["trigger"] = QLatin1String(kTriggerKeys[static_cast<int>(trigger)]);
	obj["user_filter"] = userFilter;
	QJsonArray actionArray;
	for (const RuleAction &action : actions)
		actionArray.append(action.toJson());
	obj["actions"] = actionArray;
	return obj;
}

Rule Rule::fromJson(const QJsonObject &obj)
{
	Rule rule;
	rule.name = obj.value("name").toString();
	rule.enabled = obj.value("enabled").toBool(true);
	rule.trigger = keyToEnum(kTriggerKeys, obj.value("trigger").toString(), TriggerType::JoinedVoice);
	rule.userFilter = obj.value("user_filter").toString();
	for (const QJsonValue &v : obj.value("actions").toArray())
		rule.actions.append(RuleAction::fromJson(v.toObject()));
	return rule;
}

const char *triggerDisplayName(TriggerType type)
{
	switch (type) {
	case TriggerType::JoinedVoice:
		return "Joined voice channel";
	case TriggerType::LeftVoice:
		return "Left voice channel";
	case TriggerType::SelfMuted:
		return "Muted myself";
	case TriggerType::SelfUnmuted:
		return "Unmuted myself";
	case TriggerType::SelfDeafened:
		return "Deafened myself";
	case TriggerType::SelfUndeafened:
		return "Undeafened myself";
	case TriggerType::ParticipantJoined:
		return "Someone joined the channel";
	case TriggerType::ParticipantLeft:
		return "Someone left the channel";
	case TriggerType::SpeakingStarted:
		return "User started speaking";
	case TriggerType::SpeakingStopped:
		return "User stopped speaking";
	case TriggerType::AnySpeakingStarted:
		return "Anyone started speaking";
	case TriggerType::AnySpeakingStopped:
		return "Everyone stopped speaking";
	}
	return "?";
}

const char *actionDisplayName(ActionType type)
{
	switch (type) {
	case ActionType::ShowSource:
		return "Show source";
	case ActionType::HideSource:
		return "Hide source";
	case ActionType::MuteSource:
		return "Mute audio source";
	case ActionType::UnmuteSource:
		return "Unmute audio source";
	case ActionType::SetVolume:
		return "Set volume";
	}
	return "?";
}

bool triggerHasUserFilter(TriggerType type)
{
	switch (type) {
	case TriggerType::ParticipantJoined:
	case TriggerType::ParticipantLeft:
	case TriggerType::SpeakingStarted:
	case TriggerType::SpeakingStopped:
		return true;
	default:
		return false;
	}
}
