// Local control: servos (dial + flag per washer) and the gateway's own sensors (PIR, DHT11, rain).
// Runs without the internet.
#pragma once

// Call after state_init(). Attaches the servos and creates control_task (priority 4).
void control_start();
