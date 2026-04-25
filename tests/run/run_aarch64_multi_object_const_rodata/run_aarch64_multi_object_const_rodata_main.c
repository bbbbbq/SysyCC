extern void abort(void);
extern const char shared_message[];
extern const unsigned long shared_message_len;
extern const char *shared_message_ptr(void);

int main(void) {
    const char *message = shared_message_ptr();
    if (message != shared_message) {
        abort();
    }
    if (shared_message_len != 10UL) {
        abort();
    }
    if (shared_message[0] != 'S' || shared_message[4] != 'C') {
        abort();
    }
    return 0;
}
