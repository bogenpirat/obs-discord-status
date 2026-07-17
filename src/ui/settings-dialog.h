#pragma once

#include "../rules/rule.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QPushButton;
class DiscordRpcClient;
class RulesEngine;
struct PluginConfig;

// Tools-menu dialog: connection status + rules CRUD.
class SettingsDialog : public QDialog {
	Q_OBJECT

public:
	SettingsDialog(DiscordRpcClient *client, RulesEngine *engine, PluginConfig *config, QWidget *parent = nullptr);

private:
	void refreshRuleList();
	void addRule();
	void editRule();
	void removeRule();
	void toggleRule();
	void persist();

	DiscordRpcClient *m_client;
	RulesEngine *m_engine;
	PluginConfig *m_config;

	QLabel *m_statusLabel;
	QListWidget *m_ruleList;
	QPushButton *m_editButton;
	QPushButton *m_removeButton;
	QPushButton *m_toggleButton;
};

// Modal editor for a single rule; returns true when accepted.
bool editRuleDialog(Rule &rule, QWidget *parent);
