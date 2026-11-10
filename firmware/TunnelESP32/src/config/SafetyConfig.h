#pragma once
// Emergency is the DWIN button VP 0x1040, not one of the sensor inputs.
// It latches STOP and releases the TB6600; an explicit LCD Reset is required.
#define EMERGENCY_GPIO (-1)
#define EMERGENCY_ACTIVE_LEVEL 1
// NPN output sinks EN- when GPIO9=HIGH. Typical TB6600 opto ON = offline.
#define STEP_RELEASE_LEVEL 1
#define STEP_HOLD_LEVEL 0
// Logical CLOSE=0, OPEN=1. Set the relevant value to 1 if that door's
// mechanical installation requires the opposite electrical DIR level.
#define GATE1_DIRECTION_INVERT 0
#define GATE2_DIRECTION_INVERT 0
