#pragma once
#include "turn.hpp"
namespace arli {
void run_revert(Database&,const v1::ClaimTask&,EventSink,std::atomic<bool>*,const std::function<void()>& check_workspace={});
}
