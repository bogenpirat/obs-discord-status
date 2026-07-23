#include "engine.h"
#include "actions.h"
#include "../voice-state.h"

#include <obs-module.h>
#include <plugin-support.h>

RulesEngine::RulesEngine(VoiceStateModel *model, QObject *parent) : QObject(parent)
{
	connect(model, &VoiceStateModel::joinedVoice, this,
		[this](const QString &) { fire(TriggerType::JoinedVoice); });
	connect(model, &VoiceStateModel::leftVoice, this, [this] { fire(TriggerType::LeftVoice); });
	connect(model, &VoiceStateModel::selfMuteChanged, this,
		[this](bool muted) { fire(muted ? TriggerType::SelfMuted : TriggerType::SelfUnmuted); });
	connect(model, &VoiceStateModel::selfDeafChanged, this,
		[this](bool deaf) { fire(deaf ? TriggerType::SelfDeafened : TriggerType::SelfUndeafened); });
	connect(model, &VoiceStateModel::participantJoined, this,
		[this](const VoiceParticipant &user, int) { fire(TriggerType::ParticipantJoined, &user); });
	connect(model, &VoiceStateModel::participantLeft, this,
		[this](const VoiceParticipant &user, int) { fire(TriggerType::ParticipantLeft, &user); });
	connect(model, &VoiceStateModel::speakingChanged, this, [this](const VoiceParticipant &user, bool speaking) {
		fire(speaking ? TriggerType::SpeakingStarted : TriggerType::SpeakingStopped, &user);
	});
	connect(model, &VoiceStateModel::anySpeakingChanged, this, [this](bool speaking) {
		fire(speaking ? TriggerType::AnySpeakingStarted : TriggerType::AnySpeakingStopped);
	});
}

void RulesEngine::fire(TriggerType trigger, const VoiceParticipant *user)
{
	for (const Rule &rule : m_rules) {
		if (!rule.enabled || rule.trigger != trigger)
			continue;

		if (user && triggerHasUserFilter(trigger) && !rule.userFilter.isEmpty()) {
			bool matches = user->userId == rule.userFilter ||
				       user->displayName.contains(rule.userFilter, Qt::CaseInsensitive);
			if (!matches)
				continue;
		}

		obs_log(LOG_INFO, "rule '%s' fired (%s)", rule.name.toUtf8().constData(), triggerDisplayName(trigger));
		executeActions(rule.actions);
	}
}
