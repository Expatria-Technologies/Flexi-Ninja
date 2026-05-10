#ifndef SERIAL_TERMINAL_H
#define SERIAL_TERMINAL_H

void save_configuration();
void load_configuration();
void handle_serial_input();
void process_command(char* command); // Only char* command, net_info is global

#endif
// SERIAL_TERMINAL_H 