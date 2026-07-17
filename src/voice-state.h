#pragma once

#include <QObject>
#include <QHash>
#include <QJsonObject>
#include <QString>

class DiscordRpcClient;

struct VoiceParticipant {
	QString userId;
	QString displayName; // nick if set, else username
	bool speaking = false;
};

// Normalizes raw Discord RPC events into a coherent voice-state model with
// edge-triggered signals. Lives on the OBS/Qt main thread; the client's
// signals arrive via queued connections.
class VoiceStateModel : public QObject {
	Q_OBJECT

public:
	explicit VoiceStateModel(DiscordRpcClient *client, QObject *parent = nullptr);

	bool inVoiceChannel() const { return !m_channelId.isEmpty(); }
	QString channelName() const { return m_channelName; }
	bool selfMute() const { return m_selfMute; }
	bool selfDeaf() const { return m_selfDeaf; }
	// Participants excluding the local user.
	QList<VoiceParticipant> others() const;
	int otherCount() const { return others().size(); }
	bool anySpeaking() const;

signals:
	void joinedVoice(const QString &channelName);
	void leftVoice();
	void selfMuteChanged(bool muted);
	void selfDeafChanged(bool deafened);
	void participantJoined(const VoiceParticipant &user, int otherCount);
	void participantLeft(const VoiceParticipant &user, int otherCount);
	void speakingChanged(const VoiceParticipant &user, bool speaking);
	void anySpeakingChanged(bool speaking);

private slots:
	void onConnected(const QString &selfUserId, const QString &selfUsername);
	void onDisconnected();
	void onEvent(const QString &evt, const QJsonObject &data);
	void onCommandResponse(const QString &cmd, const QString &nonce, const QJsonObject &data);

private:
	void switchChannel(const QString &channelId);
	void seedChannel(const QJsonObject &channel);
	void addVoiceState(const QJsonObject &state, bool emitSignals);
	void removeVoiceState(const QJsonObject &state);
	void setSelfVoiceSettings(bool mute, bool deaf);
	void resetChannelState();

	DiscordRpcClient *m_client;
	QString m_selfUserId;
	QString m_channelId;
	QString m_channelName;
	bool m_selfMute = false;
	bool m_selfDeaf = false;
	bool m_haveVoiceSettings = false;
	QHash<QString, VoiceParticipant> m_participants; // includes self
};
