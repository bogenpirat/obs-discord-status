#include "voice-state.h"
#include "discord/rpc-client.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonArray>

VoiceStateModel::VoiceStateModel(DiscordRpcClient *client, QObject *parent) : QObject(parent), m_client(client)
{
	connect(client, &DiscordRpcClient::connected, this, &VoiceStateModel::onConnected);
	connect(client, &DiscordRpcClient::disconnected, this, &VoiceStateModel::onDisconnected);
	connect(client, &DiscordRpcClient::eventReceived, this, &VoiceStateModel::onEvent);
	connect(client, &DiscordRpcClient::commandResponse, this, &VoiceStateModel::onCommandResponse);
}

QList<VoiceParticipant> VoiceStateModel::others() const
{
	QList<VoiceParticipant> result;
	for (const auto &p : m_participants) {
		if (p.userId != m_selfUserId)
			result.append(p);
	}
	return result;
}

bool VoiceStateModel::anySpeaking() const
{
	for (const auto &p : m_participants) {
		if (p.speaking && p.userId != m_selfUserId)
			return true;
	}
	return false;
}

void VoiceStateModel::onConnected(const QString &selfUserId, const QString &selfUsername)
{
	Q_UNUSED(selfUsername);
	m_selfUserId = selfUserId;
	m_haveVoiceSettings = false;

	// Global subscriptions (not tied to a channel).
	m_client->subscribe(QStringLiteral("VOICE_CHANNEL_SELECT"), {});
	m_client->subscribe(QStringLiteral("VOICE_SETTINGS_UPDATE"), {});
	// Seed current state.
	m_client->sendCommand(QStringLiteral("GET_VOICE_SETTINGS"), {});
	m_client->sendCommand(QStringLiteral("GET_SELECTED_VOICE_CHANNEL"), {});
}

void VoiceStateModel::onDisconnected()
{
	if (inVoiceChannel()) {
		resetChannelState();
		emit leftVoice();
	}
}

void VoiceStateModel::onEvent(const QString &evt, const QJsonObject &data)
{
	if (evt == QStringLiteral("VOICE_CHANNEL_SELECT")) {
		QString channelId = data.value("channel_id").toString(); // null/empty when leaving
		switchChannel(channelId);
	} else if (evt == QStringLiteral("VOICE_SETTINGS_UPDATE")) {
		setSelfVoiceSettings(data.value("mute").toBool(), data.value("deaf").toBool());
	} else if (evt == QStringLiteral("VOICE_STATE_CREATE")) {
		addVoiceState(data, true);
	} else if (evt == QStringLiteral("VOICE_STATE_UPDATE")) {
		addVoiceState(data, false);
	} else if (evt == QStringLiteral("VOICE_STATE_DELETE")) {
		removeVoiceState(data);
	} else if (evt == QStringLiteral("SPEAKING_START") || evt == QStringLiteral("SPEAKING_STOP")) {
		bool speaking = evt == QStringLiteral("SPEAKING_START");
		QString userId = data.value("user_id").toString();
		auto it = m_participants.find(userId);
		if (it == m_participants.end())
			return;
		if (it->speaking == speaking)
			return;
		bool wasAnySpeaking = anySpeaking();
		it->speaking = speaking;
		emit speakingChanged(*it, speaking);
		bool nowAnySpeaking = anySpeaking();
		if (wasAnySpeaking != nowAnySpeaking)
			emit anySpeakingChanged(nowAnySpeaking);
	}
}

void VoiceStateModel::onCommandResponse(const QString &cmd, const QString &nonce, const QJsonObject &data)
{
	Q_UNUSED(nonce);
	if (cmd == QStringLiteral("GET_SELECTED_VOICE_CHANNEL") || cmd == QStringLiteral("GET_CHANNEL")) {
		// data is null (not in voice) or a channel object with voice_states
		QString channelId = data.value("id").toString();
		if (cmd == QStringLiteral("GET_SELECTED_VOICE_CHANNEL")) {
			if (channelId.isEmpty())
				return;
			if (channelId != m_channelId)
				switchChannel(channelId);
		}
		if (!channelId.isEmpty() && channelId == m_channelId)
			seedChannel(data);
	} else if (cmd == QStringLiteral("GET_VOICE_SETTINGS")) {
		setSelfVoiceSettings(data.value("mute").toBool(), data.value("deaf").toBool());
	}
}

