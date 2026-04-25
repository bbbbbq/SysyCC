int shared_counter = 0;

int *shared_counter_slot(void) {
    return &shared_counter;
}

int helper(int value) {
    int *slot = shared_counter_slot();
    *slot = value + 1;
    return *slot;
}
