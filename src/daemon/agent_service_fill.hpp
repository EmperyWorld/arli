#pragma once

#include "db.hpp"

#include "arli.grpc.pb.h"

namespace arli {

void fill_task(const TaskRow& t, v1::TaskInfo* out);
void fill_settings(const SettingsRow& s, v1::Settings* out);
void fill_dialog(const DialogRow& d, v1::Dialog* out);

}  // namespace arli
