extern int puts(const char *);
extern const char *message_from_helper(void);

int main(void) {
    const char *message = message_from_helper();
    if (message == 0 || message[0] != 'A') {
        return 2;
    }
    if (puts(message) < 0) {
        return 3;
    }
    return 0;
}
