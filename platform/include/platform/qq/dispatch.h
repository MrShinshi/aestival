/*
 * aestival
 * Copyright (c) 2026 MrShinshi
 * Licensed under MIT
 */
#pragma once
#include "session.h"

namespace platform::qq {

void dispatch_event(session& s, std::string_view event, std::string_view data);

} // namespace platform::qq
