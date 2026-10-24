// Cloud path: NTP time + MQTT to NETPIE, publishing the node table every 10 s.
#pragma once

// Call after Wi-Fi is started. Creates cloud_task (priority 2).
void cloud_start();