void VoiceStateModel::switchChannel(const QString &channelId)
{
	if (channelId == m_channelId)
		return;

	QJsonObject oldArgs{{"channel_id", m_channelId}};
	if (!m_channelId.isEmpty()) {
		for (const char *evt : {"VOICE_STATE_CREATE", "VOICE_STATE_UPDATE", "VOICE_STATE_DELETE",
					"SPEAKING_START", "SPEAKING_STOP"})
			m_client->unsubscribe(QString::fromLatin1(evt), oldArgs);
		resetChannelState();
		emit leftVoice();
		obs_log(LOG_INFO, "left voice channel");
	}

	m_channelId = channelId;
	if (m_channelId.isEmpty())
		return;

	QJsonObject args{{"channel_id", m_channelId}};
	for (const char *evt :
	     {"VOICE_STATE_CREATE", "VOICE_STATE_UPDATE", "VOICE_STATE_DELETE", "SPEAKING_START", "SPEAKING_STOP"})
		m_client->subscribe(QString::fromLatin1(evt), args);
	// Fetch name + current members; joinedVoice is emitted from seedChannel
	// once we know the channel name.
	m_client->sendCommand(QStringLiteral("GET_CHANNEL"), args);
}

void VoiceStateModel::seedChannel(const QJsonObject &channel)
{
	bool firstSeed = m_channelName.isEmpty();
	m_channelName = channel.value("name").toString();

	m_participants.clear();
	for (const QJsonValue &v : channel.value("voice_states").toArray())
		addVoiceState(v.toObject(), false);

	if (firstSeed) {
		obs_log(LOG_INFO, "joined voice channel '%s' (%d other member(s))", m_channelName.toUtf8().constData(),
			(int)others().size());
		emit joinedVoice(m_channelName);
	}
}

void VoiceStateModel::addVoiceState(const QJsonObject &state, bool emitSignals)
{
	QJsonObject user = state.value("user").toObject();
	QString userId = user.value("id").toString();
	if (userId.isEmpty())
		return;

	bool isNew = !m_participants.contains(userId);
	VoiceParticipant &p = m_participants[userId];
	p.userId = userId;
	QString nick = state.value("nick").toString();
	p.displayName = nick.isEmpty() ? user.value("username").toString() : nick;

	if (isNew && emitSignals && userId != m_selfUserId) {
		obs_log(LOG_INFO, "'%s' joined the voice channel", p.displayName.toUtf8().constData());
		emit participantJoined(p, others().size());
	}
}

void VoiceStateModel::removeVoiceState(const QJsonObject &state)
{
	QString userId = state.value("user").toObject().value("id").toString();
	auto it = m_participants.find(userId);
	if (it == m_participants.end())
		return;

	VoiceParticipant p = *it;
	bool wasAnySpeaking = anySpeaking();
	m_participants.erase(it);

	if (userId != m_selfUserId) {
		obs_log(LOG_INFO, "'%s' left the voice channel", p.displayName.toUtf8().constData());
		emit participantLeft(p, others().size());
	}
	if (wasAnySpeaking && !anySpeaking())
		emit anySpeakingChanged(false);
}

void VoiceStateModel::setSelfVoiceSettings(bool mute, bool deaf)
{
	bool first = !m_haveVoiceSettings;
	m_haveVoiceSettings = true;

	if (first) {
		m_selfMute = mute;
		m_selfDeaf = deaf;
		return; // initial seed, not an edge
	}
	if (mute != m_selfMute) {
		m_selfMute = mute;
		obs_log(LOG_INFO, "self %s", mute ? "muted" : "unmuted");
		emit selfMuteChanged(mute);
	}
	if (deaf != m_selfDeaf) {
		m_selfDeaf = deaf;
		obs_log(LOG_INFO, "self %s", deaf ? "deafened" : "undeafened");
		emit selfDeafChanged(deaf);
	}
}

void VoiceStateModel::resetChannelState()
{
	m_channelId.clear();
	m_channelName.clear();
	m_participants.clear();
}
