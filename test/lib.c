struct O {
    int n;
} _o[2];

struct O* lib(struct O* o) {
    if (o == &_o[0])
        return &_o[1];
    return &_o[0];
}
