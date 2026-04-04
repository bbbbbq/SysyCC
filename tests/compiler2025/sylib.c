extern int scanf(char format[], ...);
extern int printf(char format[], ...);
extern int getchar(void);
extern int putchar(int value);

int getint() {
    int value = 0;
    scanf("%d", &value);
    return value;
}

int getch() { return getchar(); }

float getfloat() {
    float value = 0.0;
    scanf("%a", &value);
    return value;
}

int getarray(int a[]) {
    int n = getint();
    int i = 0;
    while (i < n) {
        a[i] = getint();
        i = i + 1;
    }
    return n;
}

int getfarray(float a[]) {
    int n = getint();
    int i = 0;
    while (i < n) {
        a[i] = getfloat();
        i = i + 1;
    }
    return n;
}

void putint(int a) { printf("%d", a); }

void putch(int a) { putchar(a); }

void putfloat(float a) { printf("%a", a); }

void putarray(int n, int a[]) {
    int i = 0;
    putint(n);
    putch(58);
    while (i < n) {
        putch(32);
        putint(a[i]);
        i = i + 1;
    }
    putch(10);
}

void putfarray(int n, float a[]) {
    int i = 0;
    putint(n);
    putch(58);
    while (i < n) {
        putch(32);
        putfloat(a[i]);
        i = i + 1;
    }
    putch(10);
}

void putf(char a[], ...) { printf("%s", a); }

void _sysy_starttime(int lineno) {}

void _sysy_stoptime(int lineno) {}
