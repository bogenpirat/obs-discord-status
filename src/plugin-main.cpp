/*
Discord Status for OBS
Copyright (C) 2026 Julian <juliii@gmail.com>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

#include "config.h"
#include "discord/rpc-client.h"
#include "voice-state.h"
#include "rules/engine.h"
#include "ui/settings-dialog.h"

#include <QMainWindow>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

namespace {

PluginConfig *config;
DiscordRpcClient *client;
VoiceStateModel *model;
RulesEngine *engine;

void showSettingsDialog(void *)
{
	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	SettingsDialog dialog(client, engine, config, mainWindow);
	dialog.exec();
}

void onFrontendEvent(enum obs_frontend_event event, void *)
{
	// Start the Discord connection only once OBS is fully loaded so rule
	// actions never race scene collection loading.
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		client->start();
}

} // namespace

bool obs_module_load(void)
{
	config = new PluginConfig(PluginConfig::load());

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	client = new DiscordRpcClient(mainWindow ? static_cast<QObject *>(mainWindow) : nullptr);
	client->setAccessToken(config->accessToken);
	client->setAuthMode(config->authMode == QStringLiteral("ownapp") ? DiscordAuthMode::OwnApp
									 : DiscordAuthMode::StreamKit,
			    config->clientId, config->clientSecret);
	QObject::connect(client, &DiscordRpcClient::accessTokenChanged, client, [](const QString &token) {
		config->accessToken = token;
		config->save();
	});

	model = new VoiceStateModel(client, client);
	engine = new RulesEngine(model, client);
	engine->setRules(config->rules);

	obs_frontend_add_tools_menu_item(obs_module_text("DiscordStatus.Settings"), showSettingsDialog, nullptr);
	obs_frontend_add_event_callback(onFrontendEvent, nullptr);

	obs_log(LOG_INFO, "plugin loaded successfully (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	if (client) {
		client->stop();
		// client owns model/engine via Qt parenting; deleted with it.
		delete client;
		client = nullptr;
	}
	delete config;
	config = nullptr;
	obs_log(LOG_INFO, "plugin unloaded");
}
