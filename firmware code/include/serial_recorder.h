#pragma once

void printRecordingHelp();
bool takeSerialScheduleRequest();
// Main loop only. Returns true after processing a complete command.
bool pollRecordingCommands();
