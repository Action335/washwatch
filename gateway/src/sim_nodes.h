// gateway_sim build only: two simulated washer nodes, so the cloud path can be tested in Wokwi
// without real boards. Node 1 washes for 60 s then goes quiet; node 2 stays idle.
#pragma once

// Call after state_init(). Creates sim_nodes_task (priority 3).
void sim_nodes_start();
