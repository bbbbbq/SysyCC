const char shared_message[] = "SysyCC PIC";
const unsigned long shared_message_len = sizeof(shared_message) - 1;

const char *shared_message_ptr(void) {
    return shared_message;
}
