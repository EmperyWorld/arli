#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

#include <optional>

namespace arli::trace {

// TurnEvent payload → StepRow (nullopt for payloads that are not recorded: Queued, empty).
std::optional<StepRow> step_from_event(const v1::TurnEvent& ev, std::int64_t task_id);

// StepRow → TurnEvent for AttachTurn replay.
v1::TurnEvent event_from_step(const StepRow& step);

// StepRow → TraceItem for OpenDialog.last_trace / GetTurnTrace.
void fill_trace_item(const StepRow& step, v1::TraceItem* out);

}  // namespace arli::trace
