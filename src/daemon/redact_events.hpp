#pragma once
#include "turn.hpp"
#include "redact.hpp"
namespace arli {
inline EventSink redact_events(EventSink destination) {
  return [destination=std::move(destination),stream=redact::Stream{},last=v1::TurnEvent{}](const v1::TurnEvent& input) mutable {
    if(!destination)return true;
    redact::ensure_available();
    auto event=input;
    if(event.has_token()) {
      last=event;event.mutable_token()->set_text(stream.push(event.token().text()));
      return event.token().text().empty()||destination(event);
    }
    const auto tail=stream.finish();
    if(!tail.empty()){last.mutable_token()->set_text(tail);if(!destination(last))return false;}
    redact::message(event);return destination(event);
  };
}
}
