// Notifications (M6): cycle events from q_notices -> Discord webhook.
#pragma once

// Call after state_init(). Creates notify_task (priority 1).
void notify_start();
