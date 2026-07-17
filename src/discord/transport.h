#pragma once

#include <QByteArray>
#include <QString>

// A connected message transport to the local Discord RPC server. Implementations
// are not thread-safe per method; the client serializes sends with a mutex and
// reads from a single worker thread. close() may be called from another thread
// to abort a blocking read.
class DiscordTransport {
public:
	virtual ~DiscordTransport() = default;

	// Establish the connection (including any protocol handshake). Returns
	// false if Discord isn't reachable.
	virtual bool open(const QString &clientId) = 0;
	virtual bool sendText(const QByteArray &payload) = 0;
	// Blocking; returns an empty array on close/error.
	virtual QByteArray readMessage() = 0;
	virtual void close() = 0;
	virtual const char *name() const = 0;
};

// WebSocket on 127.0.0.1:6463-6472 via WinHTTP. Used for StreamKit auth, which
// requires an allowlisted Origin header.
class WebSocketTransport : public DiscordTransport {
public:
	~WebSocketTransport() override;
	bool open(const QString &clientId) override;
	bool sendText(const QByteArray &payload) override;
	QByteArray readMessage() override;
	void close() override;
	const char *name() const override { return "websocket"; }

private:
	void *m_session = nullptr;
	void *m_connection = nullptr;
	void *m_ws = nullptr;
};

// Named pipe \\.\pipe\discord-ipc-{0..9} with the framed RPC protocol
// (8-byte little-endian header: opcode, length). Used for own-app auth.
class PipeTransport : public DiscordTransport {
public:
	~PipeTransport() override;
	bool open(const QString &clientId) override;
	bool sendText(const QByteArray &payload) override;
	QByteArray readMessage() override;
	void close() override;
	const char *name() const override { return "ipc pipe"; }

private:
	bool sendFrame(quint32 opcode, const QByteArray &payload);
	bool readExact(char *buffer, quint32 length);

	void *m_pipe = nullptr; // INVALID_HANDLE_VALUE semantics handled in .cpp
};
