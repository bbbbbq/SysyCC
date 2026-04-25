int shared_counter = 1;

int apply_delta(int delta) {
    shared_counter += delta;
    return shared_counter;
}
