#pragma once

#include "rules/rule.h"

#include <QList>
#include <QString>

// Plugin configuration persisted as JSON in the module config directory.
struct PluginConfig {
	QString accessToken;
	// "streamkit" (default) or "ownapp"; see DiscordAuthMode.
	QString authMode;
	QString clientId;
	QString clientSecret;
	QList<Rule> rules;

	static PluginConfig load();
	void save() const;
};
