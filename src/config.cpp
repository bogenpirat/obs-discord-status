#include "config.h"

#include <obs-module.h>
#include <util/platform.h>
#include <plugin-support.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

QString configFilePath()
{
	char *path = obs_module_config_path("config.json");
	QString result = QString::fromUtf8(path);
	bfree(path);
	return result;
}

} // namespace

PluginConfig PluginConfig::load()
{
	PluginConfig config;

	QFile file(configFilePath());
	if (!file.open(QIODevice::ReadOnly))
		return config;

	QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
	config.accessToken = root.value("access_token").toString();
	config.authMode = root.value("auth_mode").toString(QStringLiteral("streamkit"));
	config.clientId = root.value("client_id").toString();
	config.clientSecret = root.value("client_secret").toString();
	for (const QJsonValue &v : root.value("rules").toArray())
		config.rules.append(Rule::fromJson(v.toObject()));

	obs_log(LOG_INFO, "loaded %d rule(s)", (int)config.rules.size());
	return config;
}

void PluginConfig::save() const
{
	char *dir = obs_module_config_path(nullptr);
	os_mkdirs(dir);
	bfree(dir);

	QJsonObject root;
	root["access_token"] = accessToken;
	root["auth_mode"] = authMode.isEmpty() ? QStringLiteral("streamkit") : authMode;
	root["client_id"] = clientId;
	root["client_secret"] = clientSecret;
	QJsonArray ruleArray;
	for (const Rule &rule : rules)
		ruleArray.append(rule.toJson());
	root["rules"] = ruleArray;

	QFile file(configFilePath());
	if (!file.open(QIODevice::WriteOnly)) {
		obs_log(LOG_ERROR, "failed to write config file");
		return;
	}
	file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
}
