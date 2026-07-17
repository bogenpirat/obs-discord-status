#pragma once

#include "rules/rule.h"

#include <QList>
#include <QString>

// Plugin configuration persisted as JSON in the module config directory.
struct PluginConfig {
	QString accessToken;
	QList<Rule> rules;

	static PluginConfig load();
	void save() const;
};
