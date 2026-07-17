#include "rpc-client.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QUuid>

#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

namespace {

// Discord's own StreamKit application. Authorizing as StreamKit lets any user
// run this plugin without registering a Discord application; the code->token
// exchange happens against StreamKit's backend, which holds the app secret.
const char *kStreamKitClientId = "207646673902501888";
const wchar_t *kWsPath = L"/?v=1&client_id=207646673902501888&encoding=json";
// Origin must match one of StreamKit's registered rpc_origins.
const wchar_t *kOriginHeader = L"Origin: https://streamkit.discord.com";

QByteArray toBytes(const QJsonObject &obj)
{
	return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

} // namespace

DiscordRpcClient::DiscordRpcClient(QObject *parent) : QObject(parent) {}

DiscordRpcClient::~DiscordRpcClient()
{
	stop();
}

void DiscordRpcClient::start()
{
	if (m_thread.joinable())
		return;
	m_stop = false;
	m_thread = std::thread([this] { runLoop(); });
}

void DiscordRpcClient::stop()
{
	m_stop = true;
	closeSocket();
	if (m_thread.joinable())
		m_thread.join();
}

void DiscordRpcClient::setAccessToken(const QString &token)
{
	std::lock_guard<std::mutex> lock(m_tokenMutex);
	m_accessToken = token;
}

QString DiscordRpcClient::accessToken() const
{
	std::lock_guard<std::mutex> lock(m_tokenMutex);
	return m_accessToken;
}

void DiscordRpcClient::sendCommand(const QString &cmd, const QJsonObject &args, const QString &nonce)
{
	QJsonObject msg;
	msg["cmd"] = cmd;
	msg["args"] = args;
	msg["nonce"] = nonce.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : nonce;
	sendJson(msg);
}

void DiscordRpcClient::subscribe(const QString &evt, const QJsonObject &args)
{
	QJsonObject msg;
	msg["cmd"] = QStringLiteral("SUBSCRIBE");
	msg["evt"] = evt;
	msg["args"] = args;
	msg["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
	sendJson(msg);
}

void DiscordRpcClient::unsubscribe(const QString &evt, const QJsonObject &args)
{
	QJsonObject msg;
	msg["cmd"] = QStringLiteral("UNSUBSCRIBE");
	msg["evt"] = evt;
	msg["args"] = args;
	msg["nonce"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
	sendJson(msg);
}

void DiscordRpcClient::runLoop()
{
	int backoffSeconds = 2;
	while (!m_stop) {
		emit statusChanged(QStringLiteral("Connecting to Discord..."));
		if (!connectAnyPort()) {
			emit statusChanged(QStringLiteral("Discord not running"));
			for (int i = 0; i < backoffSeconds * 10 && !m_stop; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			backoffSeconds = std::min(backoffSeconds * 2, 30);
			continue;
		}
		backoffSeconds = 2;

		if (performHandshake()) {
			receiveLoop();
			m_ready = false;
			emit disconnected();
		}
		closeSocket();
	}
}

bool DiscordRpcClient::connectAnyPort()
{
	std::lock_guard<std::mutex> lock(m_sendMutex);

	HINTERNET session = WinHttpOpen(L"obs-discordstatus", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return false;

	for (INTERNET_PORT port = 6463; port <= 6472 && !m_stop; port++) {
		HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
		if (!connection)
			continue;

		HINTERNET request = WinHttpOpenRequest(connection, L"GET", kWsPath, nullptr, WINHTTP_NO_REFERER,
						       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!request) {
			WinHttpCloseHandle(connection);
			continue;
		}

		WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
		WinHttpAddRequestHeaders(request, kOriginHeader, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

		// Short timeouts: this is a loopback connection.
		WinHttpSetTimeouts(request, 2000, 2000, 5000, 5000);

		if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) &&
		    WinHttpReceiveResponse(request, nullptr)) {
			DWORD status = 0;
			DWORD size = sizeof(status);
			WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
					    WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
			if (status == 101) {
				HINTERNET ws = WinHttpWebSocketCompleteUpgrade(request, 0);
				WinHttpCloseHandle(request);
				if (ws) {
					m_session = session;
					m_connection = connection;
					m_ws = ws;
					obs_log(LOG_INFO, "connected to Discord RPC on port %d", (int)port);
					return true;
				}
				WinHttpCloseHandle(connection);
				continue;
			}
			obs_log(LOG_WARNING, "Discord RPC port %d rejected upgrade (HTTP %lu)", (int)port, status);
		}
		WinHttpCloseHandle(request);
		WinHttpCloseHandle(connection);
	}

	WinHttpCloseHandle(session);
	return false;
}

void DiscordRpcClient::closeSocket()
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (m_ws) {
		WinHttpWebSocketClose((HINTERNET)m_ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle((HINTERNET)m_ws);
		m_ws = nullptr;
	}
	if (m_connection) {
		WinHttpCloseHandle((HINTERNET)m_connection);
		m_connection = nullptr;
	}
	if (m_session) {
		WinHttpCloseHandle((HINTERNET)m_session);
		m_session = nullptr;
	}
}

bool DiscordRpcClient::sendJson(const QJsonObject &msg)
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (!m_ws)
		return false;
	QByteArray payload = toBytes(msg);
	DWORD rc = WinHttpWebSocketSend((HINTERNET)m_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
					(PVOID)payload.data(), (DWORD)payload.size());
	if (rc != NO_ERROR) {
		obs_log(LOG_WARNING, "WebSocket send failed (%lu)", rc);
		return false;
	}
	return true;
}

QJsonObject DiscordRpcClient::readMessage()
{
	HINTERNET ws;
	{
		std::lock_guard<std::mutex> lock(m_sendMutex);
		ws = (HINTERNET)m_ws;
	}
	if (!ws)
		return {};

	std::string buffer;
	std::vector<char> chunk(16384);
	for (;;) {
		DWORD read = 0;
		WINHTTP_WEB_SOCKET_BUFFER_TYPE type;
		DWORD rc = WinHttpWebSocketReceive(ws, chunk.data(), (DWORD)chunk.size(), &read, &type);
		if (rc != NO_ERROR)
			return {};
		if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
			return {};
		buffer.append(chunk.data(), read);
		if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
		    type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
			break;
	}

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(buffer), &err);
	if (err.error != QJsonParseError::NoError) {
		obs_log(LOG_WARNING, "failed to parse RPC message: %s", err.errorString().toUtf8().constData());
		return {};
	}
	return doc.object();
}

bool DiscordRpcClient::performHandshake()
{
	// First message after the socket opens is DISPATCH READY.
	QJsonObject ready = readMessage();
	if (ready.value("evt").toString() != QStringLiteral("READY")) {
		obs_log(LOG_WARNING, "expected READY, got: %s",
			QString::fromUtf8(toBytes(ready)).toUtf8().constData());
		return false;
	}

	QJsonObject authData;
	QString token = accessToken();
	bool authed = !token.isEmpty() && authenticate(token, authData);

	if (!authed) {
		emit statusChanged(QStringLiteral("Waiting for authorization in Discord..."));
		token = authorizeAndExchangeCode();
		if (token.isEmpty()) {
			emit statusChanged(QStringLiteral("Authorization failed or declined"));
			// Back off; the user declined or something went wrong.
			for (int i = 0; i < 100 && !m_stop; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			return false;
		}
		if (!authenticate(token, authData)) {
			emit statusChanged(QStringLiteral("Authentication failed"));
			return false;
		}
		setAccessToken(token);
		emit accessTokenChanged(token);
	}

	QJsonObject user = authData.value("user").toObject();
	QString userId = user.value("id").toString();
	QString username = user.value("username").toString();
	obs_log(LOG_INFO, "authenticated with Discord as %s", username.toUtf8().constData());

	m_ready = true;
	emit statusChanged(QStringLiteral("Connected as ") + username);
	emit connected(userId, username);
	return true;
}

bool DiscordRpcClient::authenticate(const QString &token, QJsonObject &responseData)
{
	QJsonObject args;
	args["access_token"] = token;
	sendCommand(QStringLiteral("AUTHENTICATE"), args, QStringLiteral("authenticate"));

	QJsonObject reply = readMessage();
	if (reply.isEmpty())
		return false;
	if (reply.value("evt").toString() == QStringLiteral("ERROR")) {
		obs_log(LOG_INFO, "cached Discord token rejected: %s",
			reply.value("data").toObject().value("message").toString().toUtf8().constData());
		return false;
	}
	responseData = reply.value("data").toObject();
	return true;
}

QString DiscordRpcClient::authorizeAndExchangeCode()
{
	QJsonObject args;
	args["client_id"] = QString::fromLatin1(kStreamKitClientId);
	args["scopes"] = QJsonArray{"rpc", "messages.read"};
	args["prompt"] = QStringLiteral("none");
	sendCommand(QStringLiteral("AUTHORIZE"), args, QStringLiteral("authorize"));

	// The response arrives only after the user clicks through Discord's
	// consent popup — this can take arbitrarily long.
	QJsonObject reply = readMessage();
	if (reply.isEmpty() || reply.value("evt").toString() == QStringLiteral("ERROR")) {
		obs_log(LOG_WARNING, "AUTHORIZE failed: %s",
			reply.value("data").toObject().value("message").toString().toUtf8().constData());
		return {};
	}
	QString code = reply.value("data").toObject().value("code").toString();
	if (code.isEmpty())
		return {};

	// Exchange the OAuth code at StreamKit's backend (it owns the secret).
	QByteArray body = toBytes(QJsonObject{{"code", code}});
	QString token;

	HINTERNET session = WinHttpOpen(L"obs-discordstatus", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return {};
	HINTERNET connection = WinHttpConnect(session, L"streamkit.discord.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", L"/overlay/token", nullptr,
							    WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
							    WINHTTP_FLAG_SECURE)
				       : nullptr;
	if (request &&
	    WinHttpSendRequest(request, L"Content-Type: application/json\r\n", (DWORD)-1, (PVOID)body.data(),
			       (DWORD)body.size(), (DWORD)body.size(), 0) &&
	    WinHttpReceiveResponse(request, nullptr)) {
		std::string response;
		DWORD available = 0;
		while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
			std::vector<char> buf(available);
			DWORD read = 0;
			if (!WinHttpReadData(request, buf.data(), available, &read) || read == 0)
				break;
			response.append(buf.data(), read);
		}
		QJsonObject obj = QJsonDocument::fromJson(QByteArray::fromStdString(response)).object();
		token = obj.value("access_token").toString();
		if (token.isEmpty())
			obs_log(LOG_WARNING, "StreamKit token exchange failed: %s", response.c_str());
	} else {
		obs_log(LOG_WARNING, "StreamKit token exchange request failed (%lu)", GetLastError());
	}

	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	if (session)
		WinHttpCloseHandle(session);
	return token;
}

void DiscordRpcClient::receiveLoop()
{
	while (!m_stop) {
		QJsonObject msg = readMessage();
		if (msg.isEmpty())
			return; // socket closed or parse failure

		QString cmd = msg.value("cmd").toString();
		QString evt = msg.value("evt").toString();
		QJsonObject data = msg.value("data").toObject();

		if (cmd == QStringLiteral("DISPATCH")) {
			emit eventReceived(evt, data);
		} else if (evt == QStringLiteral("ERROR")) {
			obs_log(LOG_WARNING, "RPC error for %s: %s", cmd.toUtf8().constData(),
				data.value("message").toString().toUtf8().constData());
		} else if (cmd != QStringLiteral("SUBSCRIBE") && cmd != QStringLiteral("UNSUBSCRIBE")) {
			emit commandResponse(cmd, msg.value("nonce").toString(), data);
		}
	}
}
