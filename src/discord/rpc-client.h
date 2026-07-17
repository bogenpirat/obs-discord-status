#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>

class DiscordTransport;

enum class DiscordAuthMode {
	// Authorize as Discord's own StreamKit app over the local WebSocket.
	// Zero setup for the user; unofficial but long-stable.
	StreamKit,
	// Authorize as a user-created Discord application over the IPC pipe.
	// Requires client ID + secret, fully within Discord's rules.
	OwnApp,
};

// Client for the local Discord RPC server. All signals are emitted from a
// worker thread — connect with queued connections (Qt's AutoConnection default
// handles this for cross-thread receivers).
class DiscordRpcClient : public QObject {
	Q_OBJECT

public:
	explicit DiscordRpcClient(QObject *parent = nullptr);
	~DiscordRpcClient() override;

	void start();
	void stop();
	void restart();

	// Configure before start()/restart(). For OwnApp, id/secret come from
	// the user's Discord application.
	void setAuthMode(DiscordAuthMode mode, const QString &clientId = QString(),
			 const QString &clientSecret = QString());
	DiscordAuthMode authMode() const { return m_authMode; }

	// Cached OAuth token (persisted by the config layer between sessions).
	void setAccessToken(const QString &token);
	QString accessToken() const;

	bool isReady() const { return m_ready.load(); }

	// Thread-safe; no-ops when the socket is down.
	void sendCommand(const QString &cmd, const QJsonObject &args, const QString &nonce = QString());
	void subscribe(const QString &evt, const QJsonObject &args);
	void unsubscribe(const QString &evt, const QJsonObject &args);

signals:
	// Authenticated and ready for commands/subscriptions.
	void connected(const QString &selfUserId, const QString &selfUsername);
	void disconnected();
	void statusChanged(const QString &status);
	void accessTokenChanged(const QString &token);
	// DISPATCH events (VOICE_STATE_CREATE, SPEAKING_START, ...).
	void eventReceived(const QString &evt, const QJsonObject &data);
	// Non-dispatch command responses (GET_SELECTED_VOICE_CHANNEL, ...).
	void commandResponse(const QString &cmd, const QString &nonce, const QJsonObject &data);

private:
	void runLoop();
	bool openTransport();
	bool performHandshake(); // READY + authenticate; false = close and retry
	bool authenticate(const QString &token, QJsonObject &responseData);
	QString authorizeAndExchangeCode();
	QString exchangeCode(const QString &code);
	void receiveLoop();
	void closeTransport();

	QString effectiveClientId() const;
	bool sendJson(const QJsonObject &msg);
	// Blocking read of one complete message; empty on socket error.
	QJsonObject readMessage();

	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_ready{false};

	mutable std::mutex m_tokenMutex;
	QString m_accessToken;

	DiscordAuthMode m_authMode = DiscordAuthMode::StreamKit;
	QString m_ownClientId;
	QString m_ownClientSecret;

	std::mutex m_sendMutex; // guards transport sends and teardown
	std::unique_ptr<DiscordTransport> m_transport;
};
