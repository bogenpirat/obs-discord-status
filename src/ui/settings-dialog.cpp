#include "settings-dialog.h"
#include "../discord/rpc-client.h"
#include "../rules/engine.h"
#include "../config.h"

#include <obs.h>
#include <obs-frontend-api.h>

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

QStringList sceneNames()
{
	QStringList names;
	char **scenes = obs_frontend_get_scene_names();
	for (char **name = scenes; name && *name; name++)
		names.append(QString::fromUtf8(*name));
	bfree(scenes);
	return names;
}

QStringList sourceNames(bool audioOnly)
{
	struct EnumData {
		QStringList names;
		bool audioOnly;
	} data{{}, audioOnly};

	obs_enum_sources(
		[](void *param, obs_source_t *source) {
			auto *d = static_cast<EnumData *>(param);
			if (d->audioOnly && !(obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO))
				return true;
			d->names.append(QString::fromUtf8(obs_source_get_name(source)));
			return true;
		},
		&data);
	data.names.sort(Qt::CaseInsensitive);
	return data.names;
}

class ActionEditDialog : public QDialog {
public:
	ActionEditDialog(RuleAction &action, QWidget *parent) : QDialog(parent), m_action(action)
	{
		setWindowTitle("Edit Action");
		auto *form = new QFormLayout;

		m_type = new QComboBox;
		for (int i = 0; i <= (int)ActionType::SetVolume; i++)
			m_type->addItem(actionDisplayName((ActionType)i));
		m_type->setCurrentIndex((int)action.type);
		form->addRow("Action:", m_type);

		m_scene = new QComboBox;
		m_scene->setEditable(true);
		m_scene->addItem(""); // empty = all scenes
		m_scene->addItems(sceneNames());
		m_scene->setCurrentText(action.sceneName);
		form->addRow("Scene (empty = all):", m_scene);

		m_source = new QComboBox;
		m_source->setEditable(true);
		form->addRow("Source:", m_source);

		m_volume = new QDoubleSpinBox;
		m_volume->setRange(0.0, 200.0);
		m_volume->setSuffix("%");
		m_volume->setValue(action.volumePercent);
		form->addRow("Volume (100% = 0 dB):", m_volume);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

		auto *layout = new QVBoxLayout(this);
		layout->addLayout(form);
		layout->addWidget(buttons);

		auto updateFields = [this] {
			auto type = (ActionType)m_type->currentIndex();
			bool visibility = type == ActionType::ShowSource || type == ActionType::HideSource;
			m_scene->setEnabled(visibility);
			m_volume->setEnabled(type == ActionType::SetVolume);

			QString current = m_source->currentText();
			m_source->clear();
			m_source->addItems(sourceNames(!visibility));
			m_source->setCurrentText(current);
		};
		connect(m_type, &QComboBox::currentIndexChanged, this, updateFields);
		updateFields();
		m_source->setCurrentText(action.sourceName);
	}

	void accept() override
	{
		if (m_source->currentText().trimmed().isEmpty()) {
			QMessageBox::warning(this, "Discord Status", "Please select a source.");
			return;
		}
		m_action.type = (ActionType)m_type->currentIndex();
		m_action.sceneName = m_scene->isEnabled() ? m_scene->currentText().trimmed() : QString();
		m_action.sourceName = m_source->currentText().trimmed();
		m_action.volumePercent = m_volume->value();
		QDialog::accept();
	}

private:
	RuleAction &m_action;
	QComboBox *m_type;
	QComboBox *m_scene;
	QComboBox *m_source;
	QDoubleSpinBox *m_volume;
};

