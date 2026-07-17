#include "actions.h"

#include <obs-module.h>
#include <obs-frontend-api.h>
#include <plugin-support.h>

namespace {

void setSceneItemVisibleInScene(obs_source_t *sceneSource, const char *sourceName, bool visible, bool *found)
{
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene)
		return;
	obs_sceneitem_t *item = obs_scene_find_source_recursive(scene, sourceName);
	if (item) {
		obs_sceneitem_set_visible(item, visible);
		*found = true;
	}
}

void setSceneItemVisible(const QString &sceneName, const QString &sourceName, bool visible)
{
	QByteArray sourceUtf8 = sourceName.toUtf8();
	bool found = false;

	if (!sceneName.isEmpty()) {
		obs_source_t *sceneSource = obs_get_source_by_name(sceneName.toUtf8().constData());
		if (!sceneSource) {
			obs_log(LOG_WARNING, "action skipped: scene '%s' not found",
				sceneName.toUtf8().constData());
			return;
		}
		setSceneItemVisibleInScene(sceneSource, sourceUtf8.constData(), visible, &found);
		obs_source_release(sceneSource);
	} else {
		struct obs_frontend_source_list scenes = {};
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num; i++)
			setSceneItemVisibleInScene(scenes.sources.array[i], sourceUtf8.constData(), visible, &found);
		obs_frontend_source_list_free(&scenes);
	}

	if (!found)
		obs_log(LOG_WARNING, "action skipped: source '%s' not found in %s", sourceUtf8.constData(),
			sceneName.isEmpty() ? "any scene" : "scene");
}

void setSourceMuted(const QString &sourceName, bool muted)
{
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source) {
		obs_log(LOG_WARNING, "action skipped: audio source '%s' not found",
			sourceName.toUtf8().constData());
		return;
	}
	obs_source_set_muted(source, muted);
	obs_source_release(source);
}

void setSourceVolume(const QString &sourceName, double percent)
{
	obs_source_t *source = obs_get_source_by_name(sourceName.toUtf8().constData());
	if (!source) {
		obs_log(LOG_WARNING, "action skipped: audio source '%s' not found",
			sourceName.toUtf8().constData());
		return;
	}
	obs_source_set_volume(source, (float)(percent / 100.0));
	obs_source_release(source);
}

} // namespace

void executeActions(const QList<RuleAction> &actions)
{
	for (const RuleAction &action : actions) {
		switch (action.type) {
		case ActionType::ShowSource:
			setSceneItemVisible(action.sceneName, action.sourceName, true);
			break;
		case ActionType::HideSource:
			setSceneItemVisible(action.sceneName, action.sourceName, false);
			break;
		case ActionType::MuteSource:
			setSourceMuted(action.sourceName, true);
			break;
		case ActionType::UnmuteSource:
			setSourceMuted(action.sourceName, false);
			break;
		case ActionType::SetVolume:
			setSourceVolume(action.sourceName, action.volumePercent);
			break;
		}
	}
}
