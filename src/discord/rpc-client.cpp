#include "rpc-client.h"
#include "transport.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonDocument>
#include <QJsonArray>
#include <QUrl>
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

QByteArray toBytes(const QJsonObject &obj)
{
	return QJsonDocument(obj).toJson(QJsonDocument::Compact);
}

// Blocking HTTPS POST; returns the response body (empty on failure).
QByteArray httpsPost(const wchar_t *host, const wchar_t *path, const wchar_t *contentType, const QByteArray &body)
{
	QByteArray response;
	HINTERNET session = WinHttpOpen(L"obs-discordstatus", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
					WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return response;
	HINTERNET connection = WinHttpConnect(session, host, INTERNET_DEFAULT_HTTPS_PORT, 0);
	HINTERNET request = connection ? WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
							    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
				       : nullptr;
	std::wstring header = std::wstring(L"Content-Type: ") + contentType + L"\r\n";
	if (request &&
	    WinHttpSendRequest(request, header.c_str(), (DWORD)-1, (PVOID)body.data(), (DWORD)body.size(),
			       (DWORD)body.size(), 0) &&
	    WinHttpReceiveResponse(request, nullptr)) {
		DWORD available = 0;
		while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
			std::vector<char> buf(available);
			DWORD read = 0;
			if (!WinHttpReadData(request, buf.data(), available, &read) || read == 0)
				break;
			response.append(buf.data(), read);
		}
	} else {
		obs_log(LOG_WARNING, "HTTPS POST to %ls failed (%lu)", host, GetLastError());
	}

	if (request)
		WinHttpCloseHandle(request);
	if (connection)
		WinHttpCloseHandle(connection);
	if (session)
		WinHttpCloseHandle(session);
	return response;
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
	closeTransport();
	if (m_thread.joinable())
		m_thread.join();
}

void DiscordRpcClient::restart()
{
	stop();
	start();
}

void DiscordRpcClient::setAuthMode(DiscordAuthMode mode, const QString &clientId, const QString &clientSecret)
{
	m_authMode = mode;
	m_ownClientId = clientId;
	m_ownClientSecret = clientSecret;
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

QString DiscordRpcClient::effectiveClientId() const
{
	return m_authMode == DiscordAuthMode::StreamKit ? QString::fromLatin1(kStreamKitClientId) : m_ownClientId;
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

		if (m_authMode == DiscordAuthMode::OwnApp && (m_ownClientId.isEmpty() || m_ownClientSecret.isEmpty())) {
			emit statusChanged(QStringLiteral("Own-app mode: set client ID and secret in settings"));
			for (int i = 0; i < 50 && !m_stop; i++)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			continue;
		}

		if (!openTransport()) {
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
		closeTransport();
	}
}

bool DiscordRpcClient::openTransport()
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (m_authMode == DiscordAuthMode::StreamKit)
		m_transport = std::make_unique<WebSocketTransport>();
	else
		m_transport = std::make_unique<PipeTransport>();

	if (!m_transport->open(effectiveClientId())) {
		m_transport.reset();
		return false;
	}
	return true;
}

void DiscordRpcClient::closeTransport()
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (m_transport) {
		m_transport->close();
		m_transport.reset();
	}
}

bool DiscordRpcClient::sendJson(const QJsonObject &msg)
{
	std::lock_guard<std::mutex> lock(m_sendMutex);
	if (!m_transport)
		return false;
	return m_transport->sendText(toBytes(msg));
}

QJsonObject DiscordRpcClient::readMessage()
{
	DiscordTransport *transport;
	{
		std::lock_guard<std::mutex> lock(m_sendMutex);
		transport = m_transport.get();
	}
	if (!transport)
		return {};

	QByteArray raw = transport->readMessage();
	if (raw.isEmpty())
		return {};

	QJsonParseError err;
	QJsonDocument doc = QJsonDocument::fromJson(raw, &err);
	if (err.error != QJsonParseError::NoError) {
		obs_log(LOG_WARNING, "failed to parse RPC message: %s", err.errorString().toUtf8().constData());
		return {};
	}
	return doc.object();
}

bool DiscordRpcClient::performHandshake()
{
	// First message after the transport opens is DISPATCH READY.
	QJsonObject ready = readMessage();
	if (ready.value("evt").toString() != QStringLiteral("READY")) {
		obs_log(LOG_WARNING, "expected READY, got: %s", QString::fromUtf8(toBytes(ready)).toUtf8().constData());
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
	args["client_id"] = effectiveClientId();
	if (m_authMode == DiscordAuthMode::StreamKit) {
		args["scopes"] = QJsonArray{"rpc", "messages.read"};
		args["prompt"] = QStringLiteral("none");
	} else {
		args["scopes"] = QJsonArray{"rpc"};
	}
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
	return exchangeCode(code);
}

QString DiscordRpcClient::exchangeCode(const QString &code)
{
	QByteArray response;
	if (m_authMode == DiscordAuthMode::StreamKit) {
		// StreamKit's backend holds the app secret and exchanges for us.
		response = httpsPost(L"streamkit.discord.com", L"/overlay/token", L"application/json",
				     toBytes(QJsonObject{{"code", code}}));
	} else {
		QByteArray body = "grant_type=authorization_code";
		body += "&client_id=" + QUrl::toPercentEncoding(m_ownClientId);
		body += "&client_secret=" + QUrl::toPercentEncoding(m_ownClientSecret);
		body += "&code=" + QUrl::toPercentEncoding(code);
		response = httpsPost(L"discord.com", L"/api/oauth2/token", L"application/x-www-form-urlencoded", body);
	}

	QString token = QJsonDocument::fromJson(response).object().value("access_token").toString();
	if (token.isEmpty())
		obs_log(LOG_WARNING, "OAuth code exchange failed: %s", response.constData());
	return token;
}

void DiscordRpcClient::receiveLoop()
{
	while (!m_stop) {
		QJsonObject msg = readMessage();
		if (msg.isEmpty())
			return; // transport closed or parse failure

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