class RuleEditDialog : public QDialog {
public:
	RuleEditDialog(Rule &rule, QWidget *parent) : QDialog(parent), m_rule(rule)
	{
		setWindowTitle("Edit Rule");
		resize(480, 400);
		auto *form = new QFormLayout;

		m_name = new QLineEdit(rule.name);
		form->addRow("Name:", m_name);

		m_trigger = new QComboBox;
		for (int i = 0; i <= (int)TriggerType::AnySpeakingStopped; i++)
			m_trigger->addItem(triggerDisplayName((TriggerType)i));
		m_trigger->setCurrentIndex((int)rule.trigger);
		form->addRow("When:", m_trigger);

		m_userFilter = new QLineEdit(rule.userFilter);
		m_userFilter->setPlaceholderText("anyone (or enter a username / user ID)");
		form->addRow("Only for user:", m_userFilter);

		auto *layout = new QVBoxLayout(this);
		layout->addLayout(form);

		layout->addWidget(new QLabel("Actions:"));
		m_actionList = new QListWidget;
		layout->addWidget(m_actionList);

		auto *actionButtons = new QHBoxLayout;
		auto *add = new QPushButton("Add...");
		auto *edit = new QPushButton("Edit...");
		auto *remove = new QPushButton("Remove");
		actionButtons->addWidget(add);
		actionButtons->addWidget(edit);
		actionButtons->addWidget(remove);
		actionButtons->addStretch();
		layout->addLayout(actionButtons);

		auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
		connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
		connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
		layout->addWidget(buttons);

		connect(add, &QPushButton::clicked, this, [this] {
			RuleAction action;
			if (ActionEditDialog(action, this).exec() == QDialog::Accepted) {
				m_actions.append(action);
				refreshActions();
			}
		});
		connect(edit, &QPushButton::clicked, this, [this] {
			int row = m_actionList->currentRow();
			if (row < 0)
				return;
			if (ActionEditDialog(m_actions[row], this).exec() == QDialog::Accepted)
				refreshActions();
		});
		connect(remove, &QPushButton::clicked, this, [this] {
			int row = m_actionList->currentRow();
			if (row >= 0) {
				m_actions.removeAt(row);
				refreshActions();
			}
		});
		connect(m_trigger, &QComboBox::currentIndexChanged, this, [this] { updateUserFilterEnabled(); });

		m_actions = rule.actions;
		refreshActions();
		updateUserFilterEnabled();
	}

	void accept() override
	{
		if (m_actions.isEmpty()) {
			QMessageBox::warning(this, "Discord Status", "Add at least one action.");
			return;
		}
		m_rule.name = m_name->text().trimmed().isEmpty() ? m_trigger->currentText() : m_name->text().trimmed();
		m_rule.trigger = (TriggerType)m_trigger->currentIndex();
		m_rule.userFilter = m_userFilter->isEnabled() ? m_userFilter->text().trimmed() : QString();
		m_rule.actions = m_actions;
		QDialog::accept();
	}

private:
	void refreshActions()
	{
		m_actionList->clear();
		for (const RuleAction &action : m_actions)
			m_actionList->addItem(action.describe());
	}

	void updateUserFilterEnabled()
	{
		m_userFilter->setEnabled(triggerHasUserFilter((TriggerType)m_trigger->currentIndex()));
	}

	Rule &m_rule;
	QList<RuleAction> m_actions;
	QLineEdit *m_name;
	QComboBox *m_trigger;
	QLineEdit *m_userFilter;
	QListWidget *m_actionList;
};

} // namespace

bool editRuleDialog(Rule &rule, QWidget *parent)
{
	return RuleEditDialog(rule, parent).exec() == QDialog::Accepted;
}

