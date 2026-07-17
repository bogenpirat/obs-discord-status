#pragma once

#include "rule.h"

// Executes a rule's actions against OBS. Must be called on the OBS/Qt main
// thread (scene item and source mutations are not marshalled internally).
void executeActions(const QList<RuleAction> &actions);
