#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>

#include <atomic>
#include <mutex>
#include <thread>

// Client for the local Discord RPC server (WebSocket transport on
// 127.0.0.1:6463-6472). Authenticates via the StreamKit application so users
// don't have to register their own Discord app; an own-app mode can reuse the
// same protocol over IPC later.
//
// All signals are emitted from a worker thread — connect with queued
// connections (Qt's default AutoConnection does this for cross-thread).
class DiscordRpcClient : public QObject {
	Q_OBJECT

public:
	explicit DiscordRpcClient(QObject *parent = nullptr);
	~DiscordRpcClient() override;

	void start();
	void stop();

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
	bool connectAnyPort();
	bool performHandshake(); // READY + authenticate; false = close and retry
	bool authenticate(const QString &token, QJsonObject &responseData);
	QString authorizeAndExchangeCode();
	void receiveLoop();
	void closeSocket();

	bool sendJson(const QJsonObject &msg);
	// Blocking read of one complete text message; empty on socket error.
	QJsonObject readMessage();

	std::thread m_thread;
	std::atomic<bool> m_stop{false};
	std::atomic<bool> m_ready{false};

	mutable std::mutex m_tokenMutex;
	QString m_accessToken;

	std::mutex m_sendMutex; // guards m_ws sends and handle teardown
	void *m_session = nullptr;
	void *m_connection = nullptr;
	void *m_ws = nullptr;
};