SettingsDialog::SettingsDialog(DiscordRpcClient *client, RulesEngine *engine, PluginConfig *config, QWidget *parent)
	: QDialog(parent),
	  m_client(client),
	  m_engine(engine),
	  m_config(config)
{
	setWindowTitle("Discord Status");
	resize(560, 420);

	auto *layout = new QVBoxLayout(this);

	m_statusLabel = new QLabel("Connecting to Discord...");
	layout->addWidget(m_statusLabel);
	connect(client, &DiscordRpcClient::statusChanged, this,
		[this](const QString &status) { m_statusLabel->setText(status); });

	// Auth mode: StreamKit (zero setup) or the user's own Discord app.
	auto *authForm = new QFormLayout;
	auto *authMode = new QComboBox;
	authMode->addItem("StreamKit (no setup required)");
	authMode->addItem("Own Discord application");
	auto *clientId = new QLineEdit(m_config->clientId);
	auto *clientSecret = new QLineEdit(m_config->clientSecret);
	clientSecret->setEchoMode(QLineEdit::Password);
	auto *applyAuth = new QPushButton("Apply && Reconnect");
	authForm->addRow("Discord auth:", authMode);
	authForm->addRow("Client ID:", clientId);
	authForm->addRow("Client secret:", clientSecret);
	authForm->addRow("", applyAuth);
	layout->addLayout(authForm);

	bool ownApp = m_config->authMode == QStringLiteral("ownapp");
	authMode->setCurrentIndex(ownApp ? 1 : 0);
	clientId->setEnabled(ownApp);
	clientSecret->setEnabled(ownApp);
	connect(authMode, &QComboBox::currentIndexChanged, this, [clientId, clientSecret](int index) {
		clientId->setEnabled(index == 1);
		clientSecret->setEnabled(index == 1);
	});
	connect(applyAuth, &QPushButton::clicked, this, [this, authMode, clientId, clientSecret] {
		bool own = authMode->currentIndex() == 1;
		m_config->authMode = own ? QStringLiteral("ownapp") : QStringLiteral("streamkit");
		m_config->clientId = clientId->text().trimmed();
		m_config->clientSecret = clientSecret->text().trimmed();
		// Token from the old mode/app is not valid for the new one.
		m_config->accessToken.clear();
		m_config->save();
		m_client->setAccessToken(QString());
		m_client->setAuthMode(own ? DiscordAuthMode::OwnApp : DiscordAuthMode::StreamKit,
				      m_config->clientId, m_config->clientSecret);
		m_client->restart();
	});

	layout->addWidget(new QLabel("Rules:"));
	m_ruleList = new QListWidget;
	layout->addWidget(m_ruleList);

	auto *buttons = new QHBoxLayout;
	auto *add = new QPushButton("Add...");
	m_editButton = new QPushButton("Edit...");
	m_removeButton = new QPushButton("Remove");
	m_toggleButton = new QPushButton("Enable/Disable");
	buttons->addWidget(add);
	buttons->addWidget(m_editButton);
	buttons->addWidget(m_removeButton);
	buttons->addWidget(m_toggleButton);
	buttons->addStretch();
	layout->addLayout(buttons);

	auto *close = new QDialogButtonBox(QDialogButtonBox::Close);
	connect(close, &QDialogButtonBox::rejected, this, &QDialog::reject);
	layout->addWidget(close);

	connect(add, &QPushButton::clicked, this, &SettingsDialog::addRule);
	connect(m_editButton, &QPushButton::clicked, this, &SettingsDialog::editRule);
	connect(m_removeButton, &QPushButton::clicked, this, &SettingsDialog::removeRule);
	connect(m_toggleButton, &QPushButton::clicked, this, &SettingsDialog::toggleRule);
	connect(m_ruleList, &QListWidget::itemDoubleClicked, this, &SettingsDialog::editRule);

	refreshRuleList();
}

void SettingsDialog::refreshRuleList()
{
	m_ruleList->clear();
	for (const Rule &rule : m_config->rules) {
		QString label = QStringLiteral("%1%2  [%3, %4 action(s)]")
					.arg(rule.enabled ? "" : "(disabled) ", rule.name,
					     triggerDisplayName(rule.trigger))
					.arg(rule.actions.size());
		m_ruleList->addItem(label);
	}
}

void SettingsDialog::addRule()
{
	Rule rule;
	if (editRuleDialog(rule, this)) {
		m_config->rules.append(rule);
		persist();
	}
}

void SettingsDialog::editRule()
{
	int row = m_ruleList->currentRow();
	if (row < 0)
		return;
	if (editRuleDialog(m_config->rules[row], this))
		persist();
}

void SettingsDialog::removeRule()
{
	int row = m_ruleList->currentRow();
	if (row < 0)
		return;
	m_config->rules.removeAt(row);
	persist();
}

void SettingsDialog::toggleRule()
{
	int row = m_ruleList->currentRow();
	if (row < 0)
		return;
	m_config->rules[row].enabled = !m_config->rules[row].enabled;
	persist();
}

void SettingsDialog::persist()
{
	m_engine->setRules(m_config->rules);
	m_config->save();
	refreshRuleList();
}
