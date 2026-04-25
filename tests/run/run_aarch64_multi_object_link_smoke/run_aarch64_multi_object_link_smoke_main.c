extern void abort(void);
extern int helper(int value);
extern int *shared_counter_slot(void);

int main(void) {
    int *counter = shared_counter_slot();
    const int result = helper(41);
    if (result != 42) {
        abort();
    }
    if (counter == 0 || *counter != 42) {
        abort();
    }
    return 0;
}
