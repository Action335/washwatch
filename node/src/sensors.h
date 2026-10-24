// Reads whichever sensors this node build has fitted (HAS_* flags) into a node_msg_t.
#pragma once
#include "airbox_msg.h"

void sensors_begin();
// Fills the sensor fields and flags. Fields for sensors that aren't fitted get NOT_FITTED.
void sensors_read(node_msg_t& m);
