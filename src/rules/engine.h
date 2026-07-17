#pragma once

#include "rule.h"

#include <QObject>

class VoiceStateModel;
struct VoiceParticipant;

// Matches voice-state edges against the configured rules and runs their
// actions. Lives on the OBS/Qt main thread; the model's signals are delivered
// there, so actions execute directly.
class RulesEngine : public QObject {
	Q_OBJECT

public:
	explicit RulesEngine(VoiceStateModel *model, QObject *parent = nullptr);

	QList<Rule> rules() const { return m_rules; }
	void setRules(const QList<Rule> &rules) { m_rules = rules; }

private:
	void fire(TriggerType trigger, const VoiceParticipant *user = nullptr);
	QList<Rule> m_rules;
};
