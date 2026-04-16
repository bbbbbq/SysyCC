#define __builtin_shuffle(vec, mask) ({                                      \
    __typeof__(vec) _sysycc_vec = (vec);                                     \
    __typeof__(mask) _sysycc_mask = (mask);                                  \
    __typeof__(vec) _sysycc_result;                                          \
    _sysycc_result[0] = _sysycc_vec[((long long)_sysycc_mask[0]) & 1LL];    \
    _sysycc_result[1] = _sysycc_vec[((long long)_sysycc_mask[1]) & 1LL];    \
    _sysycc_result;                                                          \
})
