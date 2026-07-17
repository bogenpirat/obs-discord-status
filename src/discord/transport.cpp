#include "transport.h"

#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonDocument>
#include <QJsonObject>

#define NOMINMAX
#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// WebSocketTransport

namespace {
// Origin must match one of the app's registered rpc_origins; StreamKit has
// this one registered.
const wchar_t *kOriginHeader = L"Origin: https://streamkit.discord.com";
} // namespace

WebSocketTransport::~WebSocketTransport()
{
	close();
}

bool WebSocketTransport::open(const QString &clientId)
{
	HINTERNET session = WinHttpOpen(L"obs-discordstatus", WINHTTP_ACCESS_TYPE_NO_PROXY, WINHTTP_NO_PROXY_NAME,
					WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session)
		return false;

	QString path = QStringLiteral("/?v=1&client_id=%1&encoding=json").arg(clientId);
	std::wstring wpath = path.toStdWString();

	for (INTERNET_PORT port = 6463; port <= 6472; port++) {
		HINTERNET connection = WinHttpConnect(session, L"127.0.0.1", port, 0);
		if (!connection)
			continue;

		HINTERNET request = WinHttpOpenRequest(connection, L"GET", wpath.c_str(), nullptr, WINHTTP_NO_REFERER,
						       WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
		if (!request) {
			WinHttpCloseHandle(connection);
			continue;
		}

		WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
		WinHttpAddRequestHeaders(request, kOriginHeader, (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);
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

bool WebSocketTransport::sendText(const QByteArray &payload)
{
	if (!m_ws)
		return false;
	DWORD rc = WinHttpWebSocketSend((HINTERNET)m_ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
					(PVOID)payload.data(), (DWORD)payload.size());
	if (rc != NO_ERROR) {
		obs_log(LOG_WARNING, "WebSocket send failed (%lu)", rc);
		return false;
	}
	return true;
}

QByteArray WebSocketTransport::readMessage()
{
	HINTERNET ws = (HINTERNET)m_ws;
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
	return QByteArray::fromStdString(buffer);
}

void WebSocketTransport::close()
{
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

// ---------------------------------------------------------------------------
// PipeTransport

namespace {
enum PipeOpcode : quint32 {
	OpHandshake = 0,
	OpFrame = 1,
	OpClose = 2,
	OpPing = 3,
	OpPong = 4,
};
} // namespace

PipeTransport::~PipeTransport()
{
	close();
}

bool PipeTransport::open(const QString &clientId)
{
	for (int i = 0; i < 10; i++) {
		QString path = QStringLiteral("\\\\.\\pipe\\discord-ipc-%1").arg(i);
		HANDLE pipe = CreateFileW(path.toStdWString().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
					  OPEN_EXISTING, 0, nullptr);
		if (pipe == INVALID_HANDLE_VALUE)
			continue;

		m_pipe = pipe;
		QJsonObject handshake{{"v", 1}, {"client_id", clientId}};
		if (!sendFrame(OpHandshake, QJsonDocument(handshake).toJson(QJsonDocument::Compact))) {
			close();
			continue;
		}
		obs_log(LOG_INFO, "connected to Discord RPC pipe %d", i);
		return true;
	}
	return false;
}

bool PipeTransport::sendFrame(quint32 opcode, const QByteArray &payload)
{
	if (!m_pipe)
		return false;
	QByteArray frame;
	quint32 length = (quint32)payload.size();
	frame.append((const char *)&opcode, 4);
	frame.append((const char *)&length, 4);
	frame.append(payload);
	DWORD written = 0;
	return WriteFile((HANDLE)m_pipe, frame.constData(), (DWORD)frame.size(), &written, nullptr) &&
	       written == (DWORD)frame.size();
}

bool PipeTransport::sendText(const QByteArray &payload)
{
	if (!sendFrame(OpFrame, payload)) {
		obs_log(LOG_WARNING, "pipe send failed (%lu)", GetLastError());
		return false;
	}
	return true;
}

bool PipeTransport::readExact(char *buffer, quint32 length)
{
	quint32 total = 0;
	while (total < length) {
		DWORD read = 0;
		if (!ReadFile((HANDLE)m_pipe, buffer + total, length - total, &read, nullptr) || read == 0)
			return false;
		total += read;
	}
	return true;
}

QByteArray PipeTransport::readMessage()
{
	while (m_pipe) {
		quint32 header[2];
		if (!readExact((char *)header, 8))
			return {};
		quint32 opcode = header[0];
		quint32 length = header[1];
		if (length > 64 * 1024 * 1024)
			return {};

		QByteArray payload(length, Qt::Uninitialized);
		if (length > 0 && !readExact(payload.data(), length))
			return {};

		switch (opcode) {
		case OpFrame:
			return payload;
		case OpPing:
			sendFrame(OpPong, payload);
			continue;
		case OpClose:
			return {};
		default:
			continue; // handshake echo / pong: ignore
		}
	}
	return {};
}

void PipeTransport::close()
{
	if (m_pipe) {
		HANDLE pipe = (HANDLE)m_pipe;
		m_pipe = nullptr;
		CancelIoEx(pipe, nullptr);
		CloseHandle(pipe);
	}
}
