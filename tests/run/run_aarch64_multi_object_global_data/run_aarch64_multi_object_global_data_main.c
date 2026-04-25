extern void abort(void);
extern int shared_counter;
extern int apply_delta(int delta);

int main(void) {
    shared_counter = 10;
    if (apply_delta(7) != 17) {
        abort();
    }
    if (shared_counter != 17) {
        abort();
    }
    return 0;
}
