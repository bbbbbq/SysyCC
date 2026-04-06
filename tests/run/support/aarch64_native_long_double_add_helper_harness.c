long double add_one(long double x);
void putint(int value);

int main(void) {
    putint((int)add_one((long double)41));
    return 0;
}
