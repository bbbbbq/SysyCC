static int grid[16];

int accumulate(int n) {
    int sum = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            int idx = i * n + j;
            grid[idx] = i * 3 + j * 5 + (i == j);
            sum += grid[idx];
        }
    }
    return sum;
}

int main(void) {
    int value = accumulate(4);
    return value == 196 ? 0 : 1;
}
